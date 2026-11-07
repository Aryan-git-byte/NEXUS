# Recorded verification: 1.3.1 software; device remains on 1.3.0

The 1.3.1 firmware builds and the updated Windows companion is installed with
tray operation and Windows sign-in startup. The diagnostic firmware has **not**
been installed on the ESP32. Its actual wireless upload lost the link during
transfer. A subsequent protected read identified the previous confirmed 1.3.0
application in ota_0, and the installed companion connected and synchronized it.

The user reports that the screen blanks and reloads the dashboard when Bluetooth
drops. The carrier is the Waveshare ESP32-S3-Touch-LCD-2, powered by one raw
18650 on VBAT. The enclosure blocks BOOT/RESET and access to USB power, so USB
recovery and a comparison against another supply were not performed. A reset
cause, rail voltage measurement or successful 1.3.1 device boot remains needed
to distinguish brownout, panic and watchdog causes. This is not recorded as a
completed hardware repair.

The [verified 1.3.0 release](TESTING_1.3.0.md), including the successful full BLE
OTA and reconnection tests, remains available separately. Earlier records are
[1.2.0](TESTING_1.2.0.md) and [1.1.1](TESTING_1.1.1.md). Public evidence excludes
raw radio logs, local machine names, pairing secrets and flash backups.

## Firmware build and diagnostics

ESP-IDF 5.5.4 and LVGL 9.2.2 produced the actual 1.3.1 application:

    nexus_deck.bin binary size 0x10cba0 bytes.
    Smallest app partition is 0x400000 bytes. 0x2f3460 bytes (74%) free.

The application is 1,100,704 bytes. SHA-256:

    f92b5112d2d2dd829d4ea9d6f6688097fb7f13d536fcade0bc2441c8e10cd507

ELF identity:

    10f986f116a60ffa26ba684f6e92b8f1df9e66c04b21af37619cbd401d114929

The build/validated flash plan is recorded in
[firmware-build-1.3.1.json](../test-results/firmware-build-1.3.1.json).
The LCD bus, RGB565 transport, display initialization, touch pins and calibration
format remain unchanged. Brownout and watchdog protection remain enabled.

New authenticated descriptor fields capture esp_reset_reason(), a once-per-boot
identity and monotonic uptime. Setup > Device shows Last reset; the companion
validates these fields, logs them and displays a reported uptime snapshot.
Older 1.3.0 firmware gracefully omits diagnostics. Installing the new companion
does not add the firmware fields to an older device image.

## Failed physical update and subsequent radio observation

Selected actual output from the attempted ota_0-to-ota_1 transfer:

    Validated firmware 1.3.1: 1,100,704 bytes
      0% Preparing firmware 1.3.1
      5% Uploading 58,368 / 1,100,704 bytes
    BLE link lost; reconnecting
    Firmware upload failed:

The original uploader surfaced a blank TimeoutError. The exact final accepted
offset was not logged: 58,368 is the last printed progress checkpoint, not a
claim about the final flash offset. Completion/boot selection was never sent.
The inactive ota_1 contains an incomplete transfer; it was not selected.

A separate real protected-read probe found one advertisement, connected at
ATT MTU 247, and identified the confirmed 1.3.0 application/ELF in ota_0. It
completed 17 periodic status reads before a later read timed out. That firmware
does not expose reset diagnostics, so the probe did not capture a reset cause.
[ota-upload-1.3.1.json](../test-results/ota-upload-1.3.1.json) records the failed
attempt, subsequent descriptor and hardware-access limits with success:false.

The updated uploader now reports acknowledged bytes and readable errors before
completion. A missing completion acknowledgement triggers fresh boot verification.
Timeouts/cancellation after completion starts do not claim that the original
image was retained. These additional failure paths passed simulated ATT tests.

After more than four minutes of a stable normal connection, one controlled
retry inserted a 20 ms gap between firmware ATT writes and 150 ms between chunk
requests. It reached a higher offset, then also lost the link:

     15% Uploading 175,104 / 1,100,704 bytes
    Firmware upload failed: Firmware transfer stopped after 211,584 / 1,100,704 bytes: Bluetooth operation timed out. The current firmware remains selected.

This acknowledged count is exact, unlike the first attempt's printed checkpoint.
The completion request was never sent and the diagnostic image was not activated.
The public OTA record includes both failures. Pacing did not establish a fix or
identify a reset cause. No further transfer was attempted; the installed
background companion was restored with automatic reconnect enabled.

## Software and rendered UI checks

Actual Python result on Windows 11 with 64-bit Python 3.11:

    220 passed in 11.15s

The suite covers framing, approved actions, profiles, metric fallbacks, the
editor, authenticated pairing/cache recovery, reset-field validation, firmware
validation and OTA retry/cancel/timeout/reboot uncertainty. Intentional app
Quit/Disconnect operations are checked to avoid false radio-fault logs. A late
Windows CLOSED callback from an old client is tested during replacement
connection setup and cannot invalidate the current session. Radio and Windows
input operations are intercepted in these tests. The public python-tests.xml
has its hostname removed.

The native host renderer passed five local-app groups, four OTA groups and two
boot-diagnostic groups. All 16 ESP-IDF reset classifications and 1,000 stable
boot-info reads were checked. Native OTA tests exercise explicit simulated
flash/reset boundaries, not an ESP32 radio or physical power rail.

The actual LVGL source rendered 62 portrait/landscape images. Stylus navigation,
cat interactions, game/timer behavior, settings dropdowns, text bounds,
notifications and OTA feedback passed their existing checks. Device information
and reset text were visually inspected in both orientations, as was the real
Qt Firmware dialog using Windows fonts. UI rendering uses clearly identified
hardware/telemetry fixtures; its reset labels are not measurements from the deck.

Native logs: [boot checks](../test-results/lvgl-preview/boot-tests.log),
[local apps](../test-results/lvgl-preview/local-app-tests.log),
[OTA checks](../test-results/lvgl-preview/ota-tests.log), and
[layout checks](../test-results/lvgl-preview/layout-checks.log).

## Windows installation and background connection

PyInstaller built the final package successfully. Its actual startup smoke
check reported:

    editor_started: true
    profiles: 6
    visible_buttons: 8
    telemetry_samples: 2
    services_running: true
    version: 1.3.1
    success: true

These are real editor/service/PC-monitor observations; automatic BLE connections
are intentionally paused during smoke checks. Evidence:
[companion-smoke-1.3.1.json](../test-results/companion-smoke-1.3.1.json).

The installer verified the package copy, started exactly one installed process
with --background, and updated Startup/Start-menu/uninstall registration to
1.3.1. Package and bundled timer-sound hashes match the source artifacts.
Settings, saved profiles and the Windows bond were retained.

The installed process established a fresh authenticated connection to the
actual 1.3.0 device and synchronized six profiles/five artwork assets. An early
background observation lasted more than four minutes without a subsequent
connection error. Later runs, including the final reconnect-guard package,
still reported live link losses and restored encrypted sessions/profile
synchronization automatically. The final installation record explicitly does
not confirm continuous Bluetooth stability. No boot identity/reset cause was
available from that 1.3.0 device. Profile sync
rebuilds the current page, so a Home-page redraw can itself resemble a reboot;
screen appearance alone does not identify an MCU reset.
Startup shortcut targets/arguments were checked; a later Windows sign-in was
not tested. Exact observation timing and hashes are recorded in
[companion-installed-1.3.1.json](../test-results/companion-installed-1.3.1.json).
