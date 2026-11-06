import time
import subprocess
from types import SimpleNamespace
from unittest.mock import Mock

from companion.monitor import MonitorEngine, OptionalTemperatureMonitor
from companion.settings import Settings


def test_real_monitor_reports_bounded_values():
    monitor = MonitorEngine(Settings())
    try:
        time.sleep(0.05)
        sample = monitor.sample()
        assert 0 <= sample["cpu"] <= 100
        assert 0 <= sample["ram"] <= 100
        assert sample["upload"] >= 0 and sample["download"] >= 0
        assert sample["epoch"] > 1700000000
        assert sample["pc_name"]
        for metric in ("gpu_temp", "cpu_temp", "battery"):
            assert sample[metric] is None or sample[metric] >= 0
    finally:
        monitor.close()


def test_optional_windows_sensor_provider_is_bounded(monkeypatch):
    monkeypatch.setattr("companion.monitor.sys.platform", "win32")
    result = SimpleNamespace(returncode=0, stdout='[{"Parent":"/intelcpu/0","Value":57.5},'
                                                  '{"Parent":"/gpu/0","Value":48},'
                                                  '{"Parent":"/cpu/0","Value":999},null]')
    run = Mock(return_value=result)
    monkeypatch.setattr("companion.monitor.subprocess.run", run)
    assert OptionalTemperatureMonitor().sample() == (57.5, 48.0)
    assert run.call_args.kwargs["timeout"] == 4
    assert run.call_args.kwargs["shell"] is False


def test_optional_sensor_timeout_returns_unavailable(monkeypatch):
    monkeypatch.setattr("companion.monitor.sys.platform", "win32")
    monkeypatch.setattr("companion.monitor.subprocess.run", Mock(side_effect=subprocess.TimeoutExpired("provider", 4)))
    assert OptionalTemperatureMonitor().sample() == (None, None)
