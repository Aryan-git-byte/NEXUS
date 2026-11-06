"""High-contrast light controls, including native dropdown popups."""
from PySide6.QtGui import QColor, QPalette
from PySide6.QtWidgets import QApplication


def configure_theme(app: QApplication) -> None:
    app.setStyle("Fusion")
    palette = QPalette()
    colors = {
        QPalette.ColorRole.Window: "#F3F5FB",
        QPalette.ColorRole.WindowText: "#20202A",
        QPalette.ColorRole.Base: "#FFFFFF",
        QPalette.ColorRole.AlternateBase: "#EDF2FA",
        QPalette.ColorRole.Text: "#20202A",
        QPalette.ColorRole.Button: "#FFFFFF",
        QPalette.ColorRole.ButtonText: "#20202A",
        QPalette.ColorRole.ToolTipBase: "#FFFFFF",
        QPalette.ColorRole.ToolTipText: "#20202A",
        QPalette.ColorRole.Highlight: "#0067D8",
        QPalette.ColorRole.HighlightedText: "#FFFFFF",
        QPalette.ColorRole.Link: "#0067D8",
        QPalette.ColorRole.PlaceholderText: "#647087",
        QPalette.ColorRole.Light: "#FFFFFF",
        QPalette.ColorRole.Midlight: "#F2F5FA",
        QPalette.ColorRole.Dark: "#9AAAC4",
        QPalette.ColorRole.Mid: "#CBD2E0",
        QPalette.ColorRole.Shadow: "#8594AC",
    }
    for role, color in colors.items():
        palette.setColor(role, QColor(color))
    for role in (QPalette.ColorRole.WindowText, QPalette.ColorRole.Text, QPalette.ColorRole.ButtonText):
        palette.setColor(QPalette.ColorGroup.Disabled, role, QColor("#7A859A"))
    app.setPalette(palette)
