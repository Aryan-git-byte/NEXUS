"""Validate Windows input construction without sending keys to the user's desktop."""
from unittest.mock import Mock

import pytest

from companion.actions import ActionEngine, ActionError, WindowsInput
from companion.settings import Settings

# testing the navigation
def test_navigation_and_windows_keys_include_extended_flags():
    keyboard = WindowsInput()
    keyboard._send = Mock()
    keyboard.shortcut("ctrl+left")
    assert keyboard._send.call_args_list == [
        ((0x11,), {"flags": 0}), ((0x25,), {"flags": 1}),
        ((0x25,), {"flags": 3}), ((0x11,), {"flags": 2})]
    keyboard._send.reset_mock()
    keyboard.shortcut("win+d")
    assert keyboard._send.call_args_list[0].kwargs == {"flags": 1}
    assert keyboard._send.call_args_list[-1].kwargs == {"flags": 3}


def test_modifier_released_after_key_failure():
    keyboard = WindowsInput()
    keyboard._send = Mock(side_effect=[None, ActionError("Input blocked"), None])
    with pytest.raises(ActionError):
        keyboard.shortcut("ctrl+s")
    assert keyboard._send.call_args_list[-1] == ((0x11,), {"flags": 2})


def test_unicode_text_uses_utf16_code_units():
    keyboard = WindowsInput()
    keyboard._send = Mock()
    keyboard.text("A\U0001f680")
    scans = [call.kwargs["scan"] for call in keyboard._send.call_args_list[::2]]
    assert scans == [0x41, 0xD83D, 0xDE80]
    assert all(call.kwargs["flags"] == 6 for call in keyboard._send.call_args_list[1::2])


def test_terminal_launch_uses_windows_alias_when_missing_from_path(tmp_path, monkeypatch):
    alias = tmp_path / "Microsoft/WindowsApps/wt.exe"
    alias.parent.mkdir(parents=True)
    # A real Windows app execution alias also reports a zero-byte file.
    alias.touch()
    monkeypatch.setenv("LOCALAPPDATA", str(tmp_path))
    monkeypatch.setattr("companion.actions.shutil.which", lambda name: None)
    launch = Mock()
    monkeypatch.setattr("companion.actions.subprocess.Popen", launch)
    engine = ActionEngine(Settings(), input_api=Mock(), audio=Mock())
    assert engine.execute({"type": "launch_app", "target": "terminal", "args": []}) == "Action completed"
    launch.assert_called_once_with([str(alias)], cwd=None, shell=False, close_fds=True)


def test_terminal_missing_from_path_and_alias_is_rejected(tmp_path, monkeypatch):
    monkeypatch.setenv("LOCALAPPDATA", str(tmp_path))
    monkeypatch.setattr("companion.actions.shutil.which", lambda name: None)
    launch = Mock()
    monkeypatch.setattr("companion.actions.subprocess.Popen", launch)
    engine = ActionEngine(Settings(), input_api=Mock(), audio=Mock())
    with pytest.raises(ActionError, match="Windows Terminal was not found"):
        engine.execute({"type": "launch_app", "target": "terminal", "args": []})
    launch.assert_not_called()
