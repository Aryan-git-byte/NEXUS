import asyncio
import copy
import json
from pathlib import Path
from unittest.mock import Mock

import pytest

from companion.actions import ActionEngine
from companion.bluetooth import CommandDispatcher
from companion.profiles import ProfileManager
from companion.protocol import ProtocolError
from companion.settings import Settings


def test_duplicate_notification_executes_once(tmp_path):
    profiles = ProfileManager(tmp_path)
    engine = Mock()
    dispatcher = CommandDispatcher(engine)
    dispatcher.begin("a" * 32, profiles.snapshot())
    message = {"type": "action", "id": "copy", "profile": "default", "gesture": "press",
               "sequence": 12, "session": "a" * 32}
    async def scenario():
        first, retry = await asyncio.gather(dispatcher.handle(message), dispatcher.handle(message))
        assert first == retry
        assert first["status"] == "success"
    asyncio.run(scenario())
    engine.execute.assert_called_once_with({"type": "keyboard_shortcut", "keys": "ctrl+c"})


def test_untrusted_parameters_and_stale_sessions(tmp_path):
    engine = Mock()
    dispatcher = CommandDispatcher(engine)
    dispatcher.begin("a" * 32, ProfileManager(tmp_path).snapshot())
    async def scenario():
        bad = {"type": "action", "id": "vscode", "profile": "default", "gesture": "press",
               "sequence": 1, "session": "a" * 32, "target": "C:/evil.exe"}
        assert (await dispatcher.handle(bad))["status"] == "error"
        with pytest.raises(ProtocolError):
            await dispatcher.handle({**bad, "sequence": 2, "session": "b" * 32})
    asyncio.run(scenario())
    engine.execute.assert_not_called()


def test_unknown_button_and_volume_bounds(tmp_path):
    engine = Mock()
    dispatcher = CommandDispatcher(engine)
    dispatcher.begin("a" * 32, ProfileManager(tmp_path).snapshot())
    async def scenario():
        message = {"type": "action", "id": "does_not_exist", "profile": "default", "gesture": "press",
                   "sequence": 1, "session": "a" * 32}
        assert (await dispatcher.handle(message))["status"] == "error"
        await asyncio.sleep(0.05)
        assert (await dispatcher.handle({"type": "action", "id": "system_volume_set", "value": 101,
                                        "sequence": 2, "session": "a" * 32}))["status"] == "error"
    asyncio.run(scenario())
    engine.execute.assert_not_called()
    engine.audio.set_volume.assert_not_called()


def test_local_action_registry_with_injected_windows_apis():
    keyboard, audio = Mock(), Mock()
    engine = ActionEngine(Settings(), keyboard, audio)
    engine.execute({"type": "keyboard_shortcut", "keys": "ctrl+s"})
    engine.execute({"type": "media_play_pause"})
    engine.execute({"type": "volume_down"})
    keyboard.shortcut.assert_called_once_with("ctrl+s")
    keyboard.media.assert_called_once_with(0xB3)
    audio.adjust.assert_called_once_with(-0.05)
