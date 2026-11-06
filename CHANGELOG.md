# Changes

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
