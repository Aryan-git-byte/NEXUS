"""End-to-end draft editing and persistence through the real Qt widgets."""
import os
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

import base64
from unittest.mock import Mock

import pytest
from PySide6.QtCore import Qt
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication, QMessageBox
from companion.config_editor import ConfigEditor
from companion.controller import Controller
from companion.profiles import ProfileManager
from companion.settings import Settings


@pytest.fixture(scope="module")
def app():
    application = QApplication.instance() or QApplication([])
    yield application


def test_edit_reorder_custom_icon_long_press_and_reload(app, tmp_path, monkeypatch):
    manager = ProfileManager(tmp_path)
    controller = Controller(Settings(auto_connect=False), manager, tmp_path)
    controller.profiles_updated = Mock()
    warning = Mock()
    monkeypatch.setattr("companion.config_editor.QMessageBox.warning", warning)
    editor = ConfigEditor(manager, controller, no_tray=True)
    editor.show()
    try:
        QTest.qWait(30)
        editor._reload_profiles("development")
        original_id = editor.draft["buttons"][0]["id"]
        editor.label.selectAll()
        QTest.keyClicks(editor.label, "Build project")
        editor.tap.set_action({"type": "keyboard_shortcut", "keys": "ctrl+shift+b"})
        editor.long_enabled.setChecked(True)
        editor.long.set_action({"type": "keyboard_shortcut", "keys": "ctrl+s"})
        editor._custom_icon = base64.b64encode(bytes([0xAA] * 72)).decode()
        editor.apply_button()
        editor.move_button(1)
        editor.add_button()
        assert editor.selected == 8
        assert editor.preview_page == 1
        editor.remove_button()
        assert editor.save_profile()
        reloaded = ProfileManager(tmp_path)
        button = next(b for b in reloaded.profiles["development"]["buttons"] if b["id"] == original_id)
        assert button["label"] == "Build project"
        assert button["action"] == {"type": "keyboard_shortcut", "keys": "ctrl+shift+b"}
        assert button["long_press"] == {"type": "keyboard_shortcut", "keys": "ctrl+s"}
        assert len(base64.b64decode(button["icon_data"])) == 72
        assert reloaded.profiles["development"]["buttons"][1]["id"] == original_id
        controller.profiles_updated.assert_called()
        warning.assert_not_called()
    finally:
        editor.hide()
        editor.deleteLater()
        app.processEvents()


def test_snippet_requires_local_checkbox_approval(app, tmp_path, monkeypatch):
    manager = ProfileManager(tmp_path)
    editor = ConfigEditor(manager, Controller(Settings(auto_connect=False), manager, tmp_path), no_tray=True)
    warning = Mock()
    monkeypatch.setattr("companion.config_editor.QMessageBox.warning", warning)
    try:
        editor.tap.set_action({"type": "text_snippet", "text": "Approved in the editor", "approved": False})
        assert not editor._commit_button()
        warning.assert_called_once()
        editor.tap.approved.setChecked(True)
        assert editor.save_profile()
        assert manager.profiles["default"]["buttons"][0]["action"]["approved"] is True
    finally:
        editor.deleteLater()
        app.processEvents()


def test_new_profile_preserves_a_draft_when_cancelled(app, tmp_path, monkeypatch):
    manager = ProfileManager(tmp_path)
    editor = ConfigEditor(manager, Controller(Settings(auto_connect=False), manager, tmp_path), no_tray=True)
    ask_name = Mock()
    monkeypatch.setattr("companion.config_editor.QInputDialog.getText", ask_name)
    monkeypatch.setattr("companion.config_editor.QMessageBox.question",
                        lambda *args: QMessageBox.StandardButton.Cancel)
    try:
        editor.label.setText("Unsaved button")
        editor._mark_dirty()
        editor.new_profile()
        assert editor.label.text() == "Unsaved button" and editor.dirty
        ask_name.assert_not_called()
        assert editor.profile_list.currentItem().data(Qt.ItemDataRole.UserRole) == "default"
    finally:
        editor.deleteLater()
        app.processEvents()
