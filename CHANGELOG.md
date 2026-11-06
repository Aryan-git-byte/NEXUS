# Changes

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
