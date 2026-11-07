# Changes

## 1.3.1 — 7 November 2026

- Actual ESP-IDF reset cause, boot identity and monotonic uptime in the encrypted
  device descriptor, so reconnects can be distinguished from MCU restarts.
- Last reset shown under Setup > Device and in the Windows Firmware window.
  Diagnostics use a fixed boot snapshot and do not write additional NVS records.
- Authenticated session logs record the boot identity/cause/uptime. Lost live
  links update the editor/tray state immediately and are explicitly logged.
  Intentional Quit/Disconnect operations do not generate a false radio-fault log.
- Late Windows disconnect callbacks from an old client cannot invalidate a
  replacement connection; the queued-event race is covered by a reconnect test.
- Reboot verification and boot-confirmation status reads have a bounded timeout.
- OTA timeouts include the last acknowledged byte count. A missing completion
  acknowledgement triggers fresh boot verification; errors after completion
  never promise that the original image remains selected.
- Battery-power guidance for the verified Waveshare board schematic, including
  the VBAT power path and external-display pin conflicts.
- Native reset-cause/boot-identity checks, malformed diagnostic validation and
  Qt reset-feedback regression coverage.

## 1.3.0 — 7 November 2026

- Firmware uploads over the existing authenticated BLE connection, with bounded
  fragmentation, chunk acknowledgements, duplicate retry handling and cancellation.
- Two inactive 4 MB OTA slots and rollback metadata on the 16 MB flash device.
  USB bootstrap/recovery preserves calibration, preferences, bonds and artwork.
- Whole-image SHA-256, ESP32-S3/application descriptor and ESP-IDF image validation
  before selecting a new boot slot. A new image confirms its boot after 20 seconds
  of continuous live UI and Bluetooth host health.
- Native Windows Firmware window with validated file selection, progress, cancel,
  automatic reconnect and confirmation of version, ELF identity and boot slot.
- Firmware update progress on the deck; existing light UI, black clock, stylus
  apps and saved profiles continue to work.
- Automated image, transport, Qt, partition migration and native OTA failure tests.
- Windows GATT cache recovery after the update characteristic is added, with a
  verified ATT declaration and acknowledged writes; existing pairing is retained.
- Service Changed indications after firmware boot and direct reconnection to a
  saved Windows bond when a retained connection has stopped advertising.
- Connections refresh Windows' GATT attributes and wait for authenticated
  encryption before the first status read, avoiding stale handles and a startup race.
- Cold cached Windows sessions initiate a live link with an uncached protected
  read, dispose discarded GATT resources and report readable timeout errors.

## 1.2.0 — 6 November 2026

- Offline Apps hub with calculator, 2048, Snake, timers and a pettable pixel cat;
  Custom Macros remains available from the hub.
- Large stylus targets, tap arrows, one-step 2048 undo, Snake speed selection,
  pause/resume and game state retained during page navigation.
- Countdown from one second to 24 hours with on-device hour/minute/second controls,
  a 25/5 focus cycle with a longer break after four sessions, and stopwatch laps.
- Monotonic timers continue through navigation and the clock screensaver.
  Completion events use the existing encrypted, fragmented, replay-safe BLE link.
- Windows plays a bundled original PCM chime and displays tray notifications.
  Sound can be disabled; missing audio falls back to a readable notification.
- Code-native pixel cat with pat reactions, food, happiness, energy, naps, four
  names and a Follow toggle. High scores and care settings use a separate
  validated NVS record.
- Native gameplay tests and actual LVGL interaction/layout checks in both
  orientations, alongside the Windows regression suite.
- Notification banners resize correctly after rotation, leave stylus controls
  usable, and do not interrupt the black idle clock.

## 1.1.1 — 6 November 2026

- Black idle clock with bold white time, uppercase day, full date and the last
  connected computer name. Portrait and landscape use separately sized,
  antialiased Montserrat Bold clock fonts stored in flash.
- Light and warm themes with colorful tiles, readable settings dropdowns and
  bounded media title/artist labels.
- Color and animated icon import, including SVG, GIF, APNG and WebP, with
  validated artwork synchronization and persistent device storage.
- Per-user Windows installation, Start-menu shortcut, tray startup at sign-in,
  Apps uninstall entry and saved-draft shutdown support.
- PyInstaller packaging excludes foreign ICU DLLs so Qt uses the Windows 11
  system ICU API.
- Terminal launching also finds the WindowsApps execution alias when a GUI or
  startup process does not include that directory in PATH.
- Updated build, installation, icon, protocol and verification documentation;
  private backups and generated runtime data are excluded from Git.
