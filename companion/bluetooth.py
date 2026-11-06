"""BLE central, reconnect logic, synchronization and replay-safe action dispatch."""
from __future__ import annotations

import asyncio
import copy
import logging
import random
import sys
import time
import uuid
from collections import OrderedDict
from collections.abc import Awaitable, Callable
from typing import Any

from bleak import BleakClient, BleakScanner
from bleak.exc import BleakError

from .actions import ActionEngine
from .profiles import ProfileManager, device_profile
from .protocol import (COMMAND_UUID, PROFILE_UUID, RESPONSE_UUID, SERVICE_UUID, STATUS_UUID,
                       TELEMETRY_UUID, ProtocolError, Reassembler, decode_json, encode_json,
                       fragment, sequence)
from .settings import Settings
from .icons import profile_assets
from .alerts import TIMER_ALERTS

log = logging.getLogger(__name__)


class CommandDispatcher:
    """A response cache gives notification retries at-most-once execution per session."""

    def __init__(self, engine: ActionEngine):
        self.engine = engine
        self.session = ""
        self.profiles: dict[str, dict[str, Any]] = {}
        self.cache: OrderedDict[int, dict[str, Any]] = OrderedDict()
        self.highest_sequence = 0
        self.lock = asyncio.Lock()
        self.last_action = 0.0
        self.last_alert = 0.0

    def begin(self, session: str, profiles: dict[str, dict[str, Any]]) -> None:
        self.session, self.profiles = session, copy.deepcopy(profiles)
        self.cache.clear()
        self.highest_sequence = 0
        self.last_alert = 0.0

    async def handle(self, message: dict[str, Any]) -> dict[str, Any]:
        async with self.lock:
            seq = sequence(message.get("sequence"))
            if message.get("session") != self.session or not self.session:
                raise ProtocolError("Stale or unauthorized session")
            if seq in self.cache:
                return self.cache[seq]
            response = {"type": "response", "sequence": seq, "session": self.session,
                        "status": "error", "message": ""}
            if seq <= self.highest_sequence:
                response["message"] = "Expired replay rejected"
                return response
            self.highest_sequence = seq
            try:
                if message.get("type") == "timer_alert":
                    if set(message) != {"type", "sequence", "session", "kind"} or message.get("kind") not in TIMER_ALERTS:
                        raise ValueError("Invalid timer event")
                    if time.monotonic() - self.last_alert < 0.5:
                        raise ValueError("Timer alert rate limit reached")
                    self.last_alert = time.monotonic()
                    result = await asyncio.to_thread(self.engine.timer_alert,message["kind"])
                    response.update(status="success",message=result)
                    self.cache[seq] = response
                    while len(self.cache) > 64:
                        self.cache.popitem(last=False)
                    return response
                if message.get("type") != "action":
                    raise ValueError("Only registered actions are accepted")
                if set(message) - {"type", "sequence", "session", "profile", "id", "gesture", "value"}:
                    raise ValueError("Unrecognized action fields")
                if time.monotonic() - self.last_action < 0.04:
                    raise ValueError("Action rate limit reached")
                self.last_action = time.monotonic()
                if message.get("id") == "system_volume_set":
                    if set(message) != {"type", "sequence", "session", "id", "value"}:
                        raise ValueError("Invalid volume action")
                    value = message.get("value")
                    if type(value) is not int or not 0 <= value <= 100:
                        raise ValueError("Invalid volume value")
                    await asyncio.to_thread(self.engine.audio.set_volume, value)
                else:
                    if "value" in message:
                        raise ValueError("Button actions do not accept arguments over BLE")
                    action = ProfileManager.find_action(self.profiles, message.get("profile"),
                                                        message.get("id"), message.get("gesture"))
                    await asyncio.to_thread(self.engine.execute, action)
                response.update(status="success", message="Action completed")
            except Exception as exc:
                response["message"] = str(exc).encode("utf-8")[:159].decode("utf-8", "ignore")
                log.info("Action %s: %s", message.get("id", "?"), exc)
            self.cache[seq] = response
            while len(self.cache) > 64:
                self.cache.popitem(last=False)
            return response


class BluetoothManager:
    def __init__(self, settings: Settings, engine: ActionEngine,
                 state_callback: Callable[[str, str], None],
                 action_callback: Callable[[str, str], None],
                 address_callback: Callable[[str], None],
                 pin_callback: Callable[[str], Awaitable[str | None]] | None = None,
                 profile_callback: Callable[[str], None] | None = None):
        self.settings = settings
        self.on_state, self.on_action, self.on_address = state_callback, action_callback, address_callback
        self.on_pin, self.on_profile = pin_callback, profile_callback
        self.client: BleakClient | None = None
        self.session = ""
        self.ready = False
        self.icon_assets_supported = False
        self.synced_icons: set[str] = set()
        self.running = True
        self.enabled = settings.auto_connect
        self.wake = asyncio.Event()
        self.disconnected = asyncio.Event()
        self.write_lock = asyncio.Lock()
        self.channel_locks = {key: asyncio.Lock() for key in
                              (TELEMETRY_UUID, RESPONSE_UUID, PROFILE_UUID, STATUS_UUID)}
        self.sync_lock = asyncio.Lock()
        self.command_queue: asyncio.Queue[dict[str, Any]] = asyncio.Queue(maxsize=32)
        self.pending: dict[int, asyncio.Future[dict[str, Any]]] = {}
        self.assemblers = {COMMAND_UUID: Reassembler(1024), STATUS_UUID: Reassembler(2048)}
        self.dispatcher = CommandDispatcher(engine)
        self.sequence = random.randint(1, 1000000)
        self.message_id = random.randint(1, 65535)
        self.latest_telemetry: dict[str, Any] = {}
        self.saved_profiles: dict[str, dict[str, Any]] = {}
        self.profile_generation = 0
        self.active = settings.active_profile

    def _next_sequence(self) -> int:
        self.sequence = self.sequence % 0xFFFFFFFF + 1
        return self.sequence

    @staticmethod
    async def discover(timeout: float = 7.0) -> list[dict[str, str]]:
        found = await BleakScanner.discover(timeout=timeout, return_adv=True,
                                            service_uuids=[SERVICE_UUID])
        return [{"name": advert.local_name or device.name or "NEXUS DECK", "address": device.address}
                for device, advert in found.values()
                if SERVICE_UUID in [u.lower() for u in advert.service_uuids]]

    def configure_profiles(self, profiles: dict[str, dict[str, Any]], active: str) -> None:
        self.saved_profiles, self.active = copy.deepcopy(profiles), active
        self.profile_generation += 1

    async def connect_to(self, address: str = "") -> None:
        if address:
            self.settings.device_address = address
        self.enabled = True
        if self.client:
            await self.client.disconnect()
        self.wake.set()

    async def disconnect(self) -> None:
        self.enabled = False
        self.ready = False
        self.wake.set()
        if self.client:
            await self.client.disconnect()

    async def stop(self) -> None:
        self.running = False
        await self.disconnect()

    async def _find_device(self):
        self.on_state("searching", "Looking for NEXUS DECK")
        devices = await BleakScanner.discover(timeout=7.0, return_adv=True, service_uuids=[SERVICE_UUID])
        candidates = [device for device, advert in devices.values()
                      if SERVICE_UUID in [u.lower() for u in advert.service_uuids]]
        if self.settings.device_address:
            return next((d for d in candidates if d.address.lower() == self.settings.device_address.lower()), None)
        if len(candidates) > 1:
            raise BleakError("Multiple decks found; select one in Connection settings")
        return candidates[0] if candidates else None

    def _notify(self, characteristic, data: bytearray) -> None:
        key = str(characteristic.uuid).lower()
        try:
            assembler = self.assemblers.get(key)
            if assembler is None:
                raise ProtocolError("Unexpected characteristic")
            payload = assembler.feed(bytes(data))
            if payload is None:
                return
            message = decode_json(payload)
            if message.get("session") != self.session:
                raise ProtocolError("Session mismatch")
            seq = sequence(message.get("sequence"))
            if key == STATUS_UUID:
                future = self.pending.get(seq)
                if future and not future.done():
                    future.set_result(message)
            elif key == COMMAND_UUID:
                self.command_queue.put_nowait(message)
        except (ProtocolError, asyncio.QueueFull) as exc:
            log.warning("Rejected BLE notification: %s", exc)

    async def _write(self, characteristic: str, message: dict[str, Any]) -> None:
        async with self.channel_locks[characteristic]:
            client = self.client
            if client is None or not client.is_connected:
                raise BleakError("Deck is disconnected")
            self.message_id = self.message_id % 65535 + 1
            # Bleak/WinRT reports the negotiated ATT MTU. Conservative fallback is 23.
            att = min(512, max(20, int(client.mtu_size) - 3))
            chunks = fragment(encode_json(message), self.message_id, att)
            for chunk in chunks:
                for attempt in range(3):
                    try:
                        async with self.write_lock:
                            if client is not self.client or not client.is_connected:
                                raise BleakError("Deck is disconnected")
                            await asyncio.wait_for(client.write_gatt_char(characteristic, chunk, response=True), 5.0)
                        break
                    except (BleakError, asyncio.TimeoutError):
                        if attempt == 2 or not client.is_connected:
                            raise
                        await asyncio.sleep(0.04 * (attempt + 1))

    async def _request(self, characteristic: str, message: dict[str, Any],
                       expected: str, timeout: float = 12.0) -> dict[str, Any]:
        seq = sequence(message["sequence"])
        future = asyncio.get_running_loop().create_future()
        self.pending[seq] = future
        try:
            for attempt in range(3):
                await self._write(characteristic, message)
                try:
                    reply = await asyncio.wait_for(asyncio.shield(future), timeout)
                    break
                except asyncio.TimeoutError:
                    if attempt == 2:
                        raise BleakError("Deck did not acknowledge the request") from None
            if reply.get("type") != expected or reply.get("status") != "success":
                raise BleakError(reply.get("message", "Deck rejected request"))
            return reply
        finally:
            self.pending.pop(seq, None)
            if not future.done():
                future.cancel()

    async def synchronize(self, profiles: dict[str, dict[str, Any]], active: str) -> None:
        async with self.sync_lock:
            self.configure_profiles(profiles, active)
            generation = self.profile_generation
            if not self.session or not self.client or not self.client.is_connected:
                raise BleakError("Connect the deck before synchronizing")
            snapshot = copy.deepcopy(profiles)
            active = active if active in snapshot else next(iter(snapshot))
            assets = profile_assets(snapshot)
            if assets and not self.icon_assets_supported:
                raise BleakError("Update the deck firmware to use color and animated icons")
            for asset_id, asset in assets.items():
                if asset_id in self.synced_icons:
                    continue
                self.on_state("syncing", "Sending icon artwork…")
                await self._request(PROFILE_UUID, {"type": "icon_sync", "sequence": self._next_sequence(),
                    "session": self.session, "asset": asset_id, "data": asset["data"]}, "icon_ack")
                self.synced_icons.add(asset_id)
            message = {"type": "profile_sync", "schema": 1,
                       "sequence": self._next_sequence(), "session": self.session,
                       "active": active,
                       "profiles": [device_profile(p) for p in snapshot.values()]}
            encode_json(message)
            self.on_state("syncing", "Saving profiles on the deck")
            await self._request(PROFILE_UUID, message, "profile_ack", timeout=30.0)
            # The device purges artwork omitted from a committed layout bundle.
            self.synced_icons.intersection_update(assets)
            if generation == self.profile_generation:
                self.active = active
            # Preserve replay history on live profile edits; begin resets it only for a new connection.
            self.dispatcher.profiles = copy.deepcopy(snapshot)
            self.ready = True
            self.on_state("connected", "Profiles synchronized · encrypted connection")
            log.info("Profiles synchronized: %d profiles, %d artwork assets", len(snapshot), len(assets))

    async def _command_loop(self) -> None:
        while self.client and self.client.is_connected:
            message = await self.command_queue.get()
            try:
                if message.get("type") == "profile_select":
                    if set(message) != {"type", "sequence", "session", "profile"} or message.get("profile") not in self.dispatcher.profiles:
                        raise ProtocolError("Invalid profile selection")
                    self.active = message["profile"]
                    self.settings.active_profile = self.active
                    if self.on_profile:
                        self.on_profile(self.active)
                    continue
                response = await self.dispatcher.handle(message)
                await self._write(RESPONSE_UUID, response)
                self.on_action(str(message.get("id", message.get("type", ""))), response["status"] + ": " + response["message"])
            except (ProtocolError, BleakError) as exc:
                log.warning("Rejected command: %s", exc)
            finally:
                self.command_queue.task_done()

    async def _telemetry_loop(self) -> None:
        last_signature = None
        last_sent = 0.0
        while self.client and self.client.is_connected:
            if self.latest_telemetry and self.ready:
                sample = copy.deepcopy(self.latest_telemetry)
                signature = {k: v for k, v in sample.items() if k not in ("epoch", "utc_offset")}
                if signature != last_signature or time.monotonic() - last_sent >= 5.0:
                    message = {"type": "telemetry", "sequence": self._next_sequence(),
                               "session": self.session, **sample}
                    await self._write(TELEMETRY_UUID, message)
                    last_sent, last_signature = time.monotonic(), signature
            await asyncio.sleep(self.settings.telemetry_interval)

    async def run(self) -> None:
        backoff = 2.0
        while self.running:
            self.wake.clear()
            if not self.enabled:
                self.on_state("disconnected", "Connect from the editor or tray")
                await self.wake.wait()
                continue
            tasks: list[asyncio.Task] = []
            try:
                device = await self._find_device()
                if device is None:
                    raise BleakError("Deck not found; power it on and enable Bluetooth")
                self.on_state("connecting", "Pair with the six-digit passkey shown on the deck")
                self.disconnected.clear()
                loop = asyncio.get_running_loop()
                pairing_options = {}
                if sys.platform == "win32":
                    from .pairing import NexusBleakClientWinRT
                    pairing_options = {"backend": NexusBleakClientWinRT,
                                       "nexus_pin_provider": self.on_pin}
                self.client = BleakClient(device, timeout=75.0, pair=True,
                                          disconnected_callback=lambda _: loop.call_soon_threadsafe(self.disconnected.set),
                                          winrt={"use_cached_services": False}, **pairing_options)
                await self.client.connect()
                for characteristic in (COMMAND_UUID, TELEMETRY_UUID, RESPONSE_UUID, PROFILE_UUID, STATUS_UUID):
                    if self.client.services.get_characteristic(characteristic) is None:
                        raise BleakError("Device is missing the NEXUS GATT service; flash matching firmware")
                status = decode_json(bytes(await self.client.read_gatt_char(STATUS_UUID)))
                if status.get("protocol") != 1 or status.get("name") != "NEXUS DECK":
                    raise BleakError("Incompatible deck firmware")
                self.icon_assets_supported = status.get("icon_assets") is True
                self.synced_icons.clear()
                self.session = uuid.uuid4().hex
                for assembler in self.assemblers.values():
                    assembler.reset()
                while not self.command_queue.empty():
                    self.command_queue.get_nowait()
                    self.command_queue.task_done()
                self.dispatcher.begin(self.session, {})
                await self.client.start_notify(COMMAND_UUID, self._notify)
                await self.client.start_notify(STATUS_UUID, self._notify)
                hello = {"type": "hello", "protocol": 1, "sequence": self._next_sequence(),
                         "session": self.session, "pc_name": self.latest_telemetry.get("pc_name", "Windows PC"),
                         "epoch": int(time.time()), "utc_offset": self.latest_telemetry.get("utc_offset", 0)}
                await self._request(STATUS_UUID, hello, "hello_ack")
                log.info("Encrypted BLE session established with firmware %s", status.get("firmware", "unknown"))
                self.on_address(device.address)
                await self.synchronize(self.saved_profiles, self.active)
                backoff = 2.0
                tasks = [asyncio.create_task(self._command_loop()), asyncio.create_task(self._telemetry_loop()),
                         asyncio.create_task(self.disconnected.wait())]
                done, _ = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
                for task in done:
                    if not task.cancelled() and task.exception():
                        raise task.exception()
            except asyncio.CancelledError:
                break
            except Exception as exc:
                self.on_state("disconnected", str(exc))
                log.info("BLE connection: %s", exc)
            finally:
                self.ready = False
                self.icon_assets_supported = False
                self.synced_icons.clear()
                for task in tasks:
                    task.cancel()
                if tasks:
                    await asyncio.gather(*tasks, return_exceptions=True)
                self.session = ""
                self.dispatcher.begin("", {})
                for future in self.pending.values():
                    if not future.done():
                        future.set_exception(BleakError("Deck disconnected"))
                if self.client:
                    try:
                        await asyncio.wait_for(self.client.disconnect(), 5.0)
                    except Exception:
                        pass
                    self.client = None
            if self.running and self.enabled:
                try:
                    await asyncio.wait_for(self.wake.wait(), backoff + random.uniform(0, 1))
                except asyncio.TimeoutError:
                    pass
                backoff = min(backoff * 1.6, 30.0)
