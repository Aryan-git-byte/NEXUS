"""Compose README artwork from published, unmodified product UI renders.

Pillow is already included in the companion dependencies. The default run uses
the checked-in screen captures; --refresh-screens imports new host-rendered
captures without starting Bluetooth, executing macros, or accessing a deck.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont, PngImagePlugin

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / "assets/readme"
SCREENS = ART / "screens"
BRAND = ROOT / "assets/branding"
BACKGROUND = "#F7F9FC"
INK = "#18202D"
MUTED = "#627087"
BLUE = "#087AD8"
BORDER = "#DCE3EE"
COLORS = ("#087AD8", "#E34C88", "#24A26E", "#ED8A3A", "#8860DD", "#2C99D4")
FIRMWARE_SCREENS = {
    "home": "home-landscape.png",
    "development": "development-landscape.png",
    "clock": "clock-landscape.png",
    "calculator": "calculator-landscape.png",
    "2048": "2048-landscape.png",
    "snake": "snake-landscape.png",
    "timer": "timer-landscape.png",
    "focus": "focus-landscape.png",
    "pet": "pet-pat-landscape.png",
}


def font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    windows_fonts = Path(os.environ.get("SystemRoot", "C:/Windows")) / "Fonts"
    candidates = (
        [ROOT / "assets/fonts/Montserrat-Bold.ttf", windows_fonts / "segoeuib.ttf"]
        if bold else [windows_fonts / "segoeui.ttf"]
    )
    for candidate in candidates:
        if candidate.is_file():
            return ImageFont.truetype(str(candidate), size)
    try:
        return ImageFont.truetype("DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf", size)
    except OSError:
        return ImageFont.load_default(size=size)


def text(canvas: Image.Image, position: tuple[int, int], value: str,
         size: int, color: str = INK, bold: bool = False) -> None:
    ImageDraw.Draw(canvas).text(position, value, font=font(size, bold), fill=color)


def save(canvas: Image.Image, path: Path, description: str) -> None:
    metadata = PngImagePlugin.PngInfo()
    metadata.add_text("Description", description)
    metadata.add_text("Project", "NEXUS DECK")
    canvas.save(path, optimize=True, pnginfo=metadata)


def mark(canvas: Image.Image, x: int, y: int, size: int) -> None:
    draw = ImageDraw.Draw(canvas)
    scale = size / 256
    def box(values: tuple[int, int, int, int]) -> tuple[int, int, int, int]:
        return tuple(round(value * scale) + (x if index % 2 == 0 else y)
                     for index, value in enumerate(values))
    draw.rounded_rectangle(box((8, 8, 248, 248)), round(56 * scale),
                           fill="white", outline="#CBD2E0", width=max(1, round(5 * scale)))
    for index, color in enumerate(COLORS):
        left, top = 48 + index % 3 * 60, 72 + index // 3 * 64
        draw.rounded_rectangle(box((left, top, left + 40, top + 40)),
                               round(12 * scale), fill=color)


def card(canvas: Image.Image, box: tuple[int, int, int, int], radius: int = 24) -> None:
    shadow = Image.new("RGBA", canvas.size)
    left, top, right, bottom = box
    ImageDraw.Draw(shadow).rounded_rectangle((left, top + 12, right, bottom + 12),
                                             radius, fill=(27, 46, 79, 24))
    canvas.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(18)))
    ImageDraw.Draw(canvas).rounded_rectangle(box, radius, fill="white", outline=BORDER, width=2)


def screen(canvas: Image.Image, name: str, x: int, y: int, width: int) -> int:
    with Image.open(SCREENS / f"{name}.png") as source:
        height = round(source.height * width / source.width)
        capture = source.convert("RGBA").resize((width, height), Image.Resampling.LANCZOS)
    canvas.alpha_composite(capture, (x, y))
    ImageDraw.Draw(canvas).rectangle((x - 1, y - 1, x + width, y + height),
                                    outline=BORDER, width=1)
    return height


def pill(canvas: Image.Image, x: int, y: int, value: str) -> int:
    draw = ImageDraw.Draw(canvas)
    width = round(draw.textlength(value, font=font(19))) + 34
    draw.rounded_rectangle((x, y, x + width, y + 44), 14, fill="white", outline=BORDER, width=2)
    text(canvas, (x + 17, y + 8), value, 19)
    return width


def brand_assets() -> None:
    tile_elements = []
    for index, color in enumerate(COLORS):
        x, y = 48 + index % 3 * 60, 72 + index // 3 * 64
        tile_elements.append(f'  <rect x="{x}" y="{y}" width="40" height="40" rx="12" fill="{color}"/>')
    svg = '\n'.join([
        '<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256" role="img" aria-labelledby="title desc">',
        '  <title id="title">NEXUS DECK</title>',
        '  <desc id="desc">Six colorful shortcut tiles in a rounded white square.</desc>',
        '  <rect x="8" y="8" width="240" height="240" rx="56" fill="#FFFFFF" stroke="#CBD2E0" stroke-width="5"/>',
        *tile_elements,
        '</svg>', '',
    ])
    (BRAND / "logo.svg").write_text(svg, encoding="utf-8", newline="\n")
    logo = Image.new("RGBA", (512, 512))
    mark(logo, 0, 0, 512)
    save(logo, BRAND / "logo.png", "Original NEXUS DECK six-tile brand mark; transparent background.")


def hero() -> None:
    canvas = Image.new("RGBA", (1600, 820), BACKGROUND)
    mark(canvas, 72, 65, 90)
    text(canvas, (185, 81), "NEXUS DECK", 34, bold=True)
    text(canvas, (76, 242), "YOUR DESK, AT A TAP.", 20, BLUE, True)
    text(canvas, (72, 289), "Your shortcuts.", 66, bold=True)
    text(canvas, (72, 370), "One touch.", 66, BLUE, True)
    text(canvas, (76, 484), "A wireless touch deck for Windows.", 24)
    text(canvas, (76, 526), "Work, listen, focus — and take a break.", 23, MUTED)
    x = 76
    for label in ("ESP32-S3", "Windows 11", "BLE + OTA"):
        x += pill(canvas, x, 602, label) + 12
    text(canvas, (76, 728), "16-bit parallel TFT · calibrated touch", 21, MUTED)
    card(canvas, (728, 164, 1528, 764), 28)
    text(canvas, (768, 190), "THE FIRMWARE INTERFACE", 18, MUTED, True)
    screen(canvas, "development", 768, 236, 720)
    text(canvas, (768, 732), "Actual LVGL render · synthetic PC data", 16, MUTED)
    save(canvas.convert("RGB"), ART / "hero.png",
         "NEXUS DECK brand banner with an actual LVGL development-page render. PC data is synthetic; this is not a hardware photograph.")


def controls() -> None:
    canvas = Image.new("RGBA", (1440, 720), BACKGROUND)
    text(canvas, (64, 40), "Bright controls. A quiet clock.", 40, bold=True)
    text(canvas, (64, 102), "Readable at a glance, in landscape or portrait.", 23, MUTED)
    for x, name, title in ((64, "home", "SHORTCUTS & PC STATUS"),
                           (752, "clock", "THE IDLE SCREEN")):
        card(canvas, (x, 167, x + 624, 636), 22)
        text(canvas, (x + 24, 188), title, 17, MUTED, True)
        screen(canvas, name, x + 24, 230, 576)
    text(canvas, (64, 674), "Actual firmware renders · time and PC readings use preview fixtures", 18, MUTED)
    save(canvas.convert("RGB"), ART / "controls.png",
         "Actual LVGL dashboard and black idle-clock renders. Values are preview fixtures, not hardware measurements.")


def local_apps() -> None:
    canvas = Image.new("RGBA", (1440, 902), BACKGROUND)
    text(canvas, (64, 40), "A little more than shortcuts.", 40, bold=True)
    text(canvas, (64, 102), "Offline tools. One stylus. Your pixel cat.", 23, MUTED)
    items = (("calculator", "CALCULATOR"), ("2048", "2048"), ("snake", "SNAKE"),
             ("timer", "COUNTDOWN"), ("focus", "FOCUS TIMER"), ("pet", "PIXEL CAT"))
    for index, (name, title) in enumerate(items):
        x = 64 + index % 3 * 448
        y = 167 + index // 3 * 340
        card(canvas, (x, y, x + 416, y + 316), 20)
        text(canvas, (x + 16, y + 19), title, 17, MUTED, True)
        screen(canvas, name, x + 16, y + 53, 384)
    text(canvas, (64, 856), "Actual LVGL app renders · local interactions and synthetic PC data", 18, MUTED)
    save(canvas.convert("RGB"), ART / "local-apps.png",
         "Actual firmware renders of calculator, 2048, Snake, countdown, focus timer and pixel cat. PC data is synthetic.")


def refresh_screens(firmware_directory: Path, editor_directory: Path) -> None:
    sources = {name: firmware_directory / filename for name, filename in FIRMWARE_SCREENS.items()}
    sources["companion"] = editor_directory / "development-landscape.png"
    missing = [str(path) for path in sources.values() if not path.is_file()]
    if missing:
        raise SystemExit("Render the product interfaces first. Missing capture(s):\n" + "\n".join(missing))
    revision = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, check=True,
                              capture_output=True, text=True).stdout.strip()
    records = []
    for name, source in sources.items():
        with Image.open(source) as capture:
            if name != "companion" and capture.size != (480, 320):
                raise SystemExit(f"Expected a 480 × 320 firmware render: {source}")
            image = capture.convert("RGB")
        destination = SCREENS / f"{name}.png"
        description = ("Actual Qt editor render; local profiles and real PC metrics; no deck connected."
                       if name == "companion" else "Actual LVGL firmware render; synthetic PC/clock fixtures; no physical hardware accessed.")
        save(image, destination, description)
        records.append({
            "file": destination.relative_to(ROOT).as_posix(),
            "source_tool": "tools/render_editor.py" if name == "companion" else "tools/render_firmware.py",
            "source_capture": source.name,
            "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
            "published_sha256": hashlib.sha256(destination.read_bytes()).hexdigest(),
            "width": image.width, "height": image.height,
            "description": description,
        })
    (ART / "screens.json").write_text(json.dumps({
        "source_revision": revision,
        "processing": "Original RGB pixels retained; PNG metadata replaced with provenance. No UI values were retouched.",
        "screens": records,
    }, indent=2) + "\n", encoding="utf-8", newline="\n")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--refresh-screens", action="store_true")
    parser.add_argument("--firmware-dir", type=Path, default=ROOT / "test-results/lvgl-preview")
    parser.add_argument("--editor-dir", type=Path, default=ROOT / "test-results/editor-preview")
    args = parser.parse_args()
    for directory in (ART, SCREENS, BRAND):
        directory.mkdir(parents=True, exist_ok=True)
    if args.refresh_screens:
        refresh_screens(args.firmware_dir.resolve(), args.editor_dir.resolve())
    missing = [name for name in (*FIRMWARE_SCREENS, "companion") if not (SCREENS / f"{name}.png").is_file()]
    if missing:
        parser.error("Published captures are missing: " + ", ".join(missing))
    brand_assets()
    hero()
    controls()
    local_apps()
    print("Generated logo.svg, logo.png, hero.png, controls.png and local-apps.png from product UI captures.")


if __name__ == "__main__":
    main()
