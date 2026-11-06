# Recorded verification

Results recorded on 6 November 2026, with ESP-IDF 5.5.4, LVGL 9.2.2,
64-bit Python 3.11 and the connected ESP32-S3 rev v0.2 (16 MB flash,
8 MB embedded PSRAM). The firmware does not require PSRAM.

## Firmware build and upload

Firmware 1.1.1 built successfully with the installed universal Xtensa compiler:

```text
[10/12] Linking CXX executable nexus_deck.elf
Successfully created esp32s3 image.
nexus_deck.bin binary size 0xe8130 bytes.
Smallest app partition is 0x400000 bytes. 0x317ed0 bytes (77%) free.
```

The upload on COM17 wrote the bootloader, partition table and 950,576-byte
application. Esptool reported `Hash of data verified` for all three images and
reset the board. Current NVS settings/calibration were retained. The original
application and the current flash layout were backed up before writes.

The retained upload summary is in `test-results/firmware-upload.json`. Compiler
and upload logs are generated locally and ignored by Git. Device backups are
also private because they may contain bond keys. The current application binary's
SHA-256 was checked against the actual flash plan and matches the uploaded image.

## Windows regression tests

```text
116 passed in 2.48s
```

The suite exercises UTF-8 fragmentation at minimum MTU, CRC and malformed
packets, ID/action/profile validation, duplicate command suppression, approved
text, Windows input construction, media/monitor fallbacks, fake-central
handshakes and synchronization, real Qt edit/save/reorder/reload flows,
authenticated passkey pairing callbacks and calibration-preserving flash plans.
It also covers imported color/animated artwork and the Windows Terminal alias
when a GUI/startup process has no WindowsApps directory in PATH.

Results: `test-results/python-tests.xml`. Windows input calls in regression
tests are intercepted; these tests do not send arbitrary shortcuts to the PC.

## Physical status

The user confirmed the light, colorful device interface and all four separate,
readable Setup dropdowns after the previous update. After uploading 1.1.1 and
resetting, they reported the NEXUS dashboard or new black clock. This confirms
that the updated application boots on the actual board.

Real Windows companion logs recorded the encrypted handshake with firmware
1.1.1, six synchronized profiles and five artwork assets. The connection and
synchronization resumed after the final installed app restart:

```text
2026-11-06 20:05:18 INFO NEXUS DECK companion 1.1.1 started in background
2026-11-06 20:05:28 INFO Encrypted BLE session established with firmware 1.1.1
2026-11-06 20:05:35 INFO Profiles synchronized: 6 profiles, 5 artwork assets
```

These are selected log messages, with millisecond precision and logger names
omitted. Full user runtime logs stay in LocalAppData; they are not source assets.

## LVGL layout and clock verification

The host renderer compiles the actual firmware UI against LVGL 9.2.2, using
separate hardware fixtures. It rendered 34 screen images and verified:

```text
landscape: four separate settings controls and expanded lists rendered
portrait: four separate settings controls and expanded lists rendered
PASS: actual LVGL pages in both orientations; macro and media text do not overlap
```

The clock checks assert a black background, white lettering, no overlapping
labels, fitting bold digits and a retained computer name. Both clock images
were visually inspected. The two clock images and `layout-checks.log` are
retained in `test-results/lvgl-preview/`; the other rendered pages and build
logs are generated locally and ignored by Git. Its test PC name and stress
metrics are layout fixtures, not physical telemetry.

## Packaged Windows app and installation

The final PyInstaller package opened the real editor with six profiles, eight
preview buttons, running background services and two actual PC telemetry samples.
Its smoke test exited successfully; see `test-results/companion-smoke-test.json`.

Installation was performed in the intended Windows account. The installed EXE
hash matches the tested package, exactly one installed instance was observed,
and its command line and verified Startup-folder shortcut use `--background`.
The installer registered the Start-menu and uninstall entries. Saved profile
edits, configuration and pairing were retained. Evidence is in
`test-results/companion-installed.json`. A later Windows sign-in was not simulated.

## Acceptance on the actual board

The recorded checks above establish the build/upload, physical boot, user-reported
UI readability, encrypted reconnection, artwork/profile synchronization and
installed background app startup. Orientation geometry and malformed-command
rejection are also covered by automated tests.

A dedicated physical audit of touch accuracy in both orientations, calibration
retention across power loss, every Windows macro/media command and offline
navigation has not been performed by the agent. Those observations should be
recorded separately when exercised. Host rendering and intercepted keyboard
tests do not substitute for those hardware/input checks.
