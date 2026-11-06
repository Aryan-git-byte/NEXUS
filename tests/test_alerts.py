import asyncio
import wave
from pathlib import Path

import pytest

from companion.alerts import TimerAlerts
from companion.bluetooth import CommandDispatcher
from companion.protocol import ProtocolError
from companion.settings import Settings, atomic_json


def test_timer_chime_is_valid_pcm_and_local_only():
    notifications, sounds = [], []
    alerts = TimerAlerts(Settings(),lambda *message: notifications.append(message),sounds.append)
    result = alerts.fire("countdown")
    assert "chime started" in result and notifications[0][0] == "Timer complete"
    assert sounds == [alerts.sound] and alerts.sound == Path(__file__).resolve().parents[1] / "assets/timer.wav"
    with wave.open(str(sounds[0]),"rb") as sound:
        assert sound.getnchannels() == 1 and sound.getsampwidth() == 2
        assert 0.5 < sound.getnframes() / sound.getframerate() < 1.5
    with pytest.raises(ValueError):
        alerts.fire("C:/Windows/System32/cmd.exe")
    assert len(sounds) == 1


def test_silent_and_unavailable_audio_still_notify():
    notifications, sounds = [], []
    settings = Settings(timer_sound=False)
    alerts = TimerAlerts(settings,lambda *message: notifications.append(message),sounds.append)
    assert "sound is off" in alerts.fire("focus") and not sounds
    settings.timer_sound = True
    def unavailable(_):
        raise OSError("No audio device")
    alerts.player = unavailable
    assert "audio is unavailable" in alerts.fire("break") and len(notifications) == 2


def test_timer_settings_migrate_and_reject_non_boolean(tmp_path):
    atomic_json(tmp_path / "settings.json",{"auto_connect":True})
    assert Settings.load(tmp_path).timer_sound is True
    atomic_json(tmp_path / "settings.json",{"timer_sound":1})
    with pytest.raises(ValueError,match="timer_sound"):
        Settings.load(tmp_path)


def test_timer_events_are_session_bound_deduplicated_and_validated():
    class Engine:
        def __init__(self):
            self.calls = []
        def timer_alert(self,kind):
            self.calls.append(kind)
            return "Timer complete; PC chime started"
    async def run():
        engine = Engine()
        dispatcher = CommandDispatcher(engine)
        dispatcher.begin("a" * 32,{})
        message = {"type":"timer_alert","sequence":1,"session":"a" * 32,"kind":"countdown"}
        responses = await asyncio.gather(dispatcher.handle(message),dispatcher.handle(message))
        assert responses[0] == responses[1] and responses[0]["status"] == "success"
        assert engine.calls == ["countdown"]
        invalid = dict(message,sequence=2,target="cmd.exe")
        assert (await dispatcher.handle(invalid))["status"] == "error"
        assert (await dispatcher.handle(dict(message,sequence=3,kind="shell")))["status"] == "error"
        with pytest.raises(ProtocolError):
            await dispatcher.handle(dict(message,sequence=4,session="b" * 32))
        dispatcher.highest_sequence = 100
        assert (await dispatcher.handle(dict(message,sequence=50)))["status"] == "error"
        assert engine.calls == ["countdown"]
    asyncio.run(run())
