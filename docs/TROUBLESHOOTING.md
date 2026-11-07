# Troubleshooting

## A timer finishes without PC sound

Check the timer page's speaker toggle and **Connection > Timer sound** in the
Windows editor. **Test chime** verifies the installed tone. Windows volume/mute
applies. The companion must be running and the deck must show Connected.
Disconnected completions are shown locally and are not replayed on reconnect.
Missing audio uses tray notification and readable feedback.

## Games and cat

Use the arrow buttons with one stylus. Snake pauses when you leave its page.
Tap Start to resume. A reverse
turn is deliberately ignored. Tap the cat to pat, or hold it to open its page.
Follow Off hides only the header cat; Apps > Pixel cat still opens its page.

## Blank, inverted, or incorrectly colored display

Check module power, ground, its 16-bit interface straps, and the D0–D15 order in
[HARDWARE.md](HARDWARE.md). Keep the reference initialization and RGB565 settings.
Do not enable a generic SPI display library. DC is GPIO13 and WR is GPIO15. CS/RD
are assumed to be handled by the existing wiring. Reset remains -1.

If portrait worked previously, use Settings > Orientation > Portrait to compare
against native coordinates. Landscape uses software rotation; it does not write
a new MADCTL or swap the calibrated XPT2046 axes.

If the firmware never reaches calibration or Home, inspect the boot/download
connection while in ROM mode. The runtime console is disabled because its pins
are used by the LCD/touch hardware. Boot error messages are displayed locally.

## Touch does not respond or drifts

Confirm CLK47, CS48, DIN43, DOUT44 and remove conflicting UART0 drive paths.
Pressure threshold is the reference 600. Use a stylus and hold each calibration
target until green. Do not drag between targets. Release before the next point.
The calibration checks unstable samples and implausible transforms and asks for
a repeat instead of saving bad coefficients.

Use Settings > Calibrate touchscreen. It saves the same six-float `touch/cal`
record as the baseline. Orientation changes retain that calibration. Factory
reset removes it. A different partition layout, flash erase, or NVS recovery can
also require calibration again.

## Deck is not discovered

Enable Windows Bluetooth and check its BLE-capable adapter. Power on the deck,
then use Connection > Discover nearby decks. The firmware advertises the custom
service, not an HID keyboard or serial port. If several decks are found, select
the desired address. A saved address restricts automatic reconnect to that deck.

ROM download mode has no Bluetooth radio application. Tap RESET without holding
BOOT and wait for the dashboard. Hold BOOT only for USB flashing/recovery;
wireless updates require normal operation.

Confirm the firmware actually flashed is the `firmware/` NEXUS project, not the
archived `references/original/` HID firmware. Native USB serial disappearing
after boot is expected with this GPIO mapping.

## Bluetooth fails after an update with “The device does not recognize the command”

Windows can retain the earlier five-characteristic GATT table after the OTA
receiver adds a sixth characteristic. Version 1.3.0 announces Service Changed
after boot so the bonded PC refreshes that table. Connections request fresh
attributes; forced discovery can still return `ERROR_BAD_COMMAND` on some
adapters. That specific error uses a validated cache fallback. An older table
is repaired by reading the actual OTA ATT declaration and preserves encrypted,
acknowledged writes.

Install the current companion package before reconnecting an earlier 1.3.0
bootstrap. This recovery keeps the existing Windows/ESP32 bond. Other permission,
authentication and malformed-attribute errors are still rejected. A saved paired
address can reconnect directly if Windows retains a link and the deck has stopped
advertising. Only paired devices qualify for that fallback.

A cold Windows session can return all six cached controls without making any
radio connection. The companion issues an uncached protected Status read when
the session is inactive, then waits for the real ACTIVE event. Unreachable
devices and access-denied errors remain failures; metadata alone never enables
commands. Connection timeouts now have a readable explanation in the editor/log.

A cached GATT connection can become active before encryption finishes. The
companion waits up to eight seconds for the initial authenticated status read,
retrying only ATT authentication/encryption readiness errors. It never accepts
unencrypted status or actions, and other access errors are not bypassed.

Run one BLE central at a time. Quit the tray companion before using
`tools/upload_firmware.py` or another connection diagnostic, then restart it
afterward. Advertising normally pauses while a central is connected.

## Wireless upload fails, is cancelled, or needs recovery

Choose the application `nexus_deck.bin` from the current firmware build. A merged
image, bootloader, partition table, damaged file or another chip's image is
rejected before transfer. Keep the deck powered during upload and reboot.

A cancelled/disconnected uncommitted transfer keeps the current boot image. Retry
from byte zero after reconnecting. The companion reports success only after a
fresh session identifies the intended slot, version and ELF digest and confirms
the new boot. A pending image needs 20 continuous seconds of UI/Bluetooth host
health; an unhealthy boot restarts for bootloader rollback. See [OTA.md](OTA.md).

For USB recovery, use the backup-preserving flash helper. It selects the factory
application by resetting only OTA boot metadata and preserves NVS/artwork. Avoid
Factory reset or a whole-flash erase when retaining calibration and settings.

## Pairing fails, never asks for a passkey, or works on only one PC

Only one PC identity is authorized. Pairing must occur during the local
120-second enrollment window. The passkey is displayed on the deck and entered
in the Windows pairing dialog. A read-only BLE app without authenticated Secure
Connections is refused.

Remove stale NEXUS DECK/StreamDeck associations in Windows, then use Settings >
Forget saved PC on the deck. This clears bond records while retaining touch
calibration and profiles. Reopen Pair new PC and connect again. A repeat pairing
request does not automatically remove an existing owner's bond.

## Connected, but macros report an error

Read the device toast or the editor footer. Synchronize profiles after editing.
The companion rejects unknown IDs and executes only saved, approved actions.
VS Code/Terminal must be installed or assigned by executable path. Open project
and Dev server need valid Connection settings. A `.cmd` or `.bat` target is not
an executable alias; configure a real executable and literal arguments instead.

Keyboard shortcuts and text affect the current foreground Windows application.
Windows can refuse SendInput into an elevated app from a normal user process.
The companion reports the failure. It does not automatically elevate or dismiss
security prompts. Ctrl+Alt+Delete cannot be generated. Screenshot opens the
Snipping Tool region selector and needs a region selection on the PC.

## Missing media, temperature, GPU or battery readings

`--` means unavailable. A USB-powered desktop commonly has no PC battery sensor.
CPU temperature is not a standard Windows psutil metric. NVIDIA telemetry uses
NVML if its driver exposes it; other WDDM adapters can expose GPU Engine usage
through PDH. Unsupported temperatures remain unavailable.

Windows media information requires an active app publishing a system media
session. Some apps expose only transport controls, no title/artist. Master volume
uses the default audio endpoint; no endpoint gives an unavailable slider. Media
keys control the Windows media session supported by the running application.

## Metrics freeze or connection drops after a companion crash

The deck hides stale live values after 6.5 seconds and closes a stalled session
after 15 seconds. Its UI remains active. Restart the companion. Reconnection
creates a new session, syncs layouts, and resumes telemetry. Pending offline
actions are discarded to prevent delayed execution.

Network speeds are KiB/s across psutil's aggregate network counters, including
virtual interfaces. Disk usage is for the disk selected in Connection.

## Profile rejected or synchronization too large

Use the editor to correct the error. IDs must be unique; names, labels, images
and actions are strictly validated. The complete wire bundle is limited to
49,152 bytes. Artwork is validated and transferred separately. Keep at least one
profile/button. Remove unnecessary buttons or
custom image data to reduce the bundle. If a JSON file is damaged, move it out of
`%LOCALAPPDATA%\NexusDeck\profiles`, keep a backup, and restart the companion.
Rejected files are reported in the editor and log; they are not executed.

The NVS profile write must succeed before the companion activates the registry
snapshot. A reported NVS failure leaves the current UI usable. Try synchronization
again after freeing/resetting the device's own configuration if necessary.

## Build failures

Use ESP-IDF 5.5.x with its matching compiler and Python environment. The manifest
intentionally rejects other IDF major/minor versions. An internet connection is
needed to fetch LVGL on the first build. Later builds use the component cache.

If configuration was interrupted, run `idf.py -C firmware reconfigure` before
`build`. If the generated build files are stale, use the standard IDF `fullclean`
inside this project, then build again. Keep your source and user profiles.

The firmware reserves two NimBLE storage slots; its authorization policy remains
one PC. Setting the SDK store size to one can trigger an ESP-IDF 5.5.4
`-Werror=array-bounds` diagnostic in NimBLE's NVS restore sorting code.

If a reused universal-compiler build reports `XTENSA_GNU_CONFIG ... Unable to load
DLL`, use the current build helper or pass `-UseUniversalCompiler`. It restores
the compiler's library directory on PATH while retaining the ESP32-S3 toolchain.

## Logs, recovery and packaged app

The companion writes a rotating `companion.log` in `%LOCALAPPDATA%\NexusDeck`.
`--diagnose` prints real metrics without connecting BLE. `--no-bluetooth` pauses
automatic connections. `--data-dir` selects an isolated directory for testing.

Copy the entire PyInstaller `NexusDeck` directory when installing, including
`_internal`. Starting a second copy opens the existing editor. Quit from the
tray to stop services. A startup configuration error names its file directory.

The installer adds a Start-menu shortcut and a Startup-folder shortcut using
`--background`. Check the Windows notification area's hidden icons when the
editor is closed. Double-click NEXUS DECK there to reopen it. User configuration
is separate from the installation in `%LOCALAPPDATA%\NexusDeck`.

If a packaged build fails at a Qt import or reports a missing ICU entry point,
rebuild using `companion/NexusDeck.spec` and copy its complete output folder.
The spec excludes foreign ICU DLLs from developer tools on PATH and uses the
Windows 11 system ICU API.

The idle clock always uses black with bold white lettering, independently of
the selected light/warm theme. Until the first valid PC telemetry after boot,
it shows `--:--` and a clock synchronization message. Once synchronized, time
continues locally through disconnects, with the last connected computer name.
Touch the screen to return to the previous page.

Factory reset is a local, confirmed device action. It erases settings, profiles,
calibration and bonds, then restarts with the compiled default profiles. NVS
initialization recovers a full/incompatible NVS partition using the IDF recovery
procedure and shows a recovery message. Recovery can remove saved calibration
and enrollment data; recalibrate and pair again afterward.
