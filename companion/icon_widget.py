"""Color icon preview with widget-owned animation timers."""
from PySide6.QtCore import QTimer, Qt
from PySide6.QtGui import QImage, QPixmap
from PySide6.QtWidgets import QLabel
from .icons import IconAsset


def asset_pixmap(asset: IconAsset, size: int = 32, frame: int = 0) -> QPixmap:
    image = asset.rgba(frame)
    qimage = QImage(image.tobytes(), image.width, image.height, QImage.Format.Format_RGBA8888).copy()
    return QPixmap.fromImage(qimage).scaled(size, size,
        Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation)


class IconWidget(QLabel):
    def __init__(self, asset: IconAsset, size: int = 34, parent=None):
        super().__init__(parent)
        self.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.setFixedHeight(size + 4)
        self.frames = [asset_pixmap(asset,size,index) for index in range(len(asset.frames))]
        self.durations = asset.durations
        self.frame = 0
        self.setPixmap(self.frames[0])
        self.timer = None
        if len(self.frames) > 1:
            self.timer = QTimer(self)
            self.timer.setSingleShot(True)
            self.timer.timeout.connect(self.advance)

    def showEvent(self, event):
        super().showEvent(event)
        if self.timer:
            self.timer.start(self.durations[self.frame])

    def hideEvent(self, event):
        if self.timer:
            self.timer.stop()
        super().hideEvent(event)

    def advance(self):
        self.frame = (self.frame + 1) % len(self.frames)
        self.setPixmap(self.frames[self.frame])
        self.timer.start(self.durations[self.frame])
