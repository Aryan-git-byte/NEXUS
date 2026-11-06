# Profiles and editor

The six shipped profiles are Default, Development, Media, Gaming, Productivity,
and Custom. They are copied to `%LOCALAPPDATA%\NexusDeck\profiles` on the first
run. The firmware's presentation bundle is generated from these same validated
JSON files by `tools/generate_defaults.py`.

Select a profile on the left. Select a button in the deck preview to edit its
label, icon and tap action. Enable a long-press action to configure it separately.
Apply button updates the draft. Save profile validates and saves it on the PC.
Sync to deck saves the current draft first and sends all saved presentation
profiles with the selected profile active.

Add/Remove controls change the button count. Move controls change order. The
preview paginates after eight buttons in landscape or twelve in portrait.
New profile creates a stable ID; Rename changes its display name without breaking
button IDs. The editor prompts before discarding a modified draft or removing a
profile. At least one profile and one button per profile must remain.

The device Home grid shows the active profile. Development, Media and Custom
pages use the active profile if its category matches, otherwise the first saved
profile of that category. If a category has been removed entirely, they show the
active profile. Media retains its live session text and volume slider. Monitor
and Settings are device-owned screens, not executable PC profiles.

## Local JSON

```json
{
  "id": "development",
  "name": "Development",
  "category": "development",
  "revision": 1,
  "buttons": [
    {
      "id": "vscode",
      "label": "VS Code",
      "icon": "code",
      "action": {"type": "launch_app", "target": "code", "args": []},
      "long_press": {"type": "keyboard_shortcut", "keys": "ctrl+s"}
    }
  ]
}
```

Supported actions are `launch_app`, `keyboard_shortcut`, `media_play_pause`,
`media_next`, `media_previous`, `volume_up`, `volume_down`, `volume_mute`,
`screenshot`, `open_url`, `lock_windows`, `show_desktop`, and `text_snippet`.

Application targets are approved aliases (`code`, `terminal`, `powershell`,
`explorer`, `project`, `dev_server`) or absolute existing `.exe` paths selected
in the editor. `args` is a list of literal argument strings; `cwd`, when supplied,
must be an absolute folder. Launching uses `subprocess.Popen(..., shell=False)`.
An installed executable is checked again at execution time.

Shortcuts use names such as `ctrl+shift+s`, `alt+tab`, `win+d`, or `f5`. One final
non-modifier key is required. Ctrl+Alt+Delete is rejected because ordinary apps
cannot generate Windows secure attention. Websites must be http/https URLs with
no embedded credentials. Approved text snippets contain `text` and
`approved:true`; the editor requires the explicit local approval checkbox.

Import icon accepts PNG, BMP, JPEG, SVG, GIF, APNG and WebP. It preserves color
and transparency, fits artwork inside 32 × 32 pixels and retains up to 16
animation frames. Larger animations are sampled across their full duration.
Source files are limited to 10 MB, 16 megapixels and 256 frames. Standalone SVG
is rasterized by Qt; supported SVG animation is sampled into device frames.
The on-device NDI2 asset contains a shared RGBA palette, frame delays and indexed
pixels. Use built-in removes the imported image and selects bundled artwork.

Local JSON stores color artwork in `icon_image:{"id":"...","data":"..."}`.
The ID is the first 14 hexadecimal characters of its SHA-256 content hash.
The companion uploads validated assets before syncing profiles; the wire button
then contains only `icon_asset`, and device NVS retains the artwork. Executable
action definitions remain on the PC. Legacy `icon_data` remains supported for
24 × 24 monochrome icons, but new editor imports use color assets.

Limits are eight profiles, 24 buttons per profile, 40 UTF-8 bytes per profile name,
48 per button label, and a 12,288-byte profile presentation message. Artwork is
uploaded separately, with a maximum of 17,450 bytes per decoded NDI2 asset. BLE
framing supports complete messages up to 49,152 bytes. The editor validates both
layouts and assets before saving. Changing profiles does not require firmware edits.

Repository default generation is for maintainers. It does not read or overwrite
the user's LocalAppData profile directory. Run `tools/generate_defaults.py` after
editing shipped profile JSON, then rebuild the firmware so its offline defaults
match the companion defaults.
