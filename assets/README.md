# Artwork and sound

NEXUS DECK's generated icons, logo and timer chime are original project assets
covered by the repository's MIT license. `tools/generate_assets.py` rebuilds
the companion icon assets. `tools/generate_timer_sound.py` synthesizes
`timer.wav` as 16-bit PCM; it contains no imported audio samples.

The pixel cat is drawn from a small code-defined sprite in
`firmware/main/ui/ui_pet.cpp`. Its image buffers and animation are generated
on the device, so no separate sprite sheet or font glyph is required.

See `fonts/README.md` and `fonts/OFL.txt` for the Montserrat font attribution
and redistribution terms. Imported user artwork stays in the user's local
configuration and is not licensed by this project. ESP-IDF, LVGL and the
Windows dependencies retain their own licenses.

## Logo and documentation images

`branding/logo.svg` is the scalable version of the existing six-color Windows
companion mark; `branding/logo.png` has a transparent background. README
banners and galleries live in `readme/`, alongside their source captures and
provenance manifest. They use actual LVGL/Qt renders, with explicit firmware
test fixtures, and do not depict physical hardware measurements.

`tools/generate_readme_assets.py` rebuilds those compositions with Pillow.
See [image provenance and regeneration](readme/README.md) for the source tools,
fonts, and capture-refresh commands.
