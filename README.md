# NEXUS DECK

A wireless touchscreen macro controller for Windows 11, with an ESP32-S3,
ILI9488 parallel TFT, calibrated XPT2046 touch, LVGL 9 and encrypted BLE.

Version **1.3.1** includes the ESP-IDF firmware, Windows companion,
stylus-friendly local apps, Bluetooth firmware updates and reset diagnostics.
The connected deck still runs the verified 1.3.0 image: its diagnostic update
was interrupted before boot selection. See [verification](docs/TESTING.md) for
the current build, installation and physical upload status.

## What is included

- Responsive light UI with colorful tiles: dashboard, development, media,
  system monitor, custom macros, settings and touch calibration. The idle clock
  has a black background and large bold white time, day, date and computer name.
- Original 16-bit GPIO bus and ILI9488 initialization, RGB565 partial rendering,
  portrait/landscape layouts, independent touch sampling and persistent calibration.
- Custom NimBLE GATT service with authenticated pairing, one authorized PC,
  fragmentation, CRC checks, bounded queues, replies, heartbeat and reconnects.
- Windows companion with an editor, tray, validated action registry, keyboard
  shortcuts, app launching, media/audio controls, screenshots and live PC telemetry.
- Editable Default, Development, Media, Gaming, Productivity and Custom profiles,
  color/animated icon import, tap/long-press actions, ordering and BLE synchronization.
- An offline Apps hub with a calculator, 2048, Snake, countdown/focus/stopwatch
  timers and a pixel cat. Games use large tap arrows. The cat follows your pages,
  reacts to pats, and has saved care settings and high scores.
- Timer completion chimes and notifications on Windows, with device and companion
  sound controls. Timers keep running while you navigate or use the clock screen.
- Wireless firmware uploads through the companion's Firmware window, with image
  validation, progress, cancellation, verified reboot and automatic boot rollback.
- Actual reset cause, boot identity and uptime in Setup and the companion when
  diagnostic firmware is installed; transfer failures report their phase and byte count.

CPU/RAM/disk/network readings come from the actual PC. Unsupported GPU,
temperature, media and battery fields are shown as unavailable. Project and dev
server buttons use the local paths configured in the editor's Connection dialog.

## Compile and flash

Use ESP-IDF **5.5.x**; this project was built with **5.5.4** and pinned **LVGL 9.2.2**.

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_firmware.ps1 -Command build
powershell -ExecutionPolicy Bypass -File tools\build_firmware.ps1 -Command flash -Port COM17
```

Replace COM17 with your board's download port. The flash helper backs up the board,
validates its partition table and preserves calibration/settings during updates.
For the universal Xtensa compiler workaround used on this machine:

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_firmware.ps1 -Command build -DisableCompilerCache -UseUniversalCompiler -BuildDirectory build-direct
```

Keep the supplied pin mapping. GPIO19/20 are LCD data pins and native USB pins;
GPIO43/44 are touch pins and UART0 pins. Runtime consoles and the PHY USB
workaround are disabled. Native USB can disappear when the application starts.
Use BOOT/RESET to enter ROM download mode if required by your board.

After installing 1.3.0 over USB once, open **Firmware** in the Windows editor,
choose `firmware/build/nexus_deck.bin` (or `build-direct/nexus_deck.bin`), and
click **Upload over Bluetooth**. Keep the deck powered through the verified
restart. No Wi-Fi setup or USB upload mode is needed for subsequent updates.
The OTA layout requires **16 MB flash**. See [wireless updates](docs/OTA.md).

## Run the Windows companion

Use 64-bit Python 3.11 or later on Windows 11:

```powershell
py -3.11 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r companion\requirements.txt
.\.venv\Scripts\python.exe -m companion.main
```

Finish touch calibration, open the deck's **Settings > Pair new PC**, then use
**Connection > Discover nearby decks > Save and connect** in the editor. Enter
the six-digit passkey from the deck. Saved profiles sync after the secure link
is established. Closing the editor leaves the companion running in the tray.

To install a built package and start it in the tray at every sign-in:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\install_companion.ps1
```

The installer uses `%LOCALAPPDATA%\Programs\NexusDeck`, adds a Start-menu entry
and registers an uninstaller. Closing the editor keeps the app in the tray;
choose **Quit** there to stop it. `-NoStartup` disables sign-in startup during
installation. Local configurations live in `%LOCALAPPDATA%\NexusDeck` and survive
updates and uninstalling.

The portable build is `dist\NexusDeck\NexusDeck.exe`; copy its whole folder,
including `_internal`. Packaging instructions are in [BUILD.md](docs/BUILD.md).

Icons can be PNG, JPEG, BMP, SVG, GIF, APNG or WebP. The editor preserves color
and transparency, scales artwork to 32 × 32 pixels and retains up to 16 animation
frames. Imported artwork is synchronized separately from the profile layout.

## Documentation

- [Build, installation, pairing and packaging](docs/BUILD.md)
- [Hardware, preserved references and pin conflicts](docs/HARDWARE.md)
- [Firmware and companion architecture](docs/ARCHITECTURE.md)
- [BLE service, framing and validation](docs/BLUETOOTH_PROTOCOL.md)
- [Troubleshooting and recovery](docs/TROUBLESHOOTING.md)
- [Recorded verification and hardware acceptance checks](docs/TESTING.md)
- [Local apps, games, timers and pixel cat](docs/APPS.md)
- [Release changes](CHANGELOG.md)
- [Wireless firmware uploads and recovery](docs/OTA.md)

`firmware/` is the ESP-IDF project. `companion/` is the Python application.
`profiles/` contains the six source profiles and generated device presentation
bundle. `tests/` covers protocol, validation, dispatch, monitoring, editing,
pairing and flash migration. `references/original/` preserves the existing
hardware implementation with a SHA-256 manifest.
