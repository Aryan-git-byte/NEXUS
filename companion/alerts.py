"""Built-in timer notifications. BLE never selects a file, command or sound path."""
from __future__ import annotations

import logging
import sys
from collections.abc import Callable
from pathlib import Path

from .settings import Settings, resource_directory

log = logging.getLogger(__name__)
TIMER_ALERTS = {
    "countdown": ("Timer complete", "Your NEXUS DECK countdown has finished."),
    "focus": ("Focus complete", "Take a break. Your deck is timing the break now."),
    "break": ("Break complete", "Your next focus session has started on the deck."),
}


def play_chime(path: Path) -> None:
    if sys.platform != "win32":
        raise OSError("Timer audio requires Windows")
    if not path.is_file():
        raise OSError("The installed timer chime is missing")
    import winsound
    winsound.PlaySound(str(path), winsound.SND_FILENAME | winsound.SND_ASYNC | winsound.SND_NODEFAULT)


class TimerAlerts:
    def __init__(self, settings: Settings,
                 notify: Callable[[str, str], None] | None = None,
                 player: Callable[[Path], None] = play_chime):
        self.settings, self.notify, self.player = settings, notify, player
        self.sound = resource_directory() / "assets" / "timer.wav"

    def fire(self, kind: str) -> str:
        if kind not in TIMER_ALERTS:
            raise ValueError("Unknown timer event")
        title, text = TIMER_ALERTS[kind]
        if self.notify:
            self.notify(title, text)
        if not self.settings.timer_sound:
            return f"{title}; PC sound is off"
        try:
            self.player(self.sound)
            return f"{title}; PC chime started"
        except (OSError, RuntimeError, ImportError) as exc:
            log.warning("Timer sound unavailable: %s", exc)
            return f"{title}; PC audio is unavailable"

    def test(self) -> None:
        """A local editor click tests the chime even when completion sounds are off."""
        self.player(self.sound)
        if self.notify:
            self.notify("NEXUS DECK chime test", "This is the sound for completed timers.")
