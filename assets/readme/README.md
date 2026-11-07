# README visuals

The README uses the project's own logo and the real product interfaces.
The banner and galleries frame screen renders; they do not depict a physical
enclosure or claim a measured Bluetooth connection.

## Provenance

- `screens/` contains actual LVGL firmware renders and an actual Qt editor
  render. Their original RGB pixels are preserved; metadata contains only
  project provenance.
- Firmware captures come from `tools/render_firmware.py`. The PC name, clock,
  telemetry, media, and radio state are explicit host-test fixtures.
- The Windows capture comes from `tools/render_editor.py`, using default local
  profiles and real local PC metrics. Automatic BLE connections are disabled
  during capture.
- `screens.json` records the source revision, tool, image dimensions, and
  hashes of each source and published capture. Refreshing documentation
  screenshots does not constitute physical hardware verification.
- `hero.png`, `controls.png`, and `local-apps.png` are compositions of those
  captures. Captions remain visible in the images when shared separately.

The six-color logo matches the existing Windows companion mark. Its scalable
source and transparent PNG live in `../branding/`. No downloaded stock photos,
AI-generated device photographs, or user-uploaded artwork are included.

## Rebuild the artwork

From the repository root, with the companion dependencies installed:

```powershell
.\.venv\Scripts\python.exe tools\generate_readme_assets.py
```

This uses the checked-in screen captures and does not need a board, a radio,
or a running Windows companion. Pillow is already a companion dependency.
Headings use the bundled Montserrat Bold font; body text uses Segoe UI on
Windows, with a local font fallback on other systems. Those fallbacks may
change text metrics; the checked-in PNGs provide consistent GitHub rendering.

To replace captures after changing the product UI:

```powershell
.\.venv\Scripts\python.exe tools\render_firmware.py
.\.venv\Scripts\python.exe tools\render_editor.py
.\.venv\Scripts\python.exe tools\generate_readme_assets.py --refresh-screens
```

The firmware renderer requires the optional native toolchain described in
[BUILD.md](../../docs/BUILD.md#verification-commands). Inspect all generated
images and keep fixture captions intact before publishing them. Raw renderer
logs and local data directories remain excluded by `.gitignore`.

Original compositions and logo artwork follow the repository's
[MIT license](../../LICENSE); Montserrat retains its
[OFL attribution](../fonts/OFL.txt).
