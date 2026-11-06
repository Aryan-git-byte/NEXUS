"""Build the original color icon pack for Qt and the ESP32 from the same SVGs."""
from __future__ import annotations

import json
import os
import sys
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from PySide6.QtGui import QGuiApplication
from companion.icons import encode_frames, render_svg
from companion.profiles import ICONS


def svg(color: str, body: str) -> str:
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64">'
            f'<rect x="4" y="4" width="56" height="56" rx="15" fill="{color}"/>'
            f'{body}</svg>\n')


# Original, geometric artwork. Paths avoid dependencies on PC fonts or network assets.
ART = {
    "code": ("#087AD8", '<path d="m25 20-13 12 13 12m14-24 13 12-13 12m-4-28-7 32" fill="none" stroke="white" stroke-width="5" stroke-linecap="round" stroke-linejoin="round"/>'),
    "terminal": ("#263D56", '<path d="m15 21 11 11-11 11m18 0h15" fill="none" stroke="#90F3CF" stroke-width="5" stroke-linecap="round" stroke-linejoin="round"/>'),
    "powershell": ("#3A64D9", '<path d="m17 19 14 12-17 12m18 1h14" fill="none" stroke="white" stroke-width="5" stroke-linecap="round" stroke-linejoin="round"/>'),
    "folder": ("#FFBB34", '<path d="M12 21a4 4 0 0 1 4-4h12l5 6h15a4 4 0 0 1 4 4v18a4 4 0 0 1-4 4H16a4 4 0 0 1-4-4Z" fill="#FFF2C0"/><path d="M12 28h40v17a4 4 0 0 1-4 4H16a4 4 0 0 1-4-4Z" fill="white"/>'),
    "github": ("#36445C", '<path d="M17 25 16 14l11 6a25 25 0 0 1 10 0l11-6-1 11c3 3 4 6 4 10 0 11-9 15-19 15S13 46 13 35c0-4 1-7 4-10Z" fill="white"/><ellipse cx="24" cy="35" rx="3" ry="4" fill="#36445C"/><ellipse cx="40" cy="35" rx="3" ry="4" fill="#36445C"/><path d="M29 43h6" stroke="#36445C" stroke-width="3" stroke-linecap="round"/>'),
    "project": ("#3E90E6", '<path d="M18 13h19l11 11v24a4 4 0 0 1-4 4H20a4 4 0 0 1-4-4V17a4 4 0 0 1 2-4Z" fill="white"/><path d="M36 13v13h12" fill="#A6D7FF"/><path d="m26 32-5 5 5 5m12-10 5 5-5 5" fill="none" stroke="#2477CE" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"/>'),
    "server": ("#15956F", ''),
    "camera": ("#EA536E", '<path d="M13 24h10l4-6h10l4 6h10v25H13Z" fill="white"/><circle cx="32" cy="36" r="10" fill="#C93458"/><circle cx="32" cy="36" r="6" fill="#FFD8E3"/><circle cx="46" cy="29" r="2" fill="#C93458"/>'),
    "play": ("#E34C88", '<path d="m21 16 20 16-20 16Z" fill="white"/><path d="M46 20v24" stroke="#FFE0ED" stroke-width="4" stroke-linecap="round"/>'),
    "next": ("#7863DB", '<path d="m16 18 22 14-22 14Z" fill="white"/><path d="M45 19v26" stroke="white" stroke-width="5" stroke-linecap="round"/>'),
    "previous": ("#7863DB", '<path d="m48 18-22 14 22 14Z" fill="white"/><path d="M19 19v26" stroke="white" stroke-width="5" stroke-linecap="round"/>'),
    "volume_up": ("#16976A", '<path d="M13 25h9l11-9v32l-11-9h-9Z" fill="white"/><path d="M41 24a11 11 0 0 1 0 16m6-22a20 20 0 0 1 0 28" fill="none" stroke="white" stroke-width="3.5" stroke-linecap="round"/>'),
    "volume_down": ("#168EA9", '<path d="M15 25h9l11-9v32l-11-9h-9Z" fill="white"/><path d="M43 26a9 9 0 0 1 0 12" fill="none" stroke="white" stroke-width="3.5" stroke-linecap="round"/>'),
    "mute": ("#EA853B", '<path d="M12 25h9l10-9v32l-10-9h-9Z" fill="white"/><path d="m40 25 12 14m0-14L40 39" stroke="white" stroke-width="4" stroke-linecap="round"/>'),
    "copy": ("#368BE6", '<rect x="13" y="13" width="27" height="32" rx="5" fill="#B9DFFF"/><rect x="24" y="23" width="27" height="32" rx="5" fill="white"/><path d="M30 32h15m-15 7h15m-15 7h10" stroke="#368BE6" stroke-width="2.5" stroke-linecap="round"/>'),
    "paste": ("#24A26E", '<rect x="15" y="16" width="34" height="38" rx="5" fill="white"/><rect x="24" y="12" width="16" height="10" rx="4" fill="#AFF1CB"/><path d="m24 37 6 6 13-14" stroke="#168854" stroke-width="4" fill="none" stroke-linecap="round" stroke-linejoin="round"/>'),
    "cut": ("#329FAE", '<circle cx="19" cy="43" r="7" stroke="white" stroke-width="4" fill="none"/><circle cx="45" cy="43" r="7" stroke="white" stroke-width="4" fill="none"/><path d="m24 38 22-23m-6 23L18 15" stroke="white" stroke-width="4" stroke-linecap="round"/>'),
    "undo": ("#9565D4", '<path d="M19 28h17a13 13 0 0 1 0 26m-5-39L17 28l14 13" fill="none" stroke="white" stroke-width="5" stroke-linecap="round" stroke-linejoin="round"/>'),
    "save": ("#5381DF", '<path d="M16 13h28l8 8v30H12V17Z" fill="white"/><path d="M23 13h17v15H23Z" fill="#B7D3FF"/><rect x="21" y="36" width="22" height="15" rx="3" fill="#5381DF"/><path d="M27 42h10" stroke="white" stroke-width="2.5" stroke-linecap="round"/>'),
    "desktop": ("#3497CC", '<rect x="11" y="15" width="42" height="29" rx="5" fill="white"/><path d="M16 20h32v19H16Z" fill="#A3E3F6"/><path d="M25 51h14m-7-7v7" stroke="white" stroke-width="4" stroke-linecap="round"/>'),
    "lock": ("#EBA91F", '<path d="M22 29v-8a10 10 0 0 1 20 0v8" fill="none" stroke="white" stroke-width="4"/><rect x="16" y="27" width="32" height="26" rx="7" fill="white"/><circle cx="32" cy="38" r="4" fill="#C18012"/><path d="M32 40v6" stroke="#C18012" stroke-width="3"/>'),
    "browser": ("#2C99D4", '<circle cx="32" cy="32" r="21" fill="white"/><circle cx="32" cy="32" r="17" fill="#C1EAFF"/><path d="m26 26 17-5-5 17Z" fill="#F25B69"/><path d="m26 26-5 17 17-5Z" fill="#287FC5"/><circle cx="32" cy="32" r="3" fill="white"/>'),
    "text": ("#9864D4", '<path d="m12 45 11-28 11 28M16 36h14m18-6v15m0-12a7 7 0 1 0 0 9" fill="none" stroke="white" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/>'),
    "game": ("#8860DD", '<path d="M21 22h22c7 0 11 9 12 19s-5 13-11 5l-3-4H23l-3 4c-6 8-12 5-11-5s5-19 12-19Z" fill="white"/><path d="M22 27v12m-6-6h12" stroke="#7851C5" stroke-width="3.5" stroke-linecap="round"/><circle cx="44" cy="29" r="3" fill="#EC738E"/><circle cx="39" cy="35" r="3" fill="#F5B841"/>'),
    "mail": ("#ED8A3A", '<rect x="12" y="19" width="40" height="29" rx="5" fill="white"/><path d="m14 23 18 13 18-13" fill="none" stroke="#ED8A3A" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"/>'),
    "calendar": ("#EA5371", '<rect x="13" y="15" width="38" height="38" rx="6" fill="white"/><path d="M13 25h38M23 12v10m18-10v10" stroke="#FFB1C3" stroke-width="4" stroke-linecap="round"/><path d="M23 32h5v5h-5Zm13 0h5v5h-5ZM23 43h5v5h-5Zm13 0h5v5h-5Z" fill="#EA5371"/>'),
    "search": ("#379CDD", '<circle cx="28" cy="27" r="13" fill="none" stroke="white" stroke-width="5"/><path d="m38 38 13 13" stroke="white" stroke-width="6" stroke-linecap="round"/>'),
    "star": ("#EBAD22", '<path d="m32 12 6 13 15 2-11 11 3 15-13-7-13 7 3-15-11-11 15-2Z" fill="white"/>'),
    "custom": ("#6D7FDC", '<path d="M32 17v30M17 32h30" stroke="white" stroke-width="6" stroke-linecap="round"/>'),
}


def server_body(light: str) -> str:
    return (f'<rect x="13" y="16" width="38" height="14" rx="4" fill="white"/>'
            f'<rect x="13" y="35" width="38" height="14" rx="4" fill="white"/>'
            f'<path d="M21 23h13m-13 19h13" stroke="#15956F" stroke-width="3" stroke-linecap="round"/>'
            f'<circle cx="44" cy="23" r="3" fill="{light}"/>'
            f'<circle cx="44" cy="42" r="3" fill="#15956F"/>')


def write_changed(path: Path, data: str | bytes) -> None:
    payload = data.encode("utf-8") if isinstance(data, str) else data
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.is_file() or path.read_bytes() != payload:
        path.write_bytes(payload)


def main() -> None:
    application = QGuiApplication.instance() or QGuiApplication([])
    if set(ART) != set(ICONS):
        raise RuntimeError("The color pack must cover every selectable icon")
    assets = {}
    declarations = ["// Generated by tools/build_icons.py from assets/icons/*.svg.\n#pragma once\n",
                    '#include <cstddef>\n#include <cstdint>\nnamespace nexus::icons {\n']
    for name, (color, body) in ART.items():
        if name == "server":
            body = server_body("#15956F")
            frames = [render_svg(svg(color,server_body(light)).encode()).rgba()
                      for light in ("#15956F", "#FFBE38", "#15956F", "#A7DCCB")]
            asset = encode_frames(frames,[200,200,200,600])
            frames[0].save(ROOT / "assets/icons/animated-server.gif",save_all=True,
                           append_images=frames[1:],duration=list(asset.durations),loop=0)
        else:
            asset = render_svg(svg(color,body).encode())
        write_changed(ROOT / f"assets/icons/{name}.svg",svg(color,body))
        assets[name] = asset.json()
        data = asset.data
        rows = [",".join(f"0x{b:02x}" for b in data[i:i + 24]) for i in range(0,len(data),24)]
        variable = f"icon_{name}"
        declarations.append(f"inline constexpr uint8_t {variable}[] = {{\n" + ",\n".join(rows) + "\n};\n")
    declarations.append("struct BuiltinIcon { const char* name; const uint8_t* data; size_t size; };\n"
                        "inline constexpr BuiltinIcon builtins[] = {\n")
    declarations.extend(f'{{"{name}",icon_{name},sizeof(icon_{name})}},\n' for name in ART)
    declarations.append("};\n}\n")
    write_changed(ROOT / "firmware/main/assets/generated_icons.hpp","".join(declarations))
    write_changed(ROOT / "assets/icon-pack.json",json.dumps(assets,indent=2) + "\n")
    print(f"Built {len(assets)} color icons ({sum(len(__import__('base64').b64decode(v['data'])) for v in assets.values()):,} device bytes)")
    del application


if __name__ == "__main__":
    main()
