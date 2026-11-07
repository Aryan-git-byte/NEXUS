# Build, install and pair

## ESP32-S3 firmware

Use ESP-IDF **5.5.x** (validated here with **5.5.4**). The component manager fetches
the pinned **LVGL 9.2.2** dependency. `firmware/dependencies.lock` records it.
Install Espressif's ESP-IDF tools and open its PowerShell terminal, then:

```powershell
cd C:\path\to\NEXUS-DECK
idf.py -C firmware set-target esp32s3
idf.py -C firmware build
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_firmware.ps1 -Command flash -Port COM5
```

Replace the port with the board's actual supported download-mode port. Follow
the GPIO19/20 and GPIO43/44 sharing notes in [HARDWARE.md](HARDWARE.md). The helper
backs up the current flash and validates/preserves NVS before updating. Hold BOOT
and reset into ROM download mode when required by the board. A runtime console
is deliberately disabled; `idf.py monitor` will not supply application logs on
pins assigned to the display or touch controller.

A helper discovers a standard `C:\Espressif` installation, or accepts explicit
paths:

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_firmware.ps1 -Command build
powershell -ExecutionPolicy Bypass -File tools\build_firmware.ps1 -Command flash -Port COM5
```

You can pass `-IdfPath C:\your\esp-idf` and `-ToolsPath C:\your\tools`.
For the universal Xtensa compiler used on this machine, the verified command is:

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_firmware.ps1 -BuildDirectory build-direct -UseUniversalCompiler -DisableCompilerCache
```

The helper remembers the toolchain selected in an existing build directory and
restores its DLL search path on later runs. Use a separate build directory when
changing compilers.

The helper stores compiler output in `test-results/firmware-build.log` and uses
a component cache inside the repository. The normal build output is:

```text
firmware/build/nexus_deck.bin
firmware/build/bootloader/bootloader.bin
firmware/build/partition_table/partition-table.bin
firmware/build/ota_data_initial.bin
firmware/build/flash_args
```

First boot displays the calibration targets if there is no valid `touch/cal`
record. Finish calibration before opening the PC pairing dialog. Settings >
Orientation switches between logical portrait and landscape without changing
the ILI9488 initialization.

Do not change the parallel data mapping, COLMOD, or MADCTL to adapt a software
layout. Optional hardware changes are isolated in `config.hpp`.

### Wireless firmware updates

Install 1.3.0 over USB once using the backup-preserving helper. It adds OTA
metadata and two 4 MB update slots without moving calibration or artwork.
`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` is required; an old `sdkconfig` with
rollback disabled stops configuration with an explicit error. Enable it in
menuconfig or retain the project's default before rebuilding.

Build the next firmware normally. In the Windows editor, click **Firmware**,
choose the application `nexus_deck.bin`, and click **Upload over Bluetooth**.
Use the same build directory you compiled; `build-direct` is also supported.
The app validates the file, transfers it to the inactive slot and waits for a
verified reboot. Keep the deck powered. See [OTA.md](OTA.md) for cancellation,
rollback and USB recovery. A full partitioned/merged image is not an OTA file.

## Windows companion from source

Use 64-bit Python **3.11** or later on Windows 11 with a functioning BLE adapter.
No administrator session is needed for ordinary macros.

```powershell
cd C:\path\to\NEXUS-DECK
py -3.11 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r companion\requirements.txt
.\.venv\Scripts\python.exe -m companion.main
```

For the exact tested dependency environment, use
`companion/requirements-lock.txt`. The companion creates its own settings and
editable profile files in `%LOCALAPPDATA%\NexusDeck`.

Close the editor to keep the app in the notification area. Double-click the tray
icon to reopen it. The tray has Connect, Disconnect, Sync, and Quit commands.
Use `--background` to start there, or `--no-tray` to exit when the window closes.
Automatic reconnection is enabled by default. It can be changed in Connection.

## Pairing

1. Finish deck touch calibration and open Settings > Pair new PC. A new device
   opens a 120-second enrollment window at first boot as well.
2. In the Windows editor, open Connection and Discover nearby decks. Select
   NEXUS DECK, then Save and connect.
3. Enter the six-digit passkey displayed on the deck in the Windows pairing UI.
   Pairing requires authenticated LE Secure Connections and a 128-bit key.
4. Profiles sync automatically after connection. The status becomes
   `Profiles synchronized · encrypted connection` and PC metrics begin updating.

The deck saves one authorized PC identity. Other PCs are rejected. To change PCs,
use Settings > Forget saved PC, and remove the old Windows Bluetooth association.
Re-pair through a new local enrollment window. A stale bond is never silently
deleted by an incoming pairing request.

When upgrading from the original HID firmware, remove its old Windows pairing
and use the new device's Forget saved PC control to clear legacy bond records.
This keeps the existing touch calibration. Avoid Factory reset if you want to
retain calibration and layouts.

## Configure development actions

The shipped development profile includes VS Code, Terminal, PowerShell, Explorer,
GitHub, Open project, Dev server and Screenshot.

VS Code uses `Code.exe` from a standard user/system installation. Terminal uses
`wt.exe`, including its WindowsApps alias when that folder is absent from PATH.
Select an absolute executable in the button editor for other locations.
Open project uses the Project folder configured in Connection.

Dev server requires an explicitly configured `.exe`, project folder, and argument
list. For Vite using Node, choose `node.exe`, then put the actual Vite JavaScript
file and `--host` on separate argument lines. For a Python server, choose the
appropriate `python.exe` and configure `-m`, `http.server`, and a port on separate
lines. Arguments are passed directly to the configured executable; no shell is
invoked. A missing executable or project folder produces visible error feedback.

Screenshot opens Windows Snipping Tool with Win+Shift+S. Select the desired
region on the PC. Keyboard shortcuts and approved text are sent to the current
foreground application.

## Package the companion

```powershell
.\.venv\Scripts\python.exe -m pip install -r companion\requirements-dev.txt
.\.venv\Scripts\python.exe tools\generate_assets.py
.\.venv\Scripts\python.exe -m PyInstaller companion\NexusDeck.spec --noconfirm
```

The portable application is `dist\NexusDeck\NexusDeck.exe`. Copy the **entire**
NexusDeck folder, including `_internal`, to the destination PC. It does not
require Python there. User settings remain in LocalAppData and survive replacing
the app folder. The bundle includes Qt, BLE/WinRT and audio dependencies.
The original timer chime is already committed as `assets/timer.wav`. To
regenerate it, run `.\.venv\Scripts\python.exe tools\generate_timer_sound.py`
before packaging. Timer audio respects Windows master volume and mute.

Install the packaged application from the intended Windows account:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\install_companion.ps1
```

It installs in `%LOCALAPPDATA%\Programs\NexusDeck\<version>`, creates a Start-menu
shortcut, starts in the tray and adds a Startup-folder shortcut with
`--background`. It also registers **NEXUS DECK** in Windows Settings > Apps with
an uninstaller. No administrator account is required. Use `-NoStartup` to skip
sign-in startup, `-NoLaunch` to install without launching, or `-SourceDirectory`
to select another complete packaged folder.

Updates stage and verify the new copy before switching shortcuts. Current
versions can save an editor draft and exit through `NexusDeck.exe --quit`.
Before replacing an older source/portable run, save its draft and choose Quit
from its tray. The installer retains old version folders for recovery. It does
not overwrite `%LOCALAPPDATA%\NexusDeck` or remove the Windows Bluetooth bond.

Uninstall through Windows Settings > Apps, or run the installed
`%LOCALAPPDATA%\Programs\NexusDeck\uninstall.ps1`. The uninstaller removes the
program and its startup shortcuts while retaining user profiles and settings.

The packaging spec excludes unversioned ICU DLLs collected from unrelated tools
on PATH. Qt 6.12 uses Windows 11's system ICU API; a foreign `icu.dll` can prevent
the packaged editor from loading. Keep this exclusion when rebuilding.

## Verification commands

```powershell
.\.venv\Scripts\python.exe -m companion.main --diagnose
.\.venv\Scripts\python.exe -m companion.main --smoke-test --data-dir test-results\smoke-data
.\.venv\Scripts\python.exe -m pytest --junitxml test-results\python-tests.xml
```

Diagnostics collect real PC metrics. Smoke testing starts the real editor and
services, pauses automatic BLE connections, and exits without running macros.
The packaged executable supports the same checks and saves JSON results in
the selected `--data-dir`:

```powershell
.\dist\NexusDeck\NexusDeck.exe --smoke-test --data-dir C:\temp\nexus-smoke
```

Optional firmware UI verification compiles the actual LVGL pages for the PC and
runs the native calculator/game/timer/pet tests, then renders both orientations,
the local apps, every settings dropdown and the idle clock:

```powershell
.\.venv\Scripts\python.exe -m pip install ziglang==0.13.0
.\.venv\Scripts\python.exe tools\render_firmware.py
```

This renderer uses explicitly labeled hardware/telemetry fixtures. It verifies
layout geometry, colors, fonts, stylus target sizes and actual LVGL event
callbacks. It checks notification placement after rotation and across sleep.
It does not access the ESP32 or radio. Native test output is recorded in
`test-results/lvgl-preview/local-app-tests.log`; screen output and layout checks
are in the same directory.
The generated clock font C sources are already included in the repository;
firmware builds need no font converter. To regenerate them after changing the
licensed Montserrat source, run `tools/build_clock_fonts.py`.

See [TESTING.md](TESTING.md) for the recorded results and physical acceptance
checks that must be performed with the wired board.
