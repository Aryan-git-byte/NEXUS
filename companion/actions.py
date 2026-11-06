"""Approved Windows action registry. No BLE text is executed as a command."""
from __future__ import annotations

import ctypes
import logging
import os
import shutil
import subprocess
import sys
import webbrowser
from ctypes import wintypes
from pathlib import Path
from typing import Any

from .profiles import validate_action, validate_shortcut
from .settings import Settings
from .alerts import TimerAlerts

log = logging.getLogger(__name__)
IS_WINDOWS = sys.platform == "win32"
VK = {"ctrl": 0x11, "shift": 0x10, "alt": 0x12, "win": 0x5B,
      "enter": 0x0D, "tab": 0x09, "space": 0x20, "escape": 0x1B,
      "backspace": 0x08, "delete": 0x2E, "insert": 0x2D, "home": 0x24,
      "end": 0x23, "pageup": 0x21, "pagedown": 0x22, "up": 0x26,
      "down": 0x28, "left": 0x25, "right": 0x27, "printscreen": 0x2C,
      "capslock": 0x14, "plus": 0xBB, "minus": 0xBD, "comma": 0xBC,
      "period": 0xBE, "slash": 0xBF, "semicolon": 0xBA}
VK.update({chr(i).lower(): i for i in range(ord("A"), ord("Z") + 1)})
VK.update({str(i): ord(str(i)) for i in range(10)})
VK.update({f"f{i}": 0x6F + i for i in range(1, 25)})
EXTENDED_KEYS = {"win", "insert", "delete", "home", "end", "pageup", "pagedown",
                 "up", "down", "left", "right", "printscreen"}


class ActionError(RuntimeError):
    pass


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", wintypes.WORD), ("wScan", wintypes.WORD),
                ("dwFlags", wintypes.DWORD), ("time", wintypes.DWORD),
                ("dwExtraInfo", ctypes.c_size_t)]


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [("dx", wintypes.LONG), ("dy", wintypes.LONG),
                ("mouseData", wintypes.DWORD), ("dwFlags", wintypes.DWORD),
                ("time", wintypes.DWORD), ("dwExtraInfo", ctypes.c_size_t)]


class HARDWAREINPUT(ctypes.Structure):
    _fields_ = [("uMsg", wintypes.DWORD), ("wParamL", wintypes.WORD), ("wParamH", wintypes.WORD)]


class INPUTUNION(ctypes.Union):
    _fields_ = [("ki", KEYBDINPUT), ("mi", MOUSEINPUT), ("hi", HARDWAREINPUT)]


class INPUT(ctypes.Structure):
    _anonymous_ = ("u",)
    _fields_ = [("type", wintypes.DWORD), ("u", INPUTUNION)]


class WindowsInput:
    def __init__(self) -> None:
        self.user32 = None
        if IS_WINDOWS:
            self.user32 = ctypes.WinDLL("user32", use_last_error=True)
            self.user32.SendInput.argtypes = (wintypes.UINT, ctypes.POINTER(INPUT), ctypes.c_int)
            self.user32.SendInput.restype = wintypes.UINT
            self.user32.LockWorkStation.restype = wintypes.BOOL

    def _send(self, vk: int = 0, scan: int = 0, flags: int = 0) -> None:
        if not self.user32:
            raise ActionError("Keyboard actions require Windows")
        event = INPUT(type=1, ki=KEYBDINPUT(vk, scan, flags, 0, 0))
        if self.user32.SendInput(1, ctypes.byref(event), ctypes.sizeof(INPUT)) != 1:
            raise ActionError("Windows blocked keyboard input; the foreground app may be elevated")

    def shortcut(self, shortcut: str) -> None:
        keys = validate_shortcut(shortcut)
        held: list[tuple[int, int]] = []
        try:
            for key in keys:
                code = VK[key]
                flags = 1 if key in EXTENDED_KEYS else 0
                self._send(code, flags=flags)
                held.append((code, flags))
        finally:
            first_error: Exception | None = None
            for code, flags in reversed(held):
                try:
                    self._send(code, flags=flags | 2)
                except ActionError as exc:
                    first_error = exc
            if first_error:
                raise first_error

    def media(self, vk: int) -> None:
        try:
            self._send(vk)
        finally:
            self._send(vk, flags=2)

    def text(self, value: str) -> None:
        # UTF-16 code units with KEYEVENTF_UNICODE; clipboard contents are untouched.
        encoded = value.encode("utf-16-le")
        for offset in range(0, len(encoded), 2):
            scan = int.from_bytes(encoded[offset:offset + 2], "little")
            self._send(scan=scan, flags=4)
            self._send(scan=scan, flags=6)

    def lock(self) -> None:
        if not self.user32 or not self.user32.LockWorkStation():
            raise ActionError("Windows could not lock this session")


class AudioController:
    """Each call initializes COM on its calling thread; no COM pointer crosses threads."""

    @staticmethod
    def _endpoint():
        from pycaw.pycaw import AudioUtilities, IAudioEndpointVolume
        from comtypes import CLSCTX_ALL
        device = AudioUtilities.GetSpeakers()
        # pycaw's current API exposes EndpointVolume; retain compatibility with older versions.
        if hasattr(device, "EndpointVolume"):
            return device.EndpointVolume
        return ctypes.cast(device.Activate(IAudioEndpointVolume._iid_, CLSCTX_ALL, None),
                           ctypes.POINTER(IAudioEndpointVolume))

    def _call(self, operation):
        if not IS_WINDOWS:
            raise ActionError("Audio control requires Windows")
        import comtypes
        comtypes.CoInitialize()
        try:
            return operation(self._endpoint())
        except Exception as exc:
            raise ActionError(f"Audio device is unavailable: {exc}") from exc
        finally:
            comtypes.CoUninitialize()

    def read(self) -> tuple[int, bool] | None:
        try:
            return self._call(lambda e: (round(e.GetMasterVolumeLevelScalar() * 100), bool(e.GetMute())))
        except (ActionError, ImportError, OSError):
            return None

    def set_volume(self, value: int) -> None:
        if type(value) is not int or not 0 <= value <= 100:
            raise ActionError("Volume must be an integer between 0 and 100")
        self._call(lambda e: e.SetMasterVolumeLevelScalar(value / 100, None))

    def adjust(self, delta: float) -> None:
        self._call(lambda e: e.SetMasterVolumeLevelScalar(
            min(1.0, max(0.0, e.GetMasterVolumeLevelScalar() + delta)), None))

    def mute(self) -> None:
        self._call(lambda e: e.SetMute(not e.GetMute(), None))


class ActionEngine:
    def __init__(self, settings: Settings, input_api: WindowsInput | None = None,
                 audio: AudioController | None = None, timer_callback=None):
        self.settings = settings
        self.input = input_api or WindowsInput()
        self.audio = audio or AudioController()
        self.alerts = TimerAlerts(settings,timer_callback)
        self.registry = {
            "launch_app": self._launch, "keyboard_shortcut": lambda a: self.input.shortcut(a["keys"]),
            "media_play_pause": lambda a: self.input.media(0xB3),
            "media_next": lambda a: self.input.media(0xB0),
            "media_previous": lambda a: self.input.media(0xB1),
            "volume_up": lambda a: self.audio.adjust(0.05),
            "volume_down": lambda a: self.audio.adjust(-0.05),
            "volume_mute": lambda a: self.audio.mute(),
            "screenshot": lambda a: self.input.shortcut("win+shift+s"),
            "open_url": lambda a: self._open_url(a["url"]),
            "lock_windows": lambda a: self.input.lock(),
            "show_desktop": lambda a: self.input.shortcut("win+d"),
            "text_snippet": lambda a: self.input.text(a["text"]),
        }

    def execute(self, action: dict[str, Any]) -> str:
        try:
            validated = validate_action(action)
            self.registry[validated["type"]](validated)
            return "Action completed"
        except ActionError:
            raise
        except Exception as exc:
            raise ActionError(str(exc)) from exc

    def timer_alert(self, kind: str) -> str:
        return self.alerts.fire(kind)

    def _code_path(self) -> str:
        candidates = [Path(os.environ.get("LOCALAPPDATA", "")) / "Programs/Microsoft VS Code/Code.exe",
                      Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "Microsoft VS Code/Code.exe"]
        for path in candidates:
            if path.is_file():
                return str(path)
        for name in ("Code.exe", "code.exe"):
            path = shutil.which(name)
            if path:
                return path
        raise ActionError("VS Code was not found; select Code.exe in the button editor")

    def _launch(self, action: dict[str, Any]) -> None:
        target = action["target"]
        args = list(action.get("args", []))
        cwd = action.get("cwd") or None
        windows_dir = Path(os.environ.get("SystemRoot", "C:/Windows"))
        if target in ("code", "project"):
            executable = self._code_path()
            if target == "project":
                project = self._project()
                args = [str(project), *args]
        elif target == "terminal":
            executable = shutil.which("wt.exe") or ""
            if not executable and os.environ.get("LOCALAPPDATA"):
                # GUI/startup processes can have a PATH without WindowsApps.
                # App execution aliases are valid .exe reparse points, even at 0 bytes.
                alias = Path(os.environ["LOCALAPPDATA"]) / "Microsoft/WindowsApps/wt.exe"
                if alias.is_file():
                    executable = str(alias)
            if not executable:
                raise ActionError("Windows Terminal was not found; install it or select its executable")
        elif target == "powershell":
            executable = str(windows_dir / "System32/WindowsPowerShell/v1.0/powershell.exe")
        elif target == "explorer":
            executable = str(windows_dir / "explorer.exe")
        elif target == "dev_server":
            executable = self.settings.dev_server_executable
            if not executable:
                raise ActionError("Configure a development server executable and arguments in Connection settings")
            cwd = str(self._project())
            args = list(self.settings.dev_server_arguments or []) + args
        else:
            executable = target
        path = Path(executable)
        if not path.is_absolute() or path.suffix.lower() != ".exe" or not path.is_file():
            raise ActionError("Application executable does not exist or is not an absolute .exe path")
        if cwd and not Path(cwd).is_dir():
            raise ActionError("Working directory does not exist")
        subprocess.Popen([str(path), *args], cwd=cwd, shell=False, close_fds=True)

    def _project(self) -> Path:
        path = Path(self.settings.project_directory)
        if not self.settings.project_directory or not path.is_absolute() or not path.is_dir():
            raise ActionError("Choose your project folder in Connection settings")
        return path

    @staticmethod
    def _open_url(url: str) -> None:
        if not webbrowser.open(url, new=2):
            raise ActionError("Windows could not open the default browser")
