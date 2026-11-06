"""Color/animated icon import and the bounded NDI2 device image format."""
from __future__ import annotations

import base64
import hashlib
import io
import json
import re
import struct
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path

from PIL import Image, ImageOps

SIZE = 32
MAX_FRAMES = 16
MAX_BYTES = 10 + MAX_FRAMES * 2 + 256 * 4 + SIZE * SIZE * MAX_FRAMES
HEADER = struct.Struct("<4sBBHBB")
ASSET_ID = re.compile(r"^[0-9a-f]{14}$")
TILE_COLORS = {
    "code": "#E1EFFF", "terminal": "#E7EBF2", "powershell": "#E0EAFF", "folder": "#FFEFC9",
    "github": "#EFE5FF", "project": "#E1EFFF", "server": "#DDF5E7", "camera": "#FFE2DF",
    "play": "#FFE1ED", "next": "#E5E7FF", "previous": "#E5E7FF", "volume_up": "#E0F5E8",
    "volume_down": "#DFF2F7", "mute": "#FFE7D4", "copy": "#E0F1FF", "paste": "#E1F4E6",
    "cut": "#E1F4F6", "undo": "#EEE5FF", "save": "#E2ECFF", "desktop": "#E0F0FF",
    "lock": "#FFEFCA", "browser": "#E0F3FF", "text": "#EEE5FF", "game": "#EDE3FF",
    "mail": "#FFE8D5", "calendar": "#FFE2E9", "search": "#E0F1FF", "star": "#FFEFC9",
}


def tile_color(name: str) -> str:
    return TILE_COLORS.get(name, "#E7ECFF")


@dataclass(frozen=True)
class IconAsset:
    data: bytes
    durations: tuple[int, ...]
    palette: bytes
    frames: tuple[bytes, ...]

    @property
    def id(self) -> str:
        return hashlib.sha256(self.data).hexdigest()[:14]

    def json(self) -> dict[str, str]:
        return {"id": self.id, "data": base64.b64encode(self.data).decode("ascii")}

    def rgba(self, frame: int = 0) -> Image.Image:
        pixels = self.frames[frame % len(self.frames)]
        rgba = b"".join(self.palette[index * 4:index * 4 + 4] for index in pixels)
        return Image.frombytes("RGBA", (SIZE, SIZE), rgba)


def decode_asset(data: bytes) -> IconAsset:
    if not isinstance(data, bytes) or not HEADER.size <= len(data) <= MAX_BYTES:
        raise ValueError("Invalid icon data size")
    magic, width, height, colors, count, flags = HEADER.unpack_from(data)
    if magic != b"NDI2" or width != SIZE or height != SIZE or flags != 0:
        raise ValueError("Unsupported icon format")
    if not 1 <= colors <= 256 or not 1 <= count <= MAX_FRAMES:
        raise ValueError("Invalid icon palette or frame count")
    offset = HEADER.size + count * 2
    expected = offset + colors * 4 + count * SIZE * SIZE
    if len(data) != expected:
        raise ValueError("Truncated or oversized icon")
    durations = struct.unpack_from(f"<{count}H", data, HEADER.size)
    if any(not 40 <= duration <= 5000 for duration in durations):
        raise ValueError("Icon frame timing is out of range")
    palette = data[offset:offset + colors * 4]
    pixels = data[offset + colors * 4:]
    if max(pixels) >= colors:
        raise ValueError("Icon contains an invalid palette index")
    frames = tuple(pixels[i * SIZE * SIZE:(i + 1) * SIZE * SIZE] for i in range(count))
    return IconAsset(data, durations, palette, frames)


def validate_icon(value) -> dict[str, str]:
    if not isinstance(value, dict) or set(value) != {"id", "data"}:
        raise ValueError("Invalid color icon fields")
    if not isinstance(value["id"], str) or not ASSET_ID.fullmatch(value["id"]):
        raise ValueError("Invalid icon ID")
    text = value["data"]
    if not isinstance(text, str) or len(text) > ((MAX_BYTES + 2) // 3) * 4:
        raise ValueError("Color icon is too large")
    try:
        asset = decode_asset(base64.b64decode(text, validate=True))
    except (ValueError, base64.binascii.Error) as exc:
        raise ValueError("Invalid color icon encoding") from exc
    if asset.id != value["id"]:
        raise ValueError("Icon content does not match its ID")
    return asset.json()


def encode_frames(frames: list[Image.Image], durations: list[int]) -> IconAsset:
    if not 1 <= len(frames) <= MAX_FRAMES or len(frames) != len(durations):
        raise ValueError("Use 1–16 icon frames")
    sheet = Image.new("RGBA", (SIZE, SIZE * len(frames)), (0, 0, 0, 0))
    for index, frame in enumerate(frames):
        scaled = ImageOps.contain(frame.convert("RGBA"), (SIZE, SIZE), Image.Resampling.LANCZOS)
        tile = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
        tile.alpha_composite(scaled, ((SIZE - scaled.width) // 2, (SIZE - scaled.height) // 2))
        sheet.paste(tile, (0, index * SIZE))
    indexed = sheet.quantize(colors=256, method=Image.Quantize.FASTOCTREE)
    pixels = indexed.tobytes()
    colors = max(pixels) + 1
    palette = bytes(indexed.getpalette("RGBA")[:colors * 4])
    if len(palette) != colors * 4:
        raise ValueError("Could not build the icon palette")
    durations = [max(40, min(5000, int(value))) for value in durations]
    data = HEADER.pack(b"NDI2", SIZE, SIZE, colors, len(frames), 0)
    data += struct.pack(f"<{len(frames)}H", *durations) + palette + pixels
    return decode_asset(data)


def render_svg(data: bytes) -> IconAsset:
    # SVG stays on the PC. The ESP32 receives only validated image pixels.
    if len(data) > 10 * 1024 * 1024 or b"<!DOCTYPE" in data.upper() or b"<!ENTITY" in data.upper():
        raise ValueError("Choose a standalone SVG smaller than 10 MB")
    from PySide6.QtCore import QByteArray
    from PySide6.QtGui import QImage, QPainter
    from PySide6.QtSvg import QSvgRenderer
    renderer = QSvgRenderer(QByteArray(data))
    if not renderer.isValid():
        raise ValueError("The SVG could not be rendered")
    frames, durations = [], []
    count = 1
    cycle = 100
    if renderer.animated() and renderer.animationDuration() > 0:
        cycle = min(renderer.animationDuration(), MAX_FRAMES * 5000)
        count = min(MAX_FRAMES, max(2, round(cycle * renderer.framesPerSecond() / 1000)))
    for index in range(count):
        if count > 1:
            renderer.setCurrentFrame(round(index * renderer.animationDuration() * renderer.framesPerSecond() / (count * 1000)))
        canvas = QImage(128, 128, QImage.Format.Format_ARGB32_Premultiplied)
        canvas.fill(0)
        painter = QPainter(canvas)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        renderer.render(painter)
        painter.end()
        rgba = canvas.convertToFormat(QImage.Format.Format_RGBA8888)
        frames.append(Image.frombytes("RGBA", (128, 128), bytes(rgba.constBits())))
        durations.append(max(40, round(cycle / count)))
    return encode_frames(frames, durations)


def import_image(path: str | Path) -> IconAsset:
    path = Path(path)
    if path.stat().st_size > 10 * 1024 * 1024:
        raise ValueError("Choose an image smaller than 10 MB")
    if path.suffix.lower() == ".svg":
        return render_svg(path.read_bytes())
    with Image.open(path) as source:
        count = getattr(source, "n_frames", 1)
        if source.width * source.height > 16_000_000 or count > 256:
            raise ValueError("Use an image under 16 megapixels with at most 256 source frames")
        timings = []
        for index in range(count):
            source.seek(index)
            source.load()
            timings.append(max(40, min(5000, int(source.info.get("duration", 100)))))
        if count <= MAX_FRAMES:
            chosen, durations = list(range(count)), timings
        else:
            total = sum(timings)
            chosen = []
            for sample in range(MAX_FRAMES):
                target, cursor, index = sample * total / MAX_FRAMES, 0, 0
                while index < count - 1 and cursor + timings[index] <= target:
                    cursor += timings[index]
                    index += 1
                chosen.append(index)
            duration = max(40, min(5000, round(total / MAX_FRAMES)))
            durations = [duration] * MAX_FRAMES
        frames = []
        for index in chosen:
            source.seek(index)
            frames.append(source.convert("RGBA").copy())
        return encode_frames(frames, durations)


@lru_cache(maxsize=32)
def asset_from_json(data: str) -> IconAsset:
    return decode_asset(base64.b64decode(data, validate=True))


@lru_cache(maxsize=1)
def builtins() -> dict[str, dict[str, str]]:
    from .settings import resource_directory
    path = resource_directory() / "assets" / "icon-pack.json"
    return json.loads(path.read_text("utf-8")) if path.is_file() else {}


def builtin_icon(name: str) -> IconAsset | None:
    value = builtins().get(name)
    return asset_from_json(value["data"]) if value else None


def profile_assets(profiles: dict) -> dict[str, dict[str, str]]:
    result = {}
    for profile in profiles.values():
        for button in profile["buttons"]:
            if "icon_image" in button:
                value = validate_icon(button["icon_image"])
                if value["id"] in result and result[value["id"]] != value:
                    raise ValueError("Conflicting icon IDs")
                result[value["id"]] = value
    return result
