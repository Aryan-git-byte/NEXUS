# NEXUS DECK architecture

NEXUS DECK is an ESP32-S3 BLE peripheral and a Windows BLE central. The deck owns
its display, calibration, navigation, saved layouts, and connection feedback. The
companion owns executable actions, telemetry, and the editable source profiles.

```mermaid
flowchart LR
  Touch[XPT2046 sampling task\n8 ms / core 0] --> Snapshot[Protected touch snapshot]
  Snapshot --> UI[LVGL task\ncore 1]
  UI --> LCD[Partial RGB565 flush\n16-bit GPIO bus]
  UI --> Apps[Local calculator, games, timers and cat]
  UI --> Requests[Bounded request queue]
  Requests --> Worker[BLE I/O worker\ncore 0]
  Host[NimBLE host\ncore 0] --> Fragments[Bounded fragment queue]
  Fragments --> Worker
  Worker --> Events[UI events + telemetry mailbox]
  Events --> UI
  Worker <--> Host
  Host <-->|Encrypted BLE GATT| PC[Windows asyncio service]
  PC --> Registry[Saved action registry]
  PC --> Monitor[psutil / audio / media / sensors]
  Editor[Qt editor + tray] <--> PC
  Editor --> JSON[Atomic local JSON profiles]
```

## Firmware ownership

`display/lcd_driver.cpp` is the only LCD bus owner. The original register-write
lookup tables, WR strobes, initialization data, MADCTL `0x48`, and RGB565 COLMOD
`0x55` are retained. One 23,040-byte internal RAM buffer feeds LVGL's partial
renderer. Landscape transposes each flushed rectangle into the unchanged native
panel address space during transfer. No full-frame rotation buffer is required.

`display/touch_driver.cpp` exclusively owns the XPT2046 GPIOs. Sampling runs every
8 ms independently of LVGL. The UI reads a protected snapshot; it never performs
bit-banged SPI inside an LVGL input callback. The reference pressure formula,
commands, five-sample median, affine transform, and `touch/cal` NVS format remain.
Calibration is a UI state machine with a settling period, 30 distinct samples per
target, stability checks, release gating, and determinant/scale validation.

Only the UI task calls LVGL after initialization. BLE callbacks copy ATT packets
into a 12-entry queue. The I/O worker reassembles, validates, and processes JSON.
Telemetry uses a one-entry overwrite mailbox, so a slow display never accumulates
stale telemetry. Action requests and replies use bounded queues. A profile update
transfers ownership of one validated `ProfileSet` to the UI. Button arrays are
allocated once per configured profile, then released when its replacement arrives.

Settings and profiles are written by the I/O worker. Affine calibration is saved
once at the end of the calibration interaction. NVS handles are closed on every
path. A first boot loads compiled presentation profiles; subsequent boots load
the saved layouts without needing a PC connection.

The local app models in `apps/local_apps.cpp` use bounded arrays and have no
LVGL, BLE or operating-system dependencies. UI callbacks update them in the
existing UI task. Snake advances from monotonic deadlines and pauses when its
page is left. Timers use elapsed time, so PC wall-clock synchronization cannot
change their duration. No app starts an additional blocking loop.

Snake renders a fixed 80 × 64 RGB565 image (10,240 bytes), scaled without
antialiasing. The cat shares a 24 × 24 ARGB image (2,304 bytes) between its header
and page views. Reactions invalidate these small regions; the clock cat is static
during sleep. Calculator text and game state use fixed-size storage.

The validated `nexus_apps/prefs_v1` NVS record is separate from device settings,
touch calibration, profiles and bonds. App writes pass through the existing
worker queue and are coalesced. Active timers and game boards remain in RAM;
high scores, cat care and preferences survive restart.

## Windows ownership

Qt owns the GUI thread. `Controller` runs an asyncio service on a separate thread
and exchanges queued Qt signals. BLE discovery, reconnects, writes, replies, and
profile acknowledgements run there. psutil/audio monitoring and actions run in
worker threads. COM audio objects are created and released on their calling
thread, never shared across Qt or asyncio threads.

Timer completion is a built-in notification event, handled through the same
session and response cache as actions. It can select only countdown, focus or
break; it cannot select an executable or a sound file. The companion owns the
packaged PCM chime path, plays it asynchronously, and queues a Qt tray notification.

The editor works on a draft. Saving validates it and replaces a JSON file
atomically. Sync sends only presentation fields to the deck. The executable
registry switches to the saved snapshot only after the device acknowledges
validation and persistence. BLE messages cannot supply application paths,
arguments, text snippets, URLs, or a new action definition.

## Failure behavior

* A missing PC gives immediate local button feedback. Offline actions are not
  queued for later execution.
* A disconnect clears the authenticated application session and pending actions;
  navigation, calibration, profile selection, and the clock keep working.
* Sensor failures yield `null`. Device labels show `--`, with empty indicators.
* Live metrics become stale after 6.5 seconds. A 15-second application heartbeat
  timeout closes a stalled link; valid in-progress fragments count as activity.
* Windows reconnects with bounded backoff and syncs saved profiles again.
* Action replies are cached for the latest 64 action sequences. Older sequences
  are rejected, preventing an expired retry from executing again.
* Exactly-once execution across a companion process crash is not promised: a
  reconnect creates a new session, and the deck discards old pending actions.

No Wi-Fi, cloud service, Bluetooth Classic, external display library, or PC
process is required to render and operate the device UI.
