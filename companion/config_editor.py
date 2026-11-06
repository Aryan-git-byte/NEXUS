"""A complete native profile editor with a responsive deck preview."""
from __future__ import annotations

import base64
import copy
import re
from datetime import datetime
from pathlib import Path
from typing import Any

from PIL import Image, ImageOps
from PySide6.QtCore import Qt, QTimer, QSize
from PySide6.QtGui import QColor, QFont, QImage, QPixmap, QIcon
from PySide6.QtWidgets import (QCheckBox, QComboBox, QFileDialog, QFormLayout, QFrame,
                               QGridLayout, QHBoxLayout, QInputDialog, QLabel, QLineEdit,
                               QListWidget, QListWidgetItem, QMainWindow, QMessageBox,
                               QPushButton, QScrollArea, QSplitter, QVBoxLayout, QWidget)

from .action_form import ActionForm
from .connection_dialog import ConnectionDialog
from .profiles import CATEGORIES, ICONS, MAX_BUTTONS, validate_profile
from .tray import app_icon
from .icons import asset_from_json, builtin_icon, import_image, tile_color
from .icon_widget import IconWidget, asset_pixmap
from .settings import resource_directory

STYLE = """
QWidget { background: #F3F5FB; color: #20202A; font-family: 'Segoe UI'; font-size: 13px; }
QMainWindow { background: #F3F5FB; }
QLabel#brand { font-size: 21px; font-weight: 700; letter-spacing: 2px; }
QLabel#sectionTitle { font-size: 19px; font-weight: 600; }
QLabel#secondary, QLabel#statusDetail { color: #586174; }
QLabel#status { border: 1px solid #CBD2E0; border-radius: 13px; padding: 5px 12px; color: #586174; background: white; }
QFrame#panel { background: #FFFFFF; border: 1px solid #DCE1EB; border-radius: 14px; }
QFrame#deck { background: #F3F5FB; border: 1px solid #CBD2E0; border-radius: 20px; }
QFrame#panel QLabel { background: transparent; }
QPushButton { background: #FFFFFF; border: 1px solid #CBD2E0; border-radius: 7px; padding: 8px 12px; }
QPushButton:hover { background: #EAF1FC; border-color: #9AAAC4; }
QPushButton:pressed { background: #DCEBFF; }
QPushButton:disabled { color: #7A859A; border-color: #DCE1EB; }
QPushButton#primary { background: #0067D8; color: white; border: none; font-weight: 600; }
QPushButton#primary:hover { background: #147DEB; }
QPushButton#macro { background: #E7ECFF; border: 1px solid #CBD2E0; border-radius: 12px; padding: 8px; }
QPushButton#macro:checked { border: 2px solid #0067D8; }
QPushButton#macro:hover { border-color: #0067D8; }
QPushButton#macro QLabel { background: transparent; }
QLineEdit, QComboBox, QPlainTextEdit { background: #FFFFFF; color: #20202A; selection-background-color: #0067D8; selection-color: #FFFFFF; border: 1px solid #CBD2E0; border-radius: 6px; padding: 7px; }
QLineEdit:focus, QComboBox:focus, QPlainTextEdit:focus { border-color: #0067D8; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView { background: #FFFFFF; color: #20202A; selection-background-color: #0067D8; selection-color: #FFFFFF; border: 1px solid #CBD2E0; outline: none; }
QListWidget { background: transparent; border: none; outline: none; }
QListWidget::item { padding: 12px 9px; border-radius: 7px; color: #586174; }
QListWidget::item:selected { background: #DCEBFF; color: #20202A; }
QListWidget::item:hover { background: #EAF1FC; }
QScrollArea { border: none; background: transparent; }
QScrollBar:vertical { background: transparent; width: 7px; }
QScrollBar::handle:vertical { background: #B7C3D7; border-radius: 3px; min-height: 30px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QCheckBox { spacing: 8px; padding: 5px 0; }
QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid #9AAAC4; border-radius: 4px; }
QCheckBox::indicator:checked { background: #0067D8; border-color: #0067D8; }
QSplitter::handle { background: transparent; width: 12px; }
QMenu { background: #FFFFFF; border: 1px solid #CBD2E0; padding: 5px; }
QMenu::item { padding: 8px 24px; }
QMenu::item:selected { background: #DCEBFF; }
"""


def icon_pixmap(data: str, size: int = 30) -> QPixmap:
    bits = base64.b64decode(data)
    image = QImage(24, 24, QImage.Format.Format_ARGB32)
    image.fill(Qt.GlobalColor.transparent)
    for pixel in range(576):
        if bits[pixel // 8] & (0x80 >> (pixel % 8)):
            image.setPixelColor(pixel % 24, pixel // 24, QColor("#20202A"))
    return QPixmap.fromImage(image).scaled(size, size, Qt.AspectRatioMode.KeepAspectRatio,
                                         Qt.TransformationMode.SmoothTransformation)


class ConfigEditor(QMainWindow):
    def __init__(self, profiles, controller, no_tray: bool = False):
        super().__init__()
        self.profiles, self.controller, self.no_tray = profiles, controller, no_tray
        self.setWindowTitle("NEXUS DECK")
        self.setWindowIcon(app_icon())
        self.resize(1240, 800)
        self.setMinimumSize(1050, 700)
        arrow = (resource_directory() / "assets/chevron-down.svg").as_posix()
        self.setStyleSheet(STYLE + f"QComboBox::down-arrow {{ image: url('{arrow}'); width: 12px; height: 8px; }}")
        self.draft: dict[str, Any] | None = None
        self.selected = 0
        self.preview_page = 0
        self.dirty = False
        self.loading = False
        self.state = "disconnected"
        self._custom_icon: str | None = None
        self._color_icon: dict[str, str] | None = None
        self._build()
        controller.status_changed.connect(self._status)
        controller.telemetry_changed.connect(self._telemetry)
        controller.sync_finished.connect(self._synced)
        controller.action_finished.connect(lambda button, result: self.footer.setText(f"{button}: {result}"))
        controller.error.connect(lambda text: self.footer.setText(text))
        controller.pairing_requested.connect(self._pairing)
        self._reload_profiles(controller.settings.active_profile)
        self.clock_timer = QTimer(self)
        self.clock_timer.timeout.connect(self._clock)
        self.clock_timer.start(1000)
        self._clock()

    def _build(self) -> None:
        central = QWidget()
        outer = QVBoxLayout(central)
        outer.setContentsMargins(24, 22, 24, 14)
        outer.setSpacing(18)
        header = QHBoxLayout()
        logo = QLabel()
        logo.setPixmap(app_icon().pixmap(40, 40))
        header.addWidget(logo)
        names = QVBoxLayout()
        brand = QLabel("NEXUS DECK")
        brand.setObjectName("brand")
        names.addWidget(brand)
        tagline = QLabel("Your shortcuts. One touch.")
        tagline.setObjectName("secondary")
        names.addWidget(tagline)
        header.addLayout(names)
        header.addStretch()
        self.status_badge = QLabel("Disconnected")
        self.status_badge.setObjectName("status")
        header.addWidget(self.status_badge)
        connection = QPushButton("Connection")
        connection.clicked.connect(self.open_connection)
        header.addWidget(connection)
        self.sync_button = QPushButton("Sync to deck")
        self.sync_button.setObjectName("primary")
        self.sync_button.clicked.connect(self.synchronize)
        header.addWidget(self.sync_button)
        outer.addLayout(header)
        self.status_detail = QLabel("Your deck stays usable offline. Connect to run PC actions.")
        self.status_detail.setObjectName("statusDetail")
        self.status_detail.setWordWrap(True)
        outer.addWidget(self.status_detail)

        splitter = QSplitter()
        outer.addWidget(splitter, 1)
        sidebar = QFrame()
        sidebar.setObjectName("panel")
        sidebar.setMinimumWidth(170)
        sidebar.setMaximumWidth(220)
        side = QVBoxLayout(sidebar)
        side.setContentsMargins(14, 18, 14, 14)
        side_title = QLabel("Profiles")
        side_title.setObjectName("sectionTitle")
        side.addWidget(side_title)
        self.profile_list = QListWidget()
        self.profile_list.currentItemChanged.connect(self._profile_changed)
        side.addWidget(self.profile_list, 1)
        for label, callback in (("+  New profile", self.new_profile), ("Rename", self.rename_profile),
                                ("Remove profile", self.remove_profile)):
            btn = QPushButton(label)
            btn.clicked.connect(callback)
            side.addWidget(btn)
        splitter.addWidget(sidebar)

        middle = QWidget()
        center = QVBoxLayout(middle)
        center.setContentsMargins(10, 0, 10, 0)
        center.setSpacing(14)
        row = QHBoxLayout()
        self.profile_title = QLabel("Default")
        self.profile_title.setObjectName("sectionTitle")
        row.addWidget(self.profile_title, 1)
        self.orientation = QComboBox()
        self.orientation.addItems(["Landscape", "Portrait"])
        self.orientation.currentIndexChanged.connect(self._render_preview)
        row.addWidget(self.orientation)
        center.addLayout(row)
        self.deck = QFrame()
        self.deck.setObjectName("deck")
        self.deck_layout = QVBoxLayout(self.deck)
        self.deck_layout.setContentsMargins(18, 18, 18, 14)
        self.deck_layout.setSpacing(12)
        top = QHBoxLayout()
        self.clock_label = QLabel("--:--")
        self.clock_label.setStyleSheet("font-size: 24px; font-weight: 600;")
        self.date_label = QLabel()
        self.date_label.setObjectName("secondary")
        top.addWidget(self.clock_label)
        top.addStretch()
        top.addWidget(self.date_label)
        self.deck_layout.addLayout(top)
        self.grid_widget = QWidget()
        self.grid = QGridLayout(self.grid_widget)
        self.grid.setContentsMargins(0, 0, 0, 0)
        self.grid.setSpacing(8)
        self.deck_layout.addWidget(self.grid_widget, 1)
        page = QHBoxLayout()
        previous = QPushButton("‹")
        previous.setFixedWidth(36)
        previous.clicked.connect(lambda: self._paginate(-1))
        self.page_label = QLabel()
        self.page_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.page_label.setObjectName("secondary")
        next_page = QPushButton("›")
        next_page.setFixedWidth(36)
        next_page.clicked.connect(lambda: self._paginate(1))
        page.addWidget(previous)
        page.addWidget(self.page_label, 1)
        page.addWidget(next_page)
        self.deck_layout.addLayout(page)
        center.addWidget(self.deck, 1)
        instruction = QLabel("Select a button to edit it. Preview taps select buttons.")
        instruction.setObjectName("secondary")
        instruction.setWordWrap(True)
        center.addWidget(instruction)
        tools = QHBoxLayout()
        for label, callback in (("+ Add", self.add_button), ("Remove", self.remove_button),
                                ("← Move", lambda: self.move_button(-1)), ("Move →", lambda: self.move_button(1))):
            tool = QPushButton(label)
            tool.clicked.connect(callback)
            tools.addWidget(tool)
        center.addLayout(tools)
        monitor_row = QHBoxLayout()
        self.metric_labels: dict[str, QLabel] = {}
        for key, label in (("cpu", "CPU"), ("ram", "RAM"), ("volume", "Volume")):
            metric = QLabel(f"{label}  —")
            metric.setStyleSheet("background: #FFFFFF; border: 1px solid #DCE1EB; border-radius: 7px; padding: 10px;")
            monitor_row.addWidget(metric)
            self.metric_labels[key] = metric
        center.addLayout(monitor_row)
        splitter.addWidget(middle)

        inspector_panel = QFrame()
        inspector_panel.setObjectName("panel")
        inspector_panel.setMinimumWidth(290)
        inspector_panel.setMaximumWidth(360)
        inspector_outer = QVBoxLayout(inspector_panel)
        inspector_outer.setContentsMargins(16, 18, 16, 14)
        self.button_title = QLabel("Button")
        self.button_title.setObjectName("sectionTitle")
        inspector_outer.addWidget(self.button_title)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        inspector_outer.addWidget(scroll, 1)
        fields_widget = QWidget()
        fields = QVBoxLayout(fields_widget)
        fields.setContentsMargins(0, 5, 4, 5)
        form = QFormLayout()
        form.setRowWrapPolicy(QFormLayout.RowWrapPolicy.WrapAllRows)
        self.label = QLineEdit()
        self.label.setMaxLength(48)
        self.label.textEdited.connect(self._mark_dirty)
        form.addRow("Label", self.label)
        self.icon = QComboBox()
        self.icon.setIconSize(QSize(24,24))
        for key, glyph in ICONS.items():
            asset = builtin_icon(key)
            if asset:
                self.icon.addItem(QIcon(asset_pixmap(asset,24)),key.replace('_', ' ').title(),key)
            else:
                self.icon.addItem(f"{glyph}   {key.replace('_', ' ').title()}",key)
        self.icon.currentIndexChanged.connect(self._mark_dirty)
        form.addRow("Icon", self.icon)
        image_tools = QHBoxLayout()
        import_icon = QPushButton("Import icon")
        import_icon.clicked.connect(self.import_icon)
        clear_icon = QPushButton("Use built-in")
        clear_icon.clicked.connect(self.clear_icon)
        image_tools.addWidget(import_icon)
        image_tools.addWidget(clear_icon)
        form.addRow(image_tools)
        icon_help = QLabel("Color images and SVGs. GIF, APNG and WebP animations.")
        icon_help.setObjectName("secondary")
        icon_help.setWordWrap(True)
        form.addRow(icon_help)
        self.category = QComboBox()
        for key in CATEGORIES:
            self.category.addItem(key.title(), key)
        self.category.currentIndexChanged.connect(self._mark_dirty)
        form.addRow("Profile category", self.category)
        fields.addLayout(form)
        tap_label = QLabel("Tap action")
        tap_label.setStyleSheet("font-weight: 600; margin-top: 8px;")
        fields.addWidget(tap_label)
        self.tap = ActionForm()
        self.tap.changed.connect(self._mark_dirty)
        fields.addWidget(self.tap)
        self.long_enabled = QCheckBox("Use a long-press action")
        self.long_enabled.toggled.connect(self._long_changed)
        fields.addWidget(self.long_enabled)
        self.long = ActionForm()
        self.long.changed.connect(self._mark_dirty)
        fields.addWidget(self.long)
        fields.addStretch()
        scroll.setWidget(fields_widget)
        apply_button = QPushButton("Apply button")
        apply_button.clicked.connect(self.apply_button)
        inspector_outer.addWidget(apply_button)
        self.save_button = QPushButton("Save profile")
        self.save_button.setObjectName("primary")
        self.save_button.clicked.connect(self.save_profile)
        inspector_outer.addWidget(self.save_button)
        splitter.addWidget(inspector_panel)
        splitter.setSizes([185, 600, 330])
        self.footer = QLabel("Ready")
        self.footer.setWordWrap(True)
        self.footer.setObjectName("secondary")
        outer.addWidget(self.footer)
        self.setCentralWidget(central)

    def _mark_dirty(self, *args) -> None:
        if not self.loading:
            self.dirty = True
            self.save_button.setText("Save profile •")

    def _long_changed(self, enabled: bool) -> None:
        self.long.setVisible(enabled)
        self._mark_dirty()

    def _reload_profiles(self, active: str) -> None:
        self.profile_list.blockSignals(True)
        self.profile_list.clear()
        profiles = sorted(self.profiles.profiles.values(),
                          key=lambda p: (p["id"] != "default", p["name"].casefold()))
        for profile in profiles:
            item = QListWidgetItem(profile["name"])
            item.setData(Qt.ItemDataRole.UserRole, profile["id"])
            self.profile_list.addItem(item)
            if profile["id"] == active:
                self.profile_list.setCurrentItem(item)
        if not self.profile_list.currentItem():
            self.profile_list.setCurrentRow(0)
        item = self.profile_list.currentItem()
        self.profile_list.blockSignals(False)
        if item:
            self._load_profile(item.data(Qt.ItemDataRole.UserRole))

    def _profile_changed(self, current, previous) -> None:
        if current is None:
            return
        if not self._resolve_draft():
            self.profile_list.blockSignals(True)
            self.profile_list.setCurrentItem(previous)
            self.profile_list.blockSignals(False)
            return
        self._load_profile(current.data(Qt.ItemDataRole.UserRole))

    def _resolve_draft(self) -> bool:
        if self.dirty and self.draft:
            answer = QMessageBox.question(self, "Unsaved profile", "Save this profile before switching?",
                QMessageBox.StandardButton.Save | QMessageBox.StandardButton.Discard | QMessageBox.StandardButton.Cancel)
            if answer == QMessageBox.StandardButton.Cancel or (answer == QMessageBox.StandardButton.Save and not self.save_profile()):
                return False
        return True

    def _load_profile(self, profile_id: str) -> None:
        self.draft = copy.deepcopy(self.profiles.profiles[profile_id])
        self.selected = self.preview_page = 0
        self.dirty = False
        self.profile_title.setText(self.draft["name"])
        self.save_button.setText("Save profile")
        self._load_button()
        self._render_preview()

    def _load_button(self) -> None:
        if not self.draft:
            return
        self.loading = True
        button = self.draft["buttons"][self.selected]
        self.button_title.setText(f"Button {self.selected + 1}")
        self.label.setText(button["label"])
        self.icon.setCurrentIndex(self.icon.findData(button["icon"]))
        self._custom_icon = button.get("icon_data")
        self._color_icon = button.get("icon_image")
        self.category.setCurrentIndex(self.category.findData(self.draft["category"]))
        self.tap.set_action(button["action"])
        self.long_enabled.setChecked("long_press" in button)
        self.long.set_action(button.get("long_press", {"type": "keyboard_shortcut", "keys": "ctrl+c"}))
        self.long.setVisible("long_press" in button)
        self.loading = False

    def _commit_button(self) -> bool:
        if not self.draft:
            return False
        try:
            candidate = copy.deepcopy(self.draft)
            button = candidate["buttons"][self.selected]
            button.update(label=self.label.text().strip(), icon=self.icon.currentData(), action=self.tap.action())
            if self._custom_icon:
                button["icon_data"] = self._custom_icon
            else:
                button.pop("icon_data", None)
            if self._color_icon:
                button.pop("icon_data", None)
                button["icon_image"] = self._color_icon
            else:
                button.pop("icon_image", None)
            if self.long_enabled.isChecked():
                button["long_press"] = self.long.action()
            else:
                button.pop("long_press", None)
            candidate["category"] = self.category.currentData()
            validated = validate_profile(candidate)
            if validated != self.draft:
                self.draft = validated
                self._mark_dirty()
            return True
        except ValueError as exc:
            QMessageBox.warning(self, "Button configuration", str(exc))
            return False

    def apply_button(self) -> None:
        if self._commit_button():
            self._render_preview()
            self.footer.setText("Button updated. Save the profile, then sync to your deck.")

    def save_profile(self) -> bool:
        if not self._commit_button():
            return False
        try:
            self.draft = self.profiles.save(self.draft)
            self.controller.profiles_updated()
            self.dirty = False
            self.save_button.setText("Save profile")
            self.profile_title.setText(self.draft["name"])
            for index in range(self.profile_list.count()):
                item = self.profile_list.item(index)
                if item.data(Qt.ItemDataRole.UserRole) == self.draft["id"]:
                    item.setText(self.draft["name"])
                    break
            self.footer.setText("Profile saved on this PC. Sync to update your deck.")
            self._render_preview()
            return True
        except (ValueError, OSError) as exc:
            QMessageBox.warning(self, "Save profile", str(exc))
            return False

    def synchronize(self) -> None:
        if self.save_profile():
            self.sync_button.setEnabled(False)
            self.footer.setText("Synchronizing…")
            self.controller.synchronize(self.draft["id"])

    def _synced(self, success: bool, message: str) -> None:
        self.sync_button.setEnabled(True)
        self.footer.setText(message)
        if not success:
            self.footer.setStyleSheet("color: #985000;")
        else:
            self.footer.setStyleSheet("color: #15803D;")

    def _preview_layout(self) -> tuple[int, int]:
        portrait = self.orientation.currentIndex() == 1
        if self.draft and self.draft["category"] == "media":
            return (2,6) if portrait else (3,6)
        return (3,12) if portrait else (4,8)

    def _render_preview(self, *args) -> None:
        if not self.draft:
            return
        while self.grid.count():
            item = self.grid.takeAt(0)
            if item.widget():
                item.widget().deleteLater()
        portrait = self.orientation.currentIndex() == 1
        columns, per_page = self._preview_layout()
        pages = max(1, (len(self.draft["buttons"]) + per_page - 1) // per_page)
        self.preview_page = min(self.preview_page, pages - 1)
        self.deck.setMaximumWidth(410 if portrait else 650)
        self.grid_widget.setMinimumHeight(340 if portrait else 240)
        start = self.preview_page * per_page
        for index, button in enumerate(self.draft["buttons"][start:start + per_page], start):
            widget = QPushButton()
            widget.setObjectName("macro")
            widget.setStyleSheet(f"QPushButton#macro {{ background: {tile_color(button['icon'])}; }}")
            widget.setCheckable(True)
            widget.setChecked(index == self.selected)
            widget.setMinimumSize(70, 84 if portrait else 100)
            content = QVBoxLayout(widget)
            content.setAlignment(Qt.AlignmentFlag.AlignCenter)
            asset = asset_from_json(button["icon_image"]["data"]) if button.get("icon_image") else builtin_icon(button["icon"])
            icon = IconWidget(asset) if asset and not button.get("icon_data") else QLabel()
            icon.setAlignment(Qt.AlignmentFlag.AlignCenter)
            if button.get("icon_data"):
                icon.setPixmap(icon_pixmap(button["icon_data"]))
            elif not asset:
                icon.setText(ICONS[button["icon"]])
                icon.setStyleSheet("font-size: 29px; color: #26324A;")
            content.addWidget(icon)
            label = QLabel(button["label"])
            label.setWordWrap(True)
            label.setAlignment(Qt.AlignmentFlag.AlignCenter)
            label.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents)
            icon.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents)
            content.addWidget(label)
            widget.clicked.connect(lambda checked=False, i=index: self.select_button(i))
            self.grid.addWidget(widget, (index - start) // columns, (index - start) % columns)
        self.page_label.setText(f"{self.draft['name']}   ·   {self.preview_page + 1} / {pages}")

    def select_button(self, index: int) -> None:
        if index == self.selected:
            self._render_preview()
            return
        if self._commit_button():
            self.selected = index
            self._load_button()
            self._render_preview()

    def _paginate(self, delta: int) -> None:
        per_page = self._preview_layout()[1]
        pages = max(1, (len(self.draft["buttons"]) + per_page - 1) // per_page)
        if self._commit_button():
            self.preview_page = (self.preview_page + delta) % pages
            self.selected = self.preview_page * per_page
            self._load_button()
            self._render_preview()

    def add_button(self) -> None:
        if not self._commit_button():
            return
        if len(self.draft["buttons"]) >= MAX_BUTTONS:
            self.footer.setText(f"A profile supports up to {MAX_BUTTONS} buttons")
            return
        used = {b["id"] for b in self.draft["buttons"]}
        number = 1
        while f"button_{number}" in used:
            number += 1
        self.draft["buttons"].append({"id": f"button_{number}", "label": "New button", "icon": "custom",
                                      "action": {"type": "keyboard_shortcut", "keys": "ctrl+c"}})
        self.selected = len(self.draft["buttons"]) - 1
        self.preview_page = self.selected // self._preview_layout()[1]
        self._mark_dirty()
        self._load_button()
        self._render_preview()

    def remove_button(self) -> None:
        if len(self.draft["buttons"]) <= 1:
            self.footer.setText("Keep at least one button in each profile")
            return
        self.draft["buttons"].pop(self.selected)
        self.selected = min(self.selected, len(self.draft["buttons"]) - 1)
        self._mark_dirty()
        self._load_button()
        self._render_preview()

    def move_button(self, delta: int) -> None:
        destination = self.selected + delta
        if not 0 <= destination < len(self.draft["buttons"]) or not self._commit_button():
            return
        buttons = self.draft["buttons"]
        buttons[self.selected], buttons[destination] = buttons[destination], buttons[self.selected]
        self.selected = destination
        self.preview_page = destination // self._preview_layout()[1]
        self._mark_dirty()
        self._load_button()
        self._render_preview()

    def new_profile(self) -> None:
        if not self._resolve_draft():
            return
        name, okay = QInputDialog.getText(self, "New profile", "Profile name")
        if not okay or not name.strip():
            return
        profile_id = re.sub(r"[^a-z0-9_-]+", "_", name.lower()).strip("_")[:24]
        if not profile_id or not profile_id[0].isalpha():
            profile_id = "profile_" + profile_id
        original, number = profile_id, 2
        while profile_id in self.profiles.profiles:
            profile_id = f"{original}_{number}"
            number += 1
        try:
            self.profiles.save({"id": profile_id, "name": name.strip(), "category": "custom", "revision": 1,
                                "buttons": [{"id": "copy", "label": "Copy", "icon": "copy",
                                             "action": {"type": "keyboard_shortcut", "keys": "ctrl+c"}}]})
            self.controller.profiles_updated()
            self._reload_profiles(profile_id)
        except (ValueError, OSError) as exc:
            QMessageBox.warning(self, "New profile", str(exc))

    def rename_profile(self) -> None:
        name, okay = QInputDialog.getText(self, "Rename profile", "Profile name", text=self.draft["name"])
        if okay and name.strip():
            self.draft["name"] = name.strip()
            self._mark_dirty()
            self.save_profile()

    def remove_profile(self) -> None:
        answer = QMessageBox.question(self, "Remove profile", f"Remove {self.draft['name']} from this PC? Sync afterward to update the deck.")
        if answer != QMessageBox.StandardButton.Yes:
            return
        try:
            self.profiles.delete(self.draft["id"])
            self.controller.profiles_updated()
            self._reload_profiles(next(iter(self.profiles.profiles)))
        except (ValueError, OSError) as exc:
            QMessageBox.warning(self, "Remove profile", str(exc))

    def import_icon(self) -> None:
        path, _ = QFileDialog.getOpenFileName(self, "Import icon or animation", "", "Images and vectors (*.png *.apng *.bmp *.jpg *.jpeg *.ico *.svg *.gif *.webp)")
        if not path:
            return
        try:
            self._color_icon = import_image(path).json()
            self._custom_icon = None
            self._mark_dirty()
            self.apply_button()
        except (OSError, ValueError, Image.DecompressionBombError) as exc:
            QMessageBox.warning(self, "Import icon", str(exc))

    def clear_icon(self) -> None:
        self._custom_icon = None
        self._color_icon = None
        self._mark_dirty()
        self.apply_button()

    def open_connection(self) -> None:
        dialog = ConnectionDialog(self.controller, self)
        dialog.show()

    def _pairing(self, token: str, address: str) -> None:
        self.showNormal()
        self.raise_()
        self.activateWindow()
        dialog = QInputDialog(self)
        dialog.setWindowTitle("Pair NEXUS DECK")
        dialog.setLabelText(f"Enter the six-digit passkey shown on your deck.\n{address}")
        dialog.setInputMode(QInputDialog.InputMode.TextInput)
        dialog.setTextValue("")
        dialog.setOkButtonText("Pair")
        dialog.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        field = dialog.findChild(QLineEdit)
        if field:
            field.setInputMask("000000;_")
        # A non-modal dialog keeps telemetry and profile controls responsive.
        dialog.accepted.connect(lambda: self.controller.answer_pairing(token, dialog.textValue()))
        dialog.rejected.connect(lambda: self.controller.answer_pairing(token, None))
        timeout = QTimer(dialog)
        timeout.setSingleShot(True)
        timeout.timeout.connect(dialog.reject)
        timeout.start(64000)
        dialog.open()

    def _status(self, state: str, detail: str) -> None:
        self.state = state
        self.status_badge.setText(state.title())
        color = "#15803D" if state == "connected" else "#985000" if state in ("connecting", "syncing") else "#586174"
        self.status_badge.setStyleSheet(f"color: {color};")
        self.status_detail.setText(detail)

    def _telemetry(self, values: dict[str, Any]) -> None:
        for key, label in self.metric_labels.items():
            value = values.get(key)
            name = "RAM" if key == "ram" else "CPU" if key == "cpu" else "Volume"
            label.setText(f"{name}   {value:.0f}%" if value is not None else f"{name}   —")

    def _clock(self) -> None:
        now = datetime.now()
        self.clock_label.setText(now.strftime("%H:%M"))
        self.date_label.setText(now.strftime("%a, %d %b"))

    def closeEvent(self, event) -> None:
        if self.no_tray:
            if self.dirty and not self.save_profile():
                event.ignore()
                return
            event.accept()
            from PySide6.QtWidgets import QApplication
            QApplication.quit()
        else:
            self.hide()
            event.ignore()
