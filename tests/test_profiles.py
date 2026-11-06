import base64
import copy
import json
from pathlib import Path

import pytest

from companion.profiles import (ProfileManager, device_profile, validate_action,
                                validate_profile, validate_shortcut)
from companion.protocol import MAX_MESSAGE, ProtocolError, Reassembler, decode_json, encode_json, fragment

ROOT = Path(__file__).resolve().parents[1]


def test_all_shipped_profiles_validate_and_match_firmware():
    profiles = [validate_profile(json.loads(p.read_text("utf-8"))) for p in sorted((ROOT / "profiles").glob("*.json"))
                if p.name != "device_defaults.json"]
    embedded = json.loads((ROOT / "profiles/device_defaults.json").read_text("utf-8"))
    assert embedded["profiles"] == [device_profile(p) for p in profiles]
    assert len(profiles) == 6
    for profile in profiles:
        for button in device_profile(profile)["buttons"]:
            assert "action" not in button


@pytest.mark.parametrize("value", [
    {"type": "shell", "target": "evil"}, {"type": "launch_app", "target": "cmd.exe"},
    {"type": "launch_app", "target": "C:/something/script.bat"},
    {"type": "open_url", "url": "file:///C:/secret"},
    {"type": "open_url", "url": "https://user:password@example.com"},
    {"type": "text_snippet", "text": "unapproved"},
    {"type": "show_desktop", "command": "extra"},
    {"type": "keyboard_shortcut", "keys": "ctrl+alt+delete"},
])
def test_unapproved_actions_and_parameters(value):
    # Ctrl+Alt+Delete is secure attention: reject it rather than claiming SendInput can execute it.
    if value.get("keys") == "ctrl+alt+delete":
        with pytest.raises(ValueError):
            validate_shortcut(value["keys"])
    else:
        with pytest.raises(ValueError):
            validate_action(value)


def test_profile_save_load_rename_and_custom_icon(tmp_path):
    manager = ProfileManager(tmp_path)
    profile = copy.deepcopy(manager.profiles["development"])
    profile["name"] = "Work"
    profile["buttons"][0]["icon_data"] = base64.b64encode(bytes([0xAA] * 72)).decode()
    profile["buttons"][0]["long_press"] = {"type": "keyboard_shortcut", "keys": "ctrl+s"}
    saved = manager.save(profile)
    assert saved["revision"] == 2
    reloaded = ProfileManager(tmp_path)
    assert reloaded.profiles["development"]["name"] == "Work"
    bundle = reloaded.bundle("development", session="a" * 32)
    wire = encode_json(bundle)
    rx = Reassembler()
    for packet in fragment(wire, 42, 20):
        result = rx.feed(packet)
    assert decode_json(result) == bundle
    assert bundle["profiles"][1]["id"]
    assert "args" not in wire.decode()


def test_duplicate_ids_and_unknown_fields_rejected(tmp_path):
    manager = ProfileManager(tmp_path)
    profile = manager.snapshot()["default"]
    profile["buttons"][1]["id"] = profile["buttons"][0]["id"]
    with pytest.raises(ValueError):
        validate_profile(profile)


def test_large_approved_local_snippets_reload_without_expanding_ble_limit(tmp_path):
    manager = ProfileManager(tmp_path)
    profile = manager.snapshot()["custom"]
    profile["buttons"] = [{"id": f"snippet_{i}", "label": f"Snippet {i}", "icon": "text",
                           "action": {"type": "text_snippet", "approved": True, "text": "x" * 2048}}
                          for i in range(24)]
    saved = manager.save(profile)
    path = manager.directory / "custom.json"
    assert path.stat().st_size > MAX_MESSAGE
    with pytest.raises(ProtocolError):
        decode_json(path.read_bytes())
    reloaded = ProfileManager(tmp_path)
    assert reloaded.profiles["custom"] == saved
    assert len(encode_json(reloaded.bundle("custom"))) < MAX_MESSAGE
    profile = manager.snapshot()["default"]
    profile["buttons"][0]["command"] = "unexpected"
    with pytest.raises(ValueError):
        validate_profile(profile)
