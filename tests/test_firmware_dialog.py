"""Drive the real Qt firmware controls without touching the user's device."""
import os
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from unittest.mock import Mock

import pytest
from PySide6.QtWidgets import QApplication

from companion.controller import Controller
from companion.firmware_dialog import FirmwareDialog
from companion.profiles import ProfileManager
from companion.settings import Settings


@pytest.fixture(scope="module")
def app():
    return QApplication.instance() or QApplication([])


def test_validate_upload_progress_cancel_and_reconnect(app, tmp_path, firmware_blob):
    controller = Controller(Settings(auto_connect=False), ProfileManager(tmp_path), tmp_path)
    controller.upload_firmware = Mock()
    controller.cancel_firmware_update = Mock()
    dialog = FirmwareDialog(controller)
    dialog.show()
    image = tmp_path / "nexus_deck.bin"
    image.write_bytes(firmware_blob)
    try:
        dialog.load_image(str(image))
        assert dialog.version.text() == "1.3.0" and not dialog.upload.isEnabled()
        controller.device_changed.emit({"firmware": "1.3.0", "ota": True,
                                        "ota_confirmed": True, "slot": "factory"})
        controller.status_changed.emit("connected", "Encrypted")
        assert dialog.upload.isEnabled()
        dialog.upload.click()
        controller.upload_firmware.assert_called_once_with(dialog.image)
        assert dialog.busy and not dialog.choose.isEnabled() and not dialog.close_button.isEnabled()
        dialog.reject()
        assert dialog.isVisible()
        controller.firmware_progress.emit(50, "Uploading")
        assert dialog.progress.value() == 50 and dialog.cancel.isEnabled()
        dialog.cancel.click()
        controller.cancel_firmware_update.assert_called_once()
        controller.firmware_finished.emit(False, "Upload cancelled")
        assert not dialog.busy and dialog.choose.isEnabled() and dialog.upload.isEnabled()
        dialog.upload.click()
        controller.firmware_progress.emit(97, "Reconnecting")
        controller.status_changed.emit("disconnected", "Restarting")
        assert dialog.message.text() == "Reconnecting" and not dialog.cancel.isEnabled()
        controller.device_changed.emit({"firmware": "1.3.0", "ota": True,
                                        "ota_confirmed": True, "slot": "ota_0"})
        controller.status_changed.emit("connected", "Encrypted")
        controller.firmware_finished.emit(True, "Boot verified")
        assert dialog.progress.value() == 100 and dialog.message.text() == "Boot verified"
    finally:
        dialog.hide()
        dialog.deleteLater()
        app.processEvents()


def test_bad_file_and_legacy_device_cannot_upload(app, tmp_path, firmware_blob):
    controller = Controller(Settings(auto_connect=False), ProfileManager(tmp_path), tmp_path)
    dialog = FirmwareDialog(controller)
    source = tmp_path / "nexus_deck.bin"
    source.write_bytes(firmware_blob)
    try:
        controller.device_changed.emit({"firmware": "1.2.0", "ota": False})
        controller.status_changed.emit("connected", "Encrypted")
        dialog.load_image(str(source))
        assert not dialog.upload.isEnabled() and "USB installation" in dialog.message.text()
        source.write_bytes(b"bad image")
        dialog.load_image(str(source))
        assert dialog.image is None and not dialog.upload.isEnabled()
        assert dialog.version.text() == "—"
    finally:
        dialog.deleteLater()
        app.processEvents()
