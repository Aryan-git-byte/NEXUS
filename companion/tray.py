"""Native Windows notification-area controls."""
from __future__ import annotations
from PySide6.QtGui import QAction, QColor, QIcon, QPainter, QPixmap
from PySide6.QtWidgets import QApplication, QMenu, QSystemTrayIcon


def app_icon() -> QIcon:
    image = QPixmap(64, 64)
    image.fill(QColor("transparent"))
    painter = QPainter(image)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing)
    painter.setBrush(QColor("#FFFFFF"))
    painter.setPen(QColor("#CBD2E0"))
    painter.drawRoundedRect(2, 2, 60, 60, 14, 14)
    for row in range(2):
        for column in range(3):
            colors = ("#087AD8", "#E34C88", "#24A26E", "#ED8A3A", "#8860DD", "#2C99D4")
            painter.setBrush(QColor(colors[row * 3 + column]))
            painter.setPen(QColor("transparent"))
            painter.drawRoundedRect(12 + column * 15, 18 + row * 16, 10, 10, 3, 3)
    painter.end()
    return QIcon(image)


class Tray:
    def __init__(self, editor, controller):
        self.editor, self.controller = editor, controller
        self.icon = QSystemTrayIcon(app_icon(), editor)
        self.icon.setToolTip("NEXUS DECK · Disconnected")
        menu = QMenu(editor)
        for label, callback in (("Open editor", self.show), ("Connect", controller.connect_device),
                                ("Disconnect", controller.disconnect_device),
                                ("Synchronize profiles", editor.synchronize)):
            action = QAction(label, menu)
            action.triggered.connect(callback)
            menu.addAction(action)
        menu.addSeparator()
        quit_action = QAction("Quit NEXUS DECK", menu)
        quit_action.triggered.connect(QApplication.quit)
        menu.addAction(quit_action)
        self.icon.setContextMenu(menu)
        self.icon.activated.connect(lambda reason: self.show() if reason == QSystemTrayIcon.ActivationReason.DoubleClick else None)
        controller.status_changed.connect(lambda state, detail: self.icon.setToolTip(f"NEXUS DECK · {state.title()}\n{detail[:120]}"))
        self.icon.show()

    def show(self) -> None:
        self.editor.showNormal()
        self.editor.raise_()
        self.editor.activateWindow()
