"""Render the actual Qt editor offscreen, with real local PC metrics, for visual QA."""
from __future__ import annotations
import argparse
import os
import sys
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from PySide6.QtTest import QTest
from PySide6.QtGui import QFontDatabase
from PySide6.QtWidgets import QApplication
from companion.config_editor import ConfigEditor
from companion.controller import Controller
from companion.monitor import MonitorEngine
from companion.profiles import ProfileManager
from companion.settings import Settings
from companion.theme import configure_theme


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", default="test-results/editor-preview")
    args = parser.parse_args()
    directory = Path(args.output_dir).resolve()
    directory.mkdir(parents=True, exist_ok=True)
    app = QApplication([])
    configure_theme(app)
    # Qt's offscreen platform on Windows does not enumerate system fonts.
    font_directory = Path(os.environ.get("SystemRoot", "C:/Windows")) / "Fonts"
    for filename in ("segoeui.ttf", "segoeuib.ttf", "seguisym.ttf"):
        path = font_directory / filename
        if path.is_file():
            QFontDatabase.addApplicationFont(str(path))
    settings = Settings(auto_connect=False)
    profiles = ProfileManager(directory / "data")
    controller = Controller(settings, profiles, directory / "data")
    editor = ConfigEditor(profiles, controller, no_tray=True)
    editor.show()
    monitor = MonitorEngine(settings)
    try:
        QTest.qWait(200)
        editor._telemetry(monitor.sample())
        for profile, orientation in (("default", 0), ("development", 0), ("media", 0), ("development", 1)):
            editor._reload_profiles(profile)
            editor.orientation.setCurrentIndex(orientation)
            QTest.qWait(80)
            path = directory / f"{profile}-{'portrait' if orientation else 'landscape'}.png"
            if not editor.grab().save(str(path)):
                raise RuntimeError(f"Could not save {path}")
            print(path)
    finally:
        monitor.close()
        editor.hide()
        editor.deleteLater()
        app.processEvents()


if __name__ == "__main__":
    main()
