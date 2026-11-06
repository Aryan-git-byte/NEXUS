# Recorded verification: 1.2.0

Recorded on 6 November 2026 with ESP-IDF 5.5.4, LVGL 9.2.2 and 64-bit
Python 3.11 on Windows 11. The companion is installed and running. Firmware
1.2.0 is built but has **not** been uploaded: the ESP32's ROM download port and
BLE advertisements were unavailable during this verification.

## Firmware build

The final build, including the notification layout fixes, exited successfully:

```text
[18/20] Linking CXX executable nexus_deck.elf
Successfully created esp32s3 image.
nexus_deck.bin binary size 0x107f30 bytes.
Smallest app partition is 0x400000 bytes. 0x2f80d0 bytes (74%) free.
```

The application is 1,081,136 bytes. SHA-256:

```text
2fd5237e7c883bfcada80fa0c4a0e15e211dd4fe6dd587b12b35a1e17000afa6
```

`test-results/firmware-build-1.2.0.json` records the image hashes and validated
flash plan. The original LCD/touch pin configuration, display initialization,
RGB565 transport and calibration storage are unchanged. The new app preferences
use a separate `nexus_apps/prefs_v1` record. Compiling does not verify physical
touch, rendering, radio operation or persistence on the board.

## Windows regression tests

```text
120 passed in 3.16s
```

The suite retains the existing protocol, profile, editor, Windows input,
monitoring, pairing and calibration-preserving flash tests. New checks cover
the original PCM chime, disabled/unavailable audio fallback, boolean settings
migration, session-bound timer events, validation and duplicate suppression.
Windows input is intercepted in regression tests.

Results: `test-results/python-tests.xml`, with the machine hostname removed.

## Native local-app tests

The host build compiles the actual firmware model code. All five groups passed:

```text
PASS: 2048 merges, undo, stuck boards, bounds and 30 seeded games
PASS: Snake turns, tail movement, collisions, food and a full-board win
PASS: calculator arithmetic, percentages, repeated equals and error recovery
PASS: countdown deadlines, pause/resume, focus cycles, long break and stopwatch laps
PASS: pet pat reactions, care, slow decay, offline recovery and record validation
```

See `test-results/lvgl-preview/local-app-tests.log`. These checks execute real
game/calculator/timer code without accessing display, touch, NVS or BLE hardware.

## LVGL rendering and stylus interactions

The actual firmware UI was compiled against LVGL 9.2.2 and rendered into 56
portrait/landscape images. Checks exercised calculator keys, navigation,
Snake pause/resume, cat pats and visibility, timer completion, duration controls
and the four separate Settings dropdowns. Every local-app button fits its
viewport and has a target of at least 38 by 38 logical pixels.

```text
landscape: local app taps, navigation, cat pats, timer completion and modal targets passed
portrait: local app taps, navigation, cat pats, timer completion and modal targets passed
PASS: actual LVGL pages in both orientations; macro and media text do not overlap
PASS: notifications fit after rotation, expire, allow touch and stay off the idle clock
```

The clock checks require black backgrounds, white lettering, fitting bold
digits and the retained computer name. The final local-app layouts, duration
dialog and black clock were visually inspected in both orientations.

The layout log, native log, two clock images and two app contact sheets are
retained in `test-results/lvgl-preview/`. Other images, fixtures and compiler
logs are generated locally and ignored. All preview PC names and telemetry
values are explicitly labeled fixtures, not readings from the device.

## Packaged companion and background installation

PyInstaller built version 1.2.0 successfully. The packaged smoke test exited
with code 0 after opening the real editor, loading six profiles and eight
preview buttons, starting services and collecting two actual PC telemetry samples.
See `test-results/companion-smoke-1.2.0.json`.

The per-user installer then installed 1.2.0, preserved local configuration,
registered Start-menu/uninstall entries and started the background process.
Verification found exactly one running installed instance, matching EXE hashes,
`--background` arguments and the correct Startup-folder shortcut. The packaged
and installed timer WAV hashes match the source. A later Windows sign-in was
not simulated. Evidence: `test-results/companion-installed-1.2.0.json`.

The Windows `PlaySound` API accepted the chime. This establishes successful API
invocation; it does not establish that a person heard it or that a physical
timer event reached Windows over BLE.

## Physical status and remaining acceptance

The installed 1.2.0 companion starts normally and keeps attempting reconnection.
Its runtime log currently reports `Deck not found; power it on and enable Bluetooth`.
The available serial ports were COM1 and Bluetooth serial links, with no ESP32
USB download port. No writes were attempted to those unrelated ports.

The user was notified that BOOT/RESET is needed to expose the upload port.
Physical boot, touch, calibration persistence, game input, timer-to-PC audio and
encrypted reconnection with firmware 1.2.0 remain unverified until it is flashed
and exercised. The working 1.1.1 image remains the last verified upload.

The [1.1.1 baseline record](TESTING_1.1.1.md) documents the previous successful
COM17 upload, user-confirmed interface and black clock, encrypted handshake,
six synchronized profiles and five artwork assets. Its retained
`test-results/firmware-upload.json` describes **1.1.1**, not the new build.
