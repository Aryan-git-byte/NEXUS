# Third-party components

New NEXUS DECK application code is MIT licensed. The supplied original hardware
reference is preserved separately with its original contents and authorship.
Third-party code keeps its own license.

* ESP-IDF 5.5.x and NimBLE: their SDK Apache-2.0 and bundled component licenses.
* LVGL 9.2.2: MIT, with bundled font/symbol licenses in its component distribution.
* PySide6/Qt: LGPL/GPL/commercial options supplied by Qt. This app distributes
  dynamically linked PySide6 libraries. Its complete source and packaging spec
  are included so the application can be rebuilt with compatible libraries.
* Bleak: MIT. psutil: BSD-3-Clause. pycaw: MIT. comtypes: MIT.
* Pillow, NVIDIA management bindings, PyWinRT and PyInstaller retain the licenses
  in their installed distributions. PyInstaller's bootloader distribution
  exception permits bundling the application.
* The optional Zig test compiler is a development dependency, not a firmware or
  companion runtime dependency.
* Montserrat Bold and the generated clock font data use SIL Open Font License
  1.1. The source font and full license are in `assets/fonts/`; generated C stays
  in flash and requires no runtime font converter.

When redistributing packaged binaries, retain the dependency licenses included
under `_internal` and consult the installed package metadata for full texts.
`companion/requirements-lock.txt` records the environment used for local
validation and packaging.
