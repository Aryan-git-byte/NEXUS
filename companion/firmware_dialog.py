"""Validated firmware selection and an asynchronous Bluetooth update UI."""
from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (QDialog, QFileDialog, QFormLayout, QHBoxLayout,
                               QLabel, QProgressBar, QPushButton, QVBoxLayout)

from .firmware import FirmwareImage


class FirmwareDialog(QDialog):
    def __init__(self, controller, parent=None):
        super().__init__(parent)
        self.controller = controller
        self.image: FirmwareImage | None = None
        self.busy = False
        self.setWindowTitle("NEXUS DECK · Firmware")
        self.setMinimumWidth(560)
        self.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(24, 22, 24, 22)
        layout.setSpacing(16)
        title = QLabel("Update your deck")
        title.setObjectName("sectionTitle")
        layout.addWidget(title)
        note = QLabel("Send firmware over your paired Bluetooth connection. Your layouts, artwork and touch calibration stay saved.")
        note.setWordWrap(True)
        note.setObjectName("secondary")
        layout.addWidget(note)
        self.device = QLabel()
        self.device.setWordWrap(True)
        layout.addWidget(self.device)
        file_row = QHBoxLayout()
        self.file_name = QLabel("Select nexus_deck.bin from your firmware build")
        self.file_name.setWordWrap(True)
        file_row.addWidget(self.file_name, 1)
        self.choose = QPushButton("Choose firmware…")
        self.choose.clicked.connect(self._choose)
        file_row.addWidget(self.choose)
        layout.addLayout(file_row)
        details = QFormLayout()
        self.version = QLabel("—")
        self.size = QLabel("—")
        self.digest = QLabel("—")
        self.digest.setWordWrap(True)
        self.digest.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        details.addRow("New version", self.version)
        details.addRow("Image size", self.size)
        details.addRow("SHA-256", self.digest)
        layout.addLayout(details)
        self.progress = QProgressBar()
        self.progress.setRange(0, 100)
        self.progress.setValue(0)
        layout.addWidget(self.progress)
        self.message = QLabel()
        self.message.setWordWrap(True)
        self.message.setMinimumHeight(44)
        layout.addWidget(self.message)
        buttons = QHBoxLayout()
        self.cancel = QPushButton("Cancel upload")
        self.cancel.clicked.connect(self._cancel)
        self.cancel.hide()
        buttons.addWidget(self.cancel)
        buttons.addStretch()
        self.close_button = QPushButton("Close")
        self.close_button.clicked.connect(self.reject)
        buttons.addWidget(self.close_button)
        self.upload = QPushButton("Upload over Bluetooth")
        self.upload.setObjectName("primary")
        self.upload.clicked.connect(self._upload)
        buttons.addWidget(self.upload)
        layout.addLayout(buttons)
        controller.device_changed.connect(self._refresh)
        controller.status_changed.connect(self._refresh)
        controller.firmware_progress.connect(self._progress)
        controller.firmware_finished.connect(self._finished)
        self._refresh()

    def _refresh(self, *_args) -> None:
        info = self.controller.device_info
        connected = self.controller.connection_state == "connected"
        self.device.setText(f"Deck firmware {info.get('firmware', 'unknown')} · {info.get('slot', 'unknown slot')}"
                            if info else "Deck disconnected")
        available = connected and info.get("ota") is True and info.get("ota_confirmed") is True
        self.upload.setEnabled(bool(self.image and available and not self.busy))
        if self.busy:
            return
        if not connected:
            self.message.setText("Connect your deck to start a wireless update.")
        elif info.get("ota") is not True:
            self.message.setText("This deck needs one USB installation of OTA-enabled firmware (1.3.0 or newer). Future uploads use this window.")
        elif info.get("ota_confirmed") is not True:
            self.message.setText("The deck is checking its new firmware. Please wait for boot validation.")
        elif self.image:
            self.message.setText("Ready to upload. Keep the deck powered until it restarts and reconnects.")
        else:
            self.message.setText("Choose the application .bin. The complete image is validated before upload.")

    def _choose(self) -> None:
        path, _ = QFileDialog.getOpenFileName(self, "Select NEXUS DECK firmware", "", "Firmware (*.bin)")
        if path:
            self.load_image(path)

    def load_image(self, path: str) -> None:
        if self.busy:
            return
        self.image = None
        self.version.setText("—")
        self.size.setText("—")
        self.digest.setText("—")
        try:
            self.image = FirmwareImage.load(path)
        except (OSError, ValueError) as exc:
            self.file_name.setText("Select a valid NEXUS DECK application image")
            self._refresh()
            self.message.setText(str(exc))
            return
        self.file_name.setText(self.image.path.name)
        self.file_name.setToolTip(str(self.image.path))
        self.version.setText(self.image.version)
        self.size.setText(f"{len(self.image.data):,} bytes")
        self.digest.setText(self.image.sha256)
        self.progress.setValue(0)
        self._refresh()

    def _upload(self) -> None:
        if not self.image or not self.upload.isEnabled() or self.busy:
            return
        self.busy = True
        self.choose.setEnabled(False)
        self.upload.setEnabled(False)
        self.close_button.setEnabled(False)
        self.cancel.setEnabled(True)
        self.cancel.show()
        self._progress(0, "Preparing the wireless update")
        self.controller.upload_firmware(self.image)

    def _progress(self, value: int, message: str) -> None:
        if not self.busy:
            return
        self.progress.setValue(max(0, min(100, value)))
        self.message.setText(message)
        self.cancel.setEnabled(value < 96)

    def _cancel(self) -> None:
        self.cancel.setEnabled(False)
        self.message.setText("Cancelling the transfer…")
        self.controller.cancel_firmware_update()

    def _finished(self, success: bool, message: str) -> None:
        if not self.busy:
            return
        self.busy = False
        self.choose.setEnabled(True)
        self.close_button.setEnabled(True)
        self.cancel.hide()
        self.progress.setValue(100 if success else 0)
        self._refresh()
        self.message.setText(message)

    def reject(self) -> None:
        if not self.busy:
            super().reject()

    def closeEvent(self, event) -> None:
        if self.busy:
            event.ignore()
        else:
            super().closeEvent(event)
