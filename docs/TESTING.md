# Recorded verification: 1.3.0

On 7 November 2026, the ESP32-S3 received the final 1.3.0 application over its
authenticated Bluetooth connection, rebooted into ota_0, and confirmed its
boot. Two deliberate disconnects then restored fresh encrypted sessions,
six profiles, five artwork assets and actual PC telemetry. The final Windows
package is installed with tray operation and sign-in startup configured.

Builds use ESP-IDF 5.5.4, LVGL 9.2.2 and 64-bit Python 3.11 on Windows 11.
Public evidence excludes raw radio logs, machine names, pairing data and flash
backups. The [1.2.0 record](TESTING_1.2.0.md) and
[1.1.1 record](TESTING_1.1.1.md) retain earlier verification.

## Firmware build

The actual build completed successfully:

    [19/21] Linking CXX executable nexus_deck.elf
    Successfully created esp32s3 image.
    nexus_deck.bin binary size 0x10c490 bytes.
    Smallest app partition is 0x400000 bytes. 0x2f3b70 bytes (74%) free.

The final application is 1,098,896 bytes. SHA-256:

    a82845272e261a9d4d1869b23c944a7cd533b52ffda425ad605b18778864c5ef

ELF identity reported by the running device:

    da2a031666f52850a1755aec3ce6e5cde549af7ecd2cfccba1781ead988b0b8d

The public firmware-build-1.3.0.json in test-results records image hashes, the
validated four-image USB recovery plan and the actual compiler command. The
display bus, ILI9488 initialization, RGB565 transport, touch pins and calibration
format remain unchanged. Bootloader rollback is enabled; the build rejects an
old configuration that disables it.

## USB bootstrap and storage preservation

The board's physical flash capacity was identified as 16 MB. Before the initial
OTA-enabled factory bootstrap, the helper backed up 6,684,672 bytes with verified
512 KB reads. The bootloader, partition table, OTA metadata and factory image
were written and all four image hashes verified. NVS, PHY and artwork regions
were excluded from write/erase ranges.

The bootstrap factory application had SHA-256
920a52ce74b6bbd318eabc7b2a92d40da4f6c9ffe34c1f8df6303f7c0bf9f048.
It is distinct from the final cache-repair application subsequently uploaded
wirelessly. The public firmware-usb-bootstrap-1.3.0.json records that initial
upload. Raw backups and preserved NVS contents remain private and ignored.

ROM download mode does not advertise Bluetooth. After a normal RESET, the user
reported the dashboard and the agent observed advertising and an authenticated
status read from the actual device.

## Physical Bluetooth OTA test

The production uploader first verified that the real device rejected an invalid
manifest. It then cancelled a partial upload and checked that the original
factory slot, version, ELF identity and confirmation remained unchanged.

The complete final application was then transferred to the inactive slot.
Selected actual output:

    Device checks passed: invalid manifest rejected; cancelled upload retained current firmware.
    95% Uploading 1,098,896 / 1,098,896 bytes
    96% Verifying the complete firmware image
    97% Waiting for the deck to restart and reconnect
    Encrypted BLE session established with firmware 1.3.0
    Profiles synchronized: 6 profiles, 5 artwork assets
    100% Firmware 1.3.0 installed and boot verified
    OTA update verified: firmware 1.3.0, slot ota_0, 1098896 bytes

The transfer and confirmed reboot took 1,058.33 seconds, about 17 minutes
38 seconds, at ATT MTU 247. This first migration used Windows' descriptor API
for an OTA value misclassified by its earlier cached service table. Other
adapters and ordinary characteristic uploads can differ in speed. Completion
was checked through a fresh session, matching slot/version/ELF identity and
ota_confirmed:true, rather than inferred from bytes sent.

Full public result: [ota-upload-1.3.0.json](../test-results/ota-upload-1.3.0.json).

## Bluetooth connection repair and reconnection

Windows retained the earlier five-characteristic service table when the sixth
OTA control was introduced. Forced discovery failed with ERROR_BAD_COMMAND.
The companion recovered the actual write-only OTA value after reading and
validating its ATT declaration, adjacent value handle and UUID. The final
firmware announces Service Changed after boot. Connections refresh Windows'
attributes, with bounded waiting for authenticated encryption before the
first protected status read. Other access errors remain failures.

An additional cold-launch check reproduced six locally cached controls with a
CLOSED GATT session and a blank 25-second timeout. The final backend issues an
uncached protected Status read before waiting for the real ACTIVE event. It
accepts only temporary ATT authentication/encryption readiness errors at that
step; the later protected handshake must succeed. Unreachable/forbidden peers
and cancellation dispose the unclaimed native services. Regression checks cover
these cases and readable timeout feedback.

The cache/encryption connection logic passed two deliberate disconnect/reconnect cycles:

| Observation | Actual result |
| --- | --- |
| First automatic reconnect and profile sync | 69.72 seconds; fresh session |
| Second automatic reconnect and profile sync | 21.78 seconds; fresh session |
| Continuous connected observation afterward | 30.91 seconds |
| Real PC telemetry samples during the check | 118 |
| Successful telemetry ATT writes | 17 |
| Post-update OTA descriptor bridge needed | No |

Each status read identified the final confirmed image in ota_0. No reconnect
needed passkey entry or deleting the saved bond. Evidence:
[ble-reconnection-1.3.0.json](../test-results/ble-reconnection-1.3.0.json).

## Software and rendered UI checks

The final Python regression suite passed:

    180 passed in 8.54s

Checks cover protocol validation/reassembly, approved actions, profiles,
monitoring fallbacks, editor controls, authenticated pairing, cold/stale GATT
cache recovery, cold-session link initiation, the encryption startup race, firmware validation, OTA retries,
cancellation, wrong reboot identity and calibration-preserving flash recovery.
The python-tests.xml record has its hostname removed. Windows input and
radio operations are intercepted in these regression tests.

The native host build executes the actual game/calculator/timer/pet models and
OTA state machine. Five local-app groups and four OTA groups passed. OTA groups
cover inactive-slot writes, hash/retry behavior, malformed input, flash errors,
cancel/disconnect/timeouts and continuous boot health/rollback using explicit
simulated flash and reset boundaries. Logs are retained in
test-results/lvgl-preview/local-app-tests.log and ota-tests.log.

The actual LVGL pages rendered into 60 portrait/landscape images. Interaction
checks passed for stylus controls, navigation, cat pats, timer completion,
separate settings dropdowns, media text bounds, notifications and OTA feedback.
The black clock and upload/failure cards were visually inspected in both
orientations. The Windows Firmware dialog was also visually inspected through
the native Windows font platform. Its preview uses a fixture connection state,
and firmware file details come from the actual compiled image.

## Packaged companion and background installation

PyInstaller built the final 1.3.0 application. Its packaged startup check exited
0 after opening the real editor, loading six profiles/eight preview buttons,
starting services and collecting two real PC telemetry samples. It executed no
macros or Bluetooth connections. Result:
[companion-smoke-1.3.0.json](../test-results/companion-smoke-1.3.0.json).

The per-user installer preserves local profiles/settings and pairing, verifies
the copied executable, registers Start-menu/uninstall entries and configures a
Startup-folder shortcut with --background. The installed tray instance's live
connection and executable/shortcut checks are recorded in
test-results/companion-installed-1.3.0.json. A subsequent Windows sign-in was
not simulated.

The installed package established a new encrypted session and synchronized all
six profiles/five artwork assets after its background launch. Process, executable,
startup shortcut and runtime records were checked again about three minutes
after synchronization, with no subsequent connection error recorded. This is
distinct from the direct manager's continuous on-device status-read test.

## Limits of these observations

The actual wireless upload, rejection/cancellation, confirmed reboot, encrypted
reconnection, profile synchronization and telemetry writes were performed on
the physical deck. The agent did not perform a physical display/touch audit,
calibration accuracy measurement after power loss, or a stylus-triggered
timer-to-PC sound audit for 1.3.0. Host rendering and storage preservation checks
do not substitute for those observations.

A physical power-cut/rollback test, minimum-MTU upload and a second complete
upload into ota_1 were not performed. Their relevant state/transport paths
were exercised with the explicit native and simulated ATT fixtures.
