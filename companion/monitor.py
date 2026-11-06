"""Real PC telemetry. Unsupported sensors are represented by None."""
from __future__ import annotations

import asyncio
import ctypes
import logging
import math
import os
import platform
import sys
import subprocess
import time
from ctypes import wintypes
from typing import Any

import psutil

from .actions import AudioController
from .settings import Settings

log = logging.getLogger(__name__)


class PdhGpu:
    """Windows GPU Engine counters for WDDM drivers, including AMD and Intel."""

    def __init__(self) -> None:
        self.pdh = self.query = self.counter = None
        if sys.platform != "win32":
            return
        try:
            self.pdh = ctypes.WinDLL("pdh")
            handle = ctypes.c_void_p
            self.pdh.PdhOpenQueryW.argtypes = (wintypes.LPCWSTR, ctypes.c_size_t, ctypes.POINTER(handle))
            self.pdh.PdhAddEnglishCounterW.argtypes = (handle, wintypes.LPCWSTR, ctypes.c_size_t,
                                                       ctypes.POINTER(handle))
            self.pdh.PdhCollectQueryData.argtypes = (handle,)
            self.pdh.PdhCloseQuery.argtypes = (handle,)
            self.pdh.PdhGetFormattedCounterArrayW.argtypes = (
                handle, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD),
                ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p)
            self.query, self.counter = handle(), handle()
            if self.pdh.PdhOpenQueryW(None, 0, ctypes.byref(self.query)) != 0:
                self.close()
                return
            if self.pdh.PdhAddEnglishCounterW(self.query, r"\GPU Engine(*)\Utilization Percentage",
                                             0, ctypes.byref(self.counter)) != 0:
                self.close()
                return
            self.pdh.PdhCollectQueryData(self.query)
        except (OSError, AttributeError):
            self.close()

    def sample(self) -> float | None:
        if not self.query or not self.pdh:
            return None
        if self.pdh.PdhCollectQueryData(self.query) != 0:
            return None
        size, count = wintypes.DWORD(), wintypes.DWORD()
        self.pdh.PdhGetFormattedCounterArrayW(self.counter, 0x200, ctypes.byref(size),
                                             ctypes.byref(count), None)
        if not 0 < size.value <= 4 * 1024 * 1024:
            return None
        buffer = ctypes.create_string_buffer(size.value)
        if self.pdh.PdhGetFormattedCounterArrayW(self.counter, 0x200, ctypes.byref(size),
                                                ctypes.byref(count), buffer) != 0:
            return None

        class ValueUnion(ctypes.Union):
            _fields_ = [("doubleValue", ctypes.c_double), ("largeValue", ctypes.c_longlong)]

        class Value(ctypes.Structure):
            _anonymous_ = ("u",)
            _fields_ = [("status", wintypes.DWORD), ("u", ValueUnion)]

        class Item(ctypes.Structure):
            _fields_ = [("name", wintypes.LPWSTR), ("value", Value)]

        items = ctypes.cast(buffer, ctypes.POINTER(Item))
        engines: dict[str, float] = {}
        for index in range(count.value):
            item = items[index]
            if item.value.status not in (0, 1) or not item.name:
                continue
            # Sum processes sharing an engine, then report the busiest engine.
            engine = item.name.split("_luid_", 1)[-1]
            if not math.isfinite(item.value.doubleValue):
                continue
            engines[engine] = engines.get(engine, 0.0) + max(0.0, item.value.doubleValue)
        return round(min(100.0, max(engines.values())), 1) if engines else None

    def close(self) -> None:
        if self.query and self.pdh:
            self.pdh.PdhCloseQuery(self.query)
        self.query = self.counter = None


class GpuMonitor:
    def __init__(self) -> None:
        self.nvml = self.handle = None
        try:
            import pynvml
            pynvml.nvmlInit()
            if pynvml.nvmlDeviceGetCount() > 0:
                self.nvml = pynvml
                self.handle = pynvml.nvmlDeviceGetHandleByIndex(0)
            else:
                pynvml.nvmlShutdown()
        except Exception as exc:
            log.debug("NVIDIA telemetry unavailable: %s", exc)
        self.pdh = PdhGpu() if self.handle is None else None

    def sample(self) -> tuple[float | None, float | None]:
        usage = temperature = None
        if self.nvml and self.handle is not None:
            try:
                usage = float(self.nvml.nvmlDeviceGetUtilizationRates(self.handle).gpu)
            except self.nvml.NVMLError:
                pass
            try:
                temperature = float(self.nvml.nvmlDeviceGetTemperature(
                    self.handle, self.nvml.NVML_TEMPERATURE_GPU))
            except self.nvml.NVMLError:
                pass
        elif self.pdh:
            usage = self.pdh.sample()
        return usage, temperature

    def close(self) -> None:
        if self.pdh:
            self.pdh.close()
        if self.nvml:
            try:
                self.nvml.nvmlShutdown()
            except self.nvml.NVMLError:
                pass
            self.nvml = None


class MonitorEngine:
    def __init__(self, settings: Settings, audio: AudioController | None = None):
        self.settings = settings
        self.audio = audio or AudioController()
        self.gpu = GpuMonitor()
        self.last_net = psutil.net_io_counters()
        self.last_time = time.monotonic()
        psutil.cpu_percent(interval=None)

    def sample(self) -> dict[str, Any]:
        now = time.monotonic()
        net = psutil.net_io_counters()
        elapsed = max(now - self.last_time, 0.001)
        upload = max(0, net.bytes_sent - self.last_net.bytes_sent) / elapsed / 1024
        download = max(0, net.bytes_recv - self.last_net.bytes_recv) / elapsed / 1024
        self.last_time, self.last_net = now, net
        disk = battery = plugged = cpu_temp = None
        try:
            disk = psutil.disk_usage(self.settings.disk_path if sys.platform == "win32" else "/").percent
        except (OSError, ValueError):
            pass
        try:
            power = psutil.sensors_battery()
            if power is not None:
                battery, plugged = round(power.percent), bool(power.power_plugged)
        except (AttributeError, OSError):
            pass
        try:
            sensors = psutil.sensors_temperatures()
            for name in ("coretemp", "k10temp", "cpu_thermal", "zenpower"):
                values = [s.current for s in sensors.get(name, []) if 0 <= s.current <= 150]
                if values:
                    cpu_temp = round(sum(values) / len(values), 1)
                    break
        except (AttributeError, OSError, NotImplementedError):
            pass
        gpu_usage, gpu_temp = self.gpu.sample()
        audio = self.audio.read()
        local = time.localtime()
        offset = getattr(local, "tm_gmtoff", -time.altzone if local.tm_isdst > 0 else -time.timezone)
        return {"cpu": round(psutil.cpu_percent(interval=None), 1),
                "ram": round(psutil.virtual_memory().percent, 1), "disk": disk,
                "gpu": gpu_usage, "gpu_temp": gpu_temp, "cpu_temp": cpu_temp,
                "battery": battery, "plugged": plugged,
                "upload": round(upload, 1), "download": round(download, 1),
                "volume": audio[0] if audio else None, "muted": audio[1] if audio else None,
                "epoch": int(time.time()), "utc_offset": offset,
                "pc_name": platform.node().encode("utf-8")[:63].decode("utf-8", "ignore")}

    def close(self) -> None:
        self.gpu.close()


class MediaMonitor:
    def __init__(self) -> None:
        self.manager = None
        self.available = sys.platform == "win32"
        self.retry_at = 0.0

    async def sample(self) -> dict[str, Any] | None:
        if not self.available or time.monotonic() < self.retry_at:
            return None
        try:
            if self.manager is None:
                from winrt.windows.media.control import GlobalSystemMediaTransportControlsSessionManager
                self.manager = await asyncio.wait_for(
                    GlobalSystemMediaTransportControlsSessionManager.request_async(), 3.0)
            session = self.manager.get_current_session()
            if session is None:
                return None
            props = await asyncio.wait_for(session.try_get_media_properties_async(), 2.0)
            status = session.get_playback_info().playback_status
            return {"title": props.title.encode("utf-8")[:127].decode("utf-8", "ignore"),
                    "artist": props.artist.encode("utf-8")[:63].decode("utf-8", "ignore"),
                    "playing": int(status) == 4}
        except ImportError:
            self.available = False
            return None
        except Exception as exc:
            log.debug("Windows media session unavailable: %s", exc)
            self.manager = None
            self.retry_at = time.monotonic() + 15
            return None


class OptionalTemperatureMonitor:
    """Read installed Libre/Open Hardware Monitor providers, with a hard process timeout."""
    SCRIPT = r"""
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$nexusReadings = @()
foreach ($nexusNamespace in @('root\LibreHardwareMonitor','root\OpenHardwareMonitor')) {
    try {
        $nexusReadings = @(Get-CimInstance -Namespace $nexusNamespace -ClassName Sensor -Filter "SensorType = 'Temperature'" -OperationTimeoutSec 2 -ErrorAction Stop |
            Where-Object { $_.Parent -match 'cpu|gpu' } | Select-Object Parent,Value)
        if ($nexusReadings.Count -gt 0) { break }
    } catch { }
}
ConvertTo-Json -InputObject @($nexusReadings) -Compress
"""

    def sample(self) -> tuple[float | None, float | None]:
        if sys.platform != "win32":
            return None, None
        import json
        from pathlib import Path
        executable = Path(os.environ.get("SystemRoot", "C:/Windows")) / "System32/WindowsPowerShell/v1.0/powershell.exe"
        try:
            completed = subprocess.run([str(executable), "-NoProfile", "-NonInteractive", "-Command", self.SCRIPT],
                                       capture_output=True, text=True, encoding="utf-8", timeout=4,
                                       creationflags=subprocess.CREATE_NO_WINDOW, shell=False)
            if completed.returncode != 0 or len(completed.stdout) > 16384:
                return None, None
            values = json.loads(completed.stdout.strip() or "[]")
            if not isinstance(values, list):
                return None, None
            cpu, gpu = [], []
            for item in values[:128]:
                if not isinstance(item, dict):
                    continue
                value, parent = item.get("Value"), str(item.get("Parent", "")).lower()
                if type(value) not in (float, int) or not math.isfinite(value) or not 0 <= value <= 150:
                    continue
                if "cpu" in parent:
                    cpu.append(float(value))
                elif "gpu" in parent:
                    gpu.append(float(value))
            return max(cpu) if cpu else None, max(gpu) if gpu else None
        except (OSError, ValueError, subprocess.TimeoutExpired):
            return None, None
