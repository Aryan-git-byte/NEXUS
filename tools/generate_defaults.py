"""Generate the repository's shipped profiles and its exact firmware presentation bundle."""
from __future__ import annotations
import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from companion.profiles import device_profile, validate_profile


def button(button_id, label, icon, kind, **fields):
    return {"id": button_id, "label": label, "icon": icon, "action": {"type": kind, **fields}}


def make_profiles():
    b = button
    return [
        {"id": "default", "name": "Default", "category": "default", "revision": 1, "buttons": [
            b("vscode", "VS Code", "code", "launch_app", target="code"),
            b("terminal", "Terminal", "terminal", "launch_app", target="terminal"),
            b("browser", "Browser", "browser", "open_url", url="https://github.com"),
            b("screenshot", "Screenshot", "camera", "screenshot"),
            b("copy", "Copy", "copy", "keyboard_shortcut", keys="ctrl+c"),
            b("paste", "Paste", "paste", "keyboard_shortcut", keys="ctrl+v"),
            b("desktop", "Desktop", "desktop", "show_desktop"),
            b("lock", "Lock", "lock", "lock_windows"),
        ]},
        {"id": "development", "name": "Development", "category": "development", "revision": 1, "buttons": [
            b("vscode", "VS Code", "code", "launch_app", target="code"),
            b("terminal", "Terminal", "terminal", "launch_app", target="terminal"),
            b("powershell", "PowerShell", "powershell", "launch_app", target="powershell"),
            b("explorer", "Explorer", "folder", "launch_app", target="explorer"),
            b("github", "GitHub", "github", "open_url", url="https://github.com"),
            b("project", "Open project", "project", "launch_app", target="project"),
            b("dev_server", "Dev server", "server", "launch_app", target="dev_server"),
            b("screenshot", "Screenshot", "camera", "screenshot"),
        ]},
        {"id": "media", "name": "Media", "category": "media", "revision": 1, "buttons": [
            b("play", "Play / pause", "play", "media_play_pause"),
            b("previous", "Previous", "previous", "media_previous"),
            b("next", "Next", "next", "media_next"),
            b("volume_down", "Volume down", "volume_down", "volume_down"),
            b("volume_up", "Volume up", "volume_up", "volume_up"),
            b("mute", "Mute", "mute", "volume_mute"),
        ]},
        {"id": "gaming", "name": "Gaming", "category": "gaming", "revision": 1, "buttons": [
            b("screenshot", "Screenshot", "camera", "screenshot"),
            b("play", "Play / pause", "play", "media_play_pause"),
            b("mute", "Mute", "mute", "volume_mute"),
            b("volume_up", "Volume up", "volume_up", "volume_up"),
            b("volume_down", "Volume down", "volume_down", "volume_down"),
            b("desktop", "Desktop", "desktop", "show_desktop"),
        ]},
        {"id": "productivity", "name": "Productivity", "category": "productivity", "revision": 1, "buttons": [
            b("copy", "Copy", "copy", "keyboard_shortcut", keys="ctrl+c"),
            b("paste", "Paste", "paste", "keyboard_shortcut", keys="ctrl+v"),
            b("cut", "Cut", "cut", "keyboard_shortcut", keys="ctrl+x"),
            b("undo", "Undo", "undo", "keyboard_shortcut", keys="ctrl+z"),
            b("save", "Save", "save", "keyboard_shortcut", keys="ctrl+s"),
            b("explorer", "Files", "folder", "launch_app", target="explorer"),
            b("desktop", "Desktop", "desktop", "show_desktop"),
            b("lock", "Lock", "lock", "lock_windows"),
        ]},
        {"id": "custom", "name": "Custom", "category": "custom", "revision": 1, "buttons": [
            b("select_all", "Select all", "text", "keyboard_shortcut", keys="ctrl+a"),
            b("copy", "Copy", "copy", "keyboard_shortcut", keys="ctrl+c"),
            b("paste", "Paste", "paste", "keyboard_shortcut", keys="ctrl+v"),
            b("save", "Save", "save", "keyboard_shortcut", keys="ctrl+s"),
        ]},
    ]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--create-shipped", action="store_true", help="Regenerate the six repository defaults")
    args = parser.parse_args()
    directory = ROOT / "profiles"
    directory.mkdir(exist_ok=True)
    if args.create_shipped:
        for profile in make_profiles():
            value = validate_profile(profile)
            (directory / f"{value['id']}.json").write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    profiles = [validate_profile(json.loads(path.read_text("utf-8")))
                for path in sorted(directory.glob("*.json")) if path.name != "device_defaults.json"]
    if not profiles:
        raise SystemExit("No profiles found; run with --create-shipped")
    bundle = {"schema": 1, "active": "default", "profiles": [device_profile(p) for p in profiles]}
    text = json.dumps(bundle, separators=(",", ":"), ensure_ascii=False)
    if len(text.encode("utf-8")) > 12000:
        raise SystemExit("The firmware presentation bundle exceeds the protocol limit")
    (directory / "device_defaults.json").write_text(text + "\n", encoding="utf-8")
    print(f"Generated {len(profiles)} profiles; firmware bundle: {len(text.encode('utf-8'))} bytes")


if __name__ == "__main__":
    main()
