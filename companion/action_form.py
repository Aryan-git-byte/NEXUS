"""Reusable, validated editors for tap and long-press actions."""
from __future__ import annotations
from typing import Any

from PySide6.QtCore import Signal
from PySide6.QtWidgets import (QCheckBox, QComboBox, QFileDialog, QFormLayout, QHBoxLayout,
                               QLabel, QLineEdit, QPlainTextEdit, QPushButton,
                               QStackedWidget, QVBoxLayout, QWidget)

from .profiles import ACTION_TYPES, APP_ALIASES, validate_action

ACTION_LABELS = {
    "launch_app": "Launch application", "keyboard_shortcut": "Keyboard shortcut",
    "media_play_pause": "Media · Play / pause", "media_next": "Media · Next track",
    "media_previous": "Media · Previous track", "volume_up": "Volume up",
    "volume_down": "Volume down", "volume_mute": "Mute / unmute",
    "screenshot": "Capture screen region", "open_url": "Open website",
    "lock_windows": "Lock Windows", "show_desktop": "Show desktop", "text_snippet": "Type approved text",
}


class ActionForm(QWidget):
    changed = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        self.kind = QComboBox()
        for key in ACTION_TYPES:
            self.kind.addItem(ACTION_LABELS[key], key)
        layout.addWidget(self.kind)
        self.pages = QStackedWidget()
        layout.addWidget(self.pages)

        self.description = QLabel("This action uses the Windows media or system controls.")
        self.description.setWordWrap(True)
        self.description.setObjectName("secondary")
        self.pages.addWidget(self.description)

        launch = QWidget()
        form = QFormLayout(launch)
        form.setContentsMargins(0, 6, 0, 0)
        form.setRowWrapPolicy(QFormLayout.RowWrapPolicy.WrapAllRows)
        self.target = QComboBox()
        self.target.setEditable(True)
        self.target.addItems(sorted(APP_ALIASES))
        row = QWidget()
        horizontal = QHBoxLayout(row)
        horizontal.setContentsMargins(0, 0, 0, 0)
        horizontal.addWidget(self.target, 1)
        browse = QPushButton("Browse")
        browse.clicked.connect(self._browse_exe)
        horizontal.addWidget(browse)
        form.addRow("Application", row)
        self.arguments = QPlainTextEdit()
        self.arguments.setPlaceholderText("One argument per line")
        self.arguments.setMaximumHeight(76)
        form.addRow("Arguments", self.arguments)
        self.cwd = QLineEdit()
        self.cwd.setPlaceholderText("Optional working folder")
        form.addRow("Working folder", self.cwd)
        note = QLabel("Project and dev server aliases use your Connection settings.")
        note.setObjectName("secondary")
        note.setWordWrap(True)
        form.addRow(note)
        self.pages.addWidget(launch)

        self.keys = QLineEdit()
        self.keys.setPlaceholderText("ctrl+shift+s")
        shortcut = QWidget()
        shortcut_layout = QVBoxLayout(shortcut)
        shortcut_layout.setContentsMargins(0, 6, 0, 0)
        shortcut_layout.addWidget(self.keys)
        help_label = QLabel("Shortcuts are sent to the active Windows app. Examples: ctrl+c, win+d, alt+tab.")
        help_label.setObjectName("secondary")
        help_label.setWordWrap(True)
        shortcut_layout.addWidget(help_label)
        self.pages.addWidget(shortcut)

        self.url = QLineEdit()
        self.url.setPlaceholderText("https://example.com")
        self.pages.addWidget(self.url)

        text_page = QWidget()
        text_layout = QVBoxLayout(text_page)
        text_layout.setContentsMargins(0, 6, 0, 0)
        self.text = QPlainTextEdit()
        self.text.setPlaceholderText("Text to type")
        self.text.setMaximumHeight(120)
        self.approved = QCheckBox("Allow this text in the active app")
        self.approved.setToolTip("This approval is stored only in your local configuration")
        text_layout.addWidget(self.text)
        text_layout.addWidget(self.approved)
        self.pages.addWidget(text_page)

        self.kind.currentIndexChanged.connect(self._select_page)
        self.kind.currentIndexChanged.connect(self.changed)
        self.target.editTextChanged.connect(self.changed)
        for field in (self.cwd, self.keys, self.url):
            field.textEdited.connect(self.changed)
        self.arguments.textChanged.connect(self.changed)
        self.text.textChanged.connect(self.changed)
        self.approved.toggled.connect(self.changed)
        self._select_page()

    def _select_page(self) -> None:
        kind = self.kind.currentData()
        self.pages.setCurrentIndex({"launch_app": 1, "keyboard_shortcut": 2, "open_url": 3, "text_snippet": 4}.get(kind, 0))
        descriptions = {"screenshot": "Opens Windows Snipping Tool. Select a region to capture.",
                        "lock_windows": "Locks your current Windows session.",
                        "show_desktop": "Toggles the Windows desktop.",
                        "volume_up": "Increases master volume by 5%.",
                        "volume_down": "Decreases master volume by 5%.",
                        "volume_mute": "Toggles master audio mute."}
        self.description.setText(descriptions.get(kind, "Controls the current Windows media session."))

    def _browse_exe(self) -> None:
        path, _ = QFileDialog.getOpenFileName(self, "Choose application", "", "Windows application (*.exe)")
        if path:
            self.target.setEditText(path)

    def set_action(self, action: dict[str, Any]) -> None:
        self.kind.setCurrentIndex(self.kind.findData(action["type"]))
        self.target.setEditText(action.get("target", "code"))
        self.arguments.setPlainText("\n".join(action.get("args", [])))
        self.cwd.setText(action.get("cwd", ""))
        self.keys.setText(action.get("keys", "ctrl+c"))
        self.url.setText(action.get("url", "https://github.com"))
        self.text.setPlainText(action.get("text", ""))
        self.approved.setChecked(action.get("approved", False))
        self._select_page()

    def action(self) -> dict[str, Any]:
        kind = self.kind.currentData()
        value: dict[str, Any] = {"type": kind}
        if kind == "launch_app":
            value.update(target=self.target.currentText().strip(), args=self.arguments.toPlainText().splitlines())
            if self.cwd.text().strip():
                value["cwd"] = self.cwd.text().strip()
        elif kind == "keyboard_shortcut":
            value["keys"] = self.keys.text().strip()
        elif kind == "open_url":
            value["url"] = self.url.text().strip()
        elif kind == "text_snippet":
            value.update(text=self.text.toPlainText(), approved=self.approved.isChecked())
        return validate_action(value)
