"""Qt/asyncio bridge. Windows and BLE work never blocks the editor thread."""
from __future__ import annotations
import asyncio
import ctypes
import logging
import sys
import threading
import uuid
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import Any

from PySide6.QtCore import QObject, Signal

from .actions import ActionEngine
from .bluetooth import BluetoothManager
from .monitor import MediaMonitor, MonitorEngine, OptionalTemperatureMonitor
from .profiles import ProfileManager
from .settings import Settings

log = logging.getLogger(__name__)


class Controller(QObject):
    status_changed = Signal(str, str)
    telemetry_changed = Signal(dict)
    devices_found = Signal(list)
    sync_finished = Signal(bool, str)
    action_finished = Signal(str, str)
    address_changed = Signal(str)
    error = Signal(str)
    pairing_requested = Signal(str, str)
    profile_selected = Signal(str)
    timer_finished = Signal(str, str)

    def __init__(self, settings: Settings, profiles: ProfileManager, directory: Path):
        super().__init__()
        self.settings, self.profiles, self.directory = settings, profiles, directory
        self.loop: asyncio.AbstractEventLoop | None = None
        self.manager: BluetoothManager | None = None
        self.engine: ActionEngine | None = None
        self.thread: threading.Thread | None = None
        self.stop_event: asyncio.Event | None = None
        self.pin_requests: dict[str, asyncio.Future] = {}
        self.address_changed.connect(self._save_address)
        self.profile_selected.connect(self._save_active_profile)

    def start(self) -> None:
        if self.thread is not None:
            return
        self.thread = threading.Thread(target=self._thread_main, name="NexusServices", daemon=True)
        self.thread.start()

    def _thread_main(self) -> None:
        initialized = False
        try:
            if sys.platform == "win32":
                # BLE/WinRT callbacks need an MTA thread; Qt owns the GUI thread.
                ole = ctypes.OleDLL("ole32")
                ole.CoInitializeEx(None, 0)
                initialized = True
            asyncio.run(self._run())
        except Exception as exc:
            log.exception("Background services stopped")
            self.status_changed.emit("disconnected", f"Services could not start: {exc}")
            self.error.emit(str(exc))
        finally:
            if initialized:
                ctypes.OleDLL("ole32").CoUninitialize()
            self.loop = None

    async def _request_pin(self, address: str) -> str | None:
        token = uuid.uuid4().hex
        future = asyncio.get_running_loop().create_future()
        self.pin_requests[token] = future
        self.pairing_requested.emit(token, address)
        try:
            return await asyncio.wait_for(future, 65.0)
        finally:
            self.pin_requests.pop(token, None)

    def answer_pairing(self, token: str, pin: str | None) -> None:
        def answer() -> None:
            future = self.pin_requests.get(token)
            if future is not None and not future.done():
                future.set_result(pin)
        if self.loop and self.loop.is_running():
            self.loop.call_soon_threadsafe(answer)

    async def _run(self) -> None:
        self.loop = asyncio.get_running_loop()
        self.stop_event = asyncio.Event()
        engine = ActionEngine(self.settings,timer_callback=self.timer_finished.emit)
        self.engine = engine
        self.manager = BluetoothManager(self.settings, engine, self.status_changed.emit,
                                        self.action_finished.emit, self.address_changed.emit,
                                        self._request_pin, self.profile_selected.emit)
        self.manager.configure_profiles(self.profiles.snapshot(), self.settings.active_profile)
        monitor_worker = ThreadPoolExecutor(max_workers=1, thread_name_prefix="NexusTelemetry")
        monitor = await self.loop.run_in_executor(monitor_worker, MonitorEngine, self.settings, engine.audio)
        media = MediaMonitor()
        temperatures = OptionalTemperatureMonitor()
        media_sample = None
        temperature_sample: tuple[float | None, float | None] = (None, None)

        async def pause(seconds: float) -> None:
            try:
                await asyncio.wait_for(self.stop_event.wait(), seconds)
            except asyncio.TimeoutError:
                pass

        async def media_loop():
            nonlocal media_sample
            while not self.stop_event.is_set():
                media_sample = await media.sample()
                await pause(3.0)

        async def temperature_loop():
            nonlocal temperature_sample
            while not self.stop_event.is_set():
                temperature_sample = await asyncio.to_thread(temperatures.sample)
                await pause(10.0 if any(v is not None for v in temperature_sample) else 30.0)

        async def monitor_loop():
            while not self.stop_event.is_set():
                try:
                    telemetry = await self.loop.run_in_executor(monitor_worker, monitor.sample)
                    telemetry["media"] = media_sample
                    for key, value in zip(("cpu_temp", "gpu_temp"), temperature_sample):
                        if telemetry.get(key) is None and value is not None:
                            telemetry[key] = value
                    self.manager.latest_telemetry = telemetry
                    self.telemetry_changed.emit(telemetry)
                except Exception as exc:
                    log.warning("Monitoring failed: %s", exc)
                await pause(self.settings.telemetry_interval)

        tasks = [asyncio.create_task(monitor_loop()), asyncio.create_task(media_loop()),
                 asyncio.create_task(temperature_loop()), asyncio.create_task(self.manager.run())]
        try:
            await self.stop_event.wait()
        finally:
            await self.manager.stop()
            for task in tasks:
                task.cancel()
            await asyncio.gather(*tasks, return_exceptions=True)
            await self.loop.run_in_executor(monitor_worker, monitor.close)
            monitor_worker.shutdown(wait=True)

    def _submit(self, factory) -> None:
        if not self.loop or not self.manager:
            self.error.emit("Services are starting; try again in a moment")
            return
        future = asyncio.run_coroutine_threadsafe(factory(), self.loop)
        def finished(result):
            try:
                result.result()
            except Exception as exc:
                log.info("Requested operation failed: %s", exc)
                self.error.emit(str(exc))
        future.add_done_callback(finished)

    def discover(self) -> None:
        async def operation():
            self.devices_found.emit(await BluetoothManager.discover())
        self._submit(operation)

    def connect_device(self, address: str = "") -> None:
        self._submit(lambda: self.manager.connect_to(address))

    def disconnect_device(self) -> None:
        self._submit(lambda: self.manager.disconnect())

    def test_timer_sound(self) -> None:
        async def operation():
            if not self.engine:
                raise RuntimeError("Background services are starting")
            await asyncio.to_thread(self.engine.alerts.test)
        self._submit(operation)

    def synchronize(self, active: str) -> None:
        if not self.loop or not self.manager:
            self.sync_finished.emit(False, "Services are starting; try again in a moment")
            return
        snapshot = self.profiles.snapshot()
        self.settings.active_profile = active
        self.settings.save(self.directory)
        async def operation():
            try:
                await self.manager.synchronize(snapshot, active)
                self.sync_finished.emit(True, "Profiles are saved on the deck and available offline")
            except Exception as exc:
                self.sync_finished.emit(False, str(exc))
        self._submit(operation)

    def profiles_updated(self) -> None:
        """Keep the next reconnect snapshot current without changing the live action registry."""
        snapshot = self.profiles.snapshot()
        active = self.settings.active_profile
        if active not in snapshot:
            active = next(iter(snapshot))
            self.settings.active_profile = active
            self.settings.save(self.directory)
        if self.loop and self.manager:
            self.loop.call_soon_threadsafe(self.manager.configure_profiles, snapshot, active)

    def update_settings(self) -> None:
        self.settings.save(self.directory)
        if self.loop and self.manager:
            self.loop.call_soon_threadsafe(setattr, self.manager, "enabled", self.settings.auto_connect)
            self.loop.call_soon_threadsafe(self.manager.wake.set)

    def _save_address(self, address: str) -> None:
        self.settings.device_address = address
        self.settings.save(self.directory)

    def _save_active_profile(self, profile: str) -> None:
        if profile in self.profiles.profiles:
            self.settings.active_profile = profile
            self.settings.save(self.directory)

    def shutdown(self) -> None:
        if self.loop and self.stop_event and self.loop.is_running():
            self.loop.call_soon_threadsafe(self.stop_event.set)
        if self.thread:
            self.thread.join(timeout=6)
