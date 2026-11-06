The firmware uses LVGL's bundled Montserrat fonts at 14, 16, 20, 28 and 40 pixels,
with LVGL's bundled symbol glyphs. They are enabled in `firmware/sdkconfig.defaults`.
The pinned `lvgl/lvgl` component supplies the font source and licenses at build time.
The black sleep screen uses Montserrat Bold: 112px clock digits in landscape,
80px in portrait, and 22px day/date/computer labels. The large fonts contain only
clock characters and have tabular advances so the clock stays centered.
`tools/build_clock_fonts.py` generates the committed LVGL C data using Pillow.
The source `Montserrat-Bold.ttf` comes from LVGL 9.2.2's multilang demo and is
licensed under SIL OFL 1.1; retain `OFL.txt` when redistributing it or its subsets.

The Windows editor uses the operating system's Segoe UI font.
