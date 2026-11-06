"""Device discovery and explicitly configured local application paths."""
from __future__ import annotations
from pathlib import Path

import psutil
from PySide6.QtCore import Qt
from PySide6.QtWidgets import (QCheckBox, QComboBox, QDialog, QDialogButtonBox, QFileDialog,
                               QFormLayout, QHBoxLayout, QLabel, QLineEdit, QListWidget,
                               QListWidgetItem, QMessageBox, QPlainTextEdit, QPushButton,
                               QVBoxLayout, QWidget)


class ConnectionDialog(QDialog):
    def __init__(self, controller, parent=None):
        super().__init__(parent)
        self.controller = controller
        self.setWindowTitle("NEXUS DECK · Connection")
        self.setMinimumWidth(570)
        self.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        layout = QVBoxLayout(self)
        title = QLabel("Connect your deck")
        title.setObjectName("sectionTitle")
        layout.addWidget(title)
        note = QLabel("Power on the deck, open Pair new PC in its settings, then connect. Enter the deck's six-digit passkey when NEXUS DECK prompts you.")
        note.setWordWrap(True)
        note.setObjectName("secondary")
        layout.addWidget(note)
        discover = QPushButton("Discover nearby decks")
        discover.clicked.connect(self._discover)
        layout.addWidget(discover)
        self.devices = QListWidget()
        self.devices.setMaximumHeight(110)
        self.devices.itemClicked.connect(self._select_device)
        layout.addWidget(self.devices)
        self.discovery_status = QLabel("Select a deck below, or use its saved Bluetooth address.")
        self.discovery_status.setObjectName("secondary")
        layout.addWidget(self.discovery_status)
        form = QFormLayout()
        self.address = QLineEdit(controller.settings.device_address)
        self.address.setPlaceholderText("Bluetooth address")
        form.addRow("Saved deck", self.address)
        self.auto = QCheckBox("Reconnect automatically")
        self.auto.setChecked(controller.settings.auto_connect)
        form.addRow(self.auto)
        self.project = QLineEdit(controller.settings.project_directory)
        form.addRow("Project folder", self._browse_row(self.project, False))
        self.server = QLineEdit(controller.settings.dev_server_executable)
        form.addRow("Dev server .exe", self._browse_row(self.server, True))
        self.arguments = QPlainTextEdit("\n".join(controller.settings.dev_server_arguments or []))
        self.arguments.setPlaceholderText("One argument per line; for Node, start with your dev server's .js file")
        self.arguments.setMaximumHeight(75)
        form.addRow("Server arguments", self.arguments)
        self.disk = QComboBox()
        mountpoints = sorted({p.mountpoint for p in psutil.disk_partitions()})
        for path in mountpoints:
            self.disk.addItem(path)
        if controller.settings.disk_path not in mountpoints:
            self.disk.addItem(controller.settings.disk_path)
        self.disk.setCurrentText(controller.settings.disk_path)
        form.addRow("Monitored disk", self.disk)
        layout.addLayout(form)
        buttons = QHBoxLayout()
        connect = QPushButton("Save and connect")
        connect.setObjectName("primary")
        connect.clicked.connect(self._connect)
        disconnect = QPushButton("Disconnect")
        disconnect.clicked.connect(controller.disconnect_device)
        buttons.addWidget(disconnect)
        buttons.addStretch()
        save = QPushButton("Save")
        save.clicked.connect(lambda: self.accept() if self._save() else None)
        buttons.addWidget(save)
        buttons.addWidget(connect)
        layout.addLayout(buttons)
        controller.devices_found.connect(self._found)

    def _browse_row(self, field: QLineEdit, executable: bool) -> QWidget:
        row = QWidget()
        layout = QHBoxLayout(row)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(field, 1)
        browse = QPushButton("Browse")
        def choose():
            if executable:
                value, _ = QFileDialog.getOpenFileName(self, "Select executable", "", "Applications (*.exe)")
            else:
                value = QFileDialog.getExistingDirectory(self, "Select project folder")
            if value:
                field.setText(value)
        browse.clicked.connect(choose)
        layout.addWidget(browse)
        return row

    def _discover(self) -> None:
        self.discovery_status.setText("Scanning for seven seconds…")
        self.controller.discover()

    def _found(self, devices: list[dict[str, str]]) -> None:
        self.devices.clear()
        for device in devices:
            item = QListWidgetItem(f"{device['name']}   ·   {device['address']}")
            item.setData(Qt.ItemDataRole.UserRole, device["address"])
            self.devices.addItem(item)
        self.discovery_status.setText(f"Found {len(devices)} deck(s)." if devices else "No deck found. Check power, Bluetooth, and the pairing window.")

    def _select_device(self, item: QListWidgetItem) -> None:
        self.address.setText(item.data(Qt.ItemDataRole.UserRole))

    def _save(self) -> bool:
        try:
            project, server = self.project.text().strip(), self.server.text().strip()
            if project and (not Path(project).is_absolute() or not Path(project).is_dir()):
                raise ValueError("Select an existing absolute project folder")
            if server and (not Path(server).is_absolute() or not Path(server).is_file() or Path(server).suffix.lower() != ".exe"):
                raise ValueError("Select an existing absolute .exe for the development server")
            args = self.arguments.toPlainText().splitlines()
            if len(args) > 32 or any(len(a) > 1024 or "\x00" in a for a in args):
                raise ValueError("Use up to 32 arguments, each no longer than 1024 characters")
            address = self.address.text().strip()
            if len(address) > 64 or any(ord(c) < 32 for c in address):
                raise ValueError("Invalid Bluetooth address")
            settings = self.controller.settings
            settings.device_address, settings.auto_connect = address, self.auto.isChecked()
            settings.project_directory, settings.dev_server_executable = project, server
            settings.dev_server_arguments, settings.disk_path = args, self.disk.currentText()
            self.controller.update_settings()
            return True
        except (OSError, ValueError) as exc:
            QMessageBox.warning(self, "Configuration", str(exc))
            return False

    def _connect(self) -> None:
        if self._save():
            self.controller.connect_device(self.address.text().strip())
            self.accept()
