"""NEXUS DECK entry point; run `python -m companion.main` from the repository root."""
from __future__ import annotations

import argparse
import json
import logging
import sys
from logging.handlers import RotatingFileHandler
from pathlib import Path

# PyInstaller and `python companion/main.py` also resolve package imports correctly.
if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from companion.profiles import ProfileManager
from companion.settings import Settings, atomic_json, data_directory
from companion import __version__


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="NEXUS DECK Windows companion")
    parser.add_argument("--version", action="version", version=__version__)
    parser.add_argument("--data-dir", help="Store local settings and profiles in this folder")
    parser.add_argument("--background", action="store_true", help="Start in the Windows notification area")
    parser.add_argument("--no-bluetooth", action="store_true", help="Pause automatic BLE connections for this run")
    parser.add_argument("--no-tray", action="store_true", help="Exit when the editor closes")
    parser.add_argument("--diagnose", action="store_true", help="Print actual local telemetry and configuration diagnostics")
    parser.add_argument("--smoke-test", action="store_true", help="Open the real editor briefly, then exit without executing actions")
    parser.add_argument("--quit", action="store_true", help="Ask the running companion to save its draft and exit")
    args = parser.parse_args(argv)
    directory = data_directory(args.data_dir)
    directory.mkdir(parents=True, exist_ok=True)
    log = RotatingFileHandler(directory / "companion.log", maxBytes=1_000_000, backupCount=3, encoding="utf-8")
    logging.basicConfig(level=logging.INFO, handlers=[log], format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    try:
        settings = Settings.load(directory)
        profiles = ProfileManager(directory)
    except (OSError, ValueError) as exc:
        logging.error("Configuration failed: %s", exc)
        if args.diagnose or args.smoke_test:
            print(f"Configuration error: {exc}", file=sys.stderr)
        else:
            from PySide6.QtWidgets import QApplication, QMessageBox
            app = QApplication(sys.argv)
            QMessageBox.critical(None, "NEXUS DECK", f"Configuration could not be loaded:\n{exc}\n\nSee {directory}")
        return 2
    if args.no_bluetooth or args.smoke_test:
        settings.auto_connect = False
    if args.diagnose:
        from companion.monitor import MonitorEngine
        monitor = MonitorEngine(settings)
        try:
            import time
            time.sleep(0.2)
            result = {"version": __version__, "profiles": len(profiles.profiles), "profile_errors": profiles.errors,
                      "data_directory": str(directory), "telemetry": monitor.sample()}
            atomic_json(directory / "diagnostic.json", result)
            if sys.stdout is not None:
                print(json.dumps(result, indent=2, ensure_ascii=False))
        finally:
            monitor.close()
        return 0
    from PySide6.QtCore import QTimer
    from PySide6.QtNetwork import QLocalServer, QLocalSocket
    from PySide6.QtWidgets import QApplication, QSystemTrayIcon
    from companion.config_editor import ConfigEditor
    from companion.controller import Controller
    from companion.tray import Tray, app_icon
    from companion.theme import configure_theme
    app = QApplication(sys.argv)
    configure_theme(app)
    app.setApplicationName("NEXUS DECK")
    app.setOrganizationName("NexusDeck")
    app.setWindowIcon(app_icon())
    app.setQuitOnLastWindowClosed(False)
    ipc = None
    if not args.smoke_test:
        socket = QLocalSocket()
        socket.connectToServer("NexusDeck.Companion.v1")
        if socket.waitForConnected(200):
            socket.write(b"quit" if args.quit else b"show")
            socket.waitForBytesWritten(500)
            if args.quit:
                if not socket.waitForReadyRead(2000):
                    return 4
                return 0 if bytes(socket.readAll()) == b"accepted" else 3
            return 0
        if args.quit:
            return 0
        ipc = QLocalServer()
        QLocalServer.removeServer("NexusDeck.Companion.v1")
        if not ipc.listen("NexusDeck.Companion.v1"):
            logging.warning("Single-instance IPC could not be started")
    controller = Controller(settings, profiles, directory)
    no_tray = args.no_tray or args.smoke_test or not QSystemTrayIcon.isSystemTrayAvailable()
    editor = ConfigEditor(profiles, controller, no_tray)
    tray = None if no_tray else Tray(editor, controller)
    if ipc:
        def handle_instance_request():
            socket = ipc.nextPendingConnection()
            if socket:
                socket.waitForReadyRead(100)
                request = bytes(socket.readAll())
                if request == b"show":
                    editor.showNormal()
                    editor.raise_()
                    editor.activateWindow()
                elif request == b"quit":
                    saved = not editor.dirty or editor.save_profile()
                    socket.write(b"accepted" if saved else b"cancelled")
                    socket.waitForBytesWritten(500)
                    if saved:
                        QTimer.singleShot(0, app.quit)
                socket.disconnectFromServer()
                socket.deleteLater()
        ipc.newConnection.connect(handle_instance_request)
    app.aboutToQuit.connect(controller.shutdown)
    if tray:
        app.aboutToQuit.connect(tray.icon.hide)
    if not args.background or no_tray:
        editor.show()
    QTimer.singleShot(0, controller.start)
    logging.info("NEXUS DECK companion %s started%s", __version__, " in background" if args.background else "")
    if profiles.errors:
        editor.footer.setText("Some profile files were rejected: " + "; ".join(profiles.errors))
    if args.smoke_test:
        smoke_telemetry = []
        controller.telemetry_changed.connect(lambda sample: smoke_telemetry.append(sample))
        def finish_smoke():
            report = {"editor_started": editor.isVisible(), "profiles": editor.profile_list.count(),
                      "visible_buttons": editor.grid.count(), "telemetry_samples": len(smoke_telemetry),
                      "services_running": bool(controller.thread and controller.thread.is_alive()),
                      "version": __version__}
            okay = report["editor_started"] and report["services_running"] and report["telemetry_samples"] > 0
            report["success"] = okay
            atomic_json(directory / "smoke-test.json", report)
            if sys.stdout is not None:
                print(f"Editor startup {'OK' if okay else 'failed'}; {report['profiles']} profiles; "
                      f"{report['visible_buttons']} visible buttons; {report['telemetry_samples']} real telemetry samples")
            app.exit(0 if okay else 3)
        QTimer.singleShot(3000, finish_smoke)
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
