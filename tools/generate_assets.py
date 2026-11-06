"""Create the app's own vector-derived Windows icon; no downloaded artwork."""
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
destination = ROOT / "companion/assets"
destination.mkdir(parents=True, exist_ok=True)
image = Image.new("RGBA", (256, 256), (0, 0, 0, 0))
draw = ImageDraw.Draw(image)
draw.rounded_rectangle((8, 8, 248, 248), radius=56, fill="#FFFFFF", outline="#CBD2E0", width=5)
for row in range(2):
    for col in range(3):
        x, y = 48 + col * 60, 72 + row * 64
        draw.rounded_rectangle((x, y, x + 40, y + 40), radius=12,
                               fill=("#087AD8", "#E34C88", "#24A26E", "#ED8A3A", "#8860DD", "#2C99D4")[row * 3 + col])
image.save(destination / "nexus.ico", sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)])
image.save(destination / "nexus.png")
print("Created NEXUS DECK Windows icon")
