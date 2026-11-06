# NEXUS BLE protocol, version 1

ESP32-S3 is the peripheral. Windows/Bleak is the central. Bluetooth Classic is
not used. The complete 128-bit service UUID is advertised; the name `NEXUS DECK`
is in the scan response. Advertising intervals are 100–150 ms.

| Characteristic | UUID | Operations |
|---|---|---|
| Service | `b7c20000-7b9b-4e7d-9e10-a4f68c120001` | Primary custom service |
| Commands | `b7c20001-7b9b-4e7d-9e10-a4f68c120001` | Device notifications; encrypted status read |
| Telemetry | `b7c20002-7b9b-4e7d-9e10-a4f68c120001` | PC writes with response |
| Action responses | `b7c20003-7b9b-4e7d-9e10-a4f68c120001` | PC writes with response |
| Profiles | `b7c20004-7b9b-4e7d-9e10-a4f68c120001` | PC writes with response |
| Status | `b7c20005-7b9b-4e7d-9e10-a4f68c120001` | Encrypted read, PC writes, device notifications |

All characteristic access requires encryption, authentication, an authorized
bond identity, and a 16-byte encryption key. Notifications are sent only after
encryption and subscription. Pairing uses a displayed random passkey, LE Secure
Connections, MITM protection, and NVS persistence. Enrollment is allowed only
through the local 120-second window. The owner record authorizes one PC; NimBLE
has two storage slots to avoid ESP-IDF 5.5.4's one-slot NVS sort compiler issue.
Unknown identities cannot use the extra slot.

## Framing

Every write and notification is a fragment. **Reads of Status/Commands return a
plain JSON descriptor**, not a fragment stream. ATT MTU can be only 23 bytes, so
JSON is never assumed to fit one packet.

Each fragment starts with this 16-byte, little-endian header:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | ASCII `ND` |
| 2 | 1 | Protocol version: 1 |
| 3 | 1 | Reserved flags: 0 |
| 4 | 2 | Nonzero message ID, uint16 |
| 6 | 2 | Total UTF-8 JSON byte length |
| 8 | 2 | Fragment byte offset |
| 10 | 2 | Fragment payload length |
| 12 | 4 | CRC-32 of the complete JSON payload |
| 16 | variable | Fragment payload |

Python format is `<2sBBHHHHI`. CRC is standard reflected IEEE CRC-32, identical
to `zlib.crc32`, initial/final XOR `0xffffffff`. At MTU 23, the ATT payload is 20
bytes and each fragment carries 4 JSON bytes. The preferred MTU is 247; senders
use the actual negotiated MTU, with a 512-byte maximum ATT payload.

The global maximum complete message is 49,152 bytes. Profile presentation JSON
is capped at 12,288 bytes; larger frames support color artwork uploads.
Firmware additionally caps
telemetry at 2,048 and response/handshake messages at 1,024 bytes. One ordered
assembly is maintained per characteristic. Exact retries of already received
fragments are tolerated; conflicting, truncated, interleaved and out-of-order
fragments are rejected. An incomplete assembly expires after 10 seconds without
a fragment. A bad CRC never reaches JSON parsing.

Message IDs identify fragment assemblies. Nonzero uint32 **sequence numbers**
identify application requests/replies. They are independent. Each connection
also receives a fresh, lowercase 32-hex-character application session token.
It prevents stale application packets from being reused after reconnect; the
bond and authenticated BLE encryption provide authorization.

## Handshake and synchronization

The central connects/pairs, reads Status to verify `name` and `protocol`, and
subscribes to Commands and Status before sending:

```json
{"type":"hello","protocol":1,"sequence":100,"session":"0123456789abcdef0123456789abcdef","pc_name":"Windows PC","epoch":1793952000,"utc_offset":19800}
```

Epoch is UTC Unix time. Offset is the PC's local timezone offset in seconds.
The descriptor and hello acknowledgement do not invent a device battery level.

The device replies through Status:

```json
{"type":"hello_ack","sequence":100,"session":"0123456789abcdef0123456789abcdef","status":"success","message":"Encrypted session established"}
```

The central sends a `profile_sync` on Profiles with `schema:1`, `sequence`,
`session`, `active`, and `profiles`. Each profile contains `id`, `name`, `category`,
`revision` and presentation buttons. Buttons contain `id`, `label`, `icon`,
`has_long_press` and optional `icon_data` (legacy monochrome) or `icon_asset`
(color/animated artwork). **Action definitions are omitted.**

Up to eight profiles and 24 buttons per profile are supported, subject to the
total byte limit. IDs are at most 32 ASCII characters: start with a–z and continue
with a–z, 0–9, underscore or hyphen. Names are at most 40 UTF-8 bytes, labels 48,
icon names 16. Duplicate IDs, unknown keys, unsupported categories/icons and
invalid images are rejected before the profile is activated.

Legacy icons are 24 × 24, row-major, one bit per pixel, MSB first: 72 bytes encoded
as 96 base64 characters. The device converts them once per page to an alpha-only
LVGL image tinted to the common icon color.

### Color and animated artwork

The encrypted descriptor includes `icon_assets:true` and `max_message:49152`.
The central uploads each unique image through Profiles before `profile_sync`:

```json
{"type":"icon_sync","sequence":99,"session":"0123456789abcdef0123456789abcdef","asset":"0123456789abcd","data":"BASE64_NDI2_BYTES"}
```

The base64 value above represents the encoded asset; it is not valid artwork.
`asset` must equal the first 14 hex characters of SHA-256 over the decoded data.
The device checks the hash, format, dimensions, palette indexes and frame timing,
saves the asset in NVS, then returns `icon_ack` through Status. Failed validation
or storage produces an error acknowledgement. A layout referencing an unsaved
asset is rejected. After a successful layout sync, unused device assets are purged.

NDI2 uses a 10-byte little-endian header (`<4sBBHBB`): magic `NDI2`, width 32,
height 32, palette count 1–256, frame count 1–16 and flags 0. This is followed by
one uint16 delay per frame (40–5,000 ms), the shared RGBA palette (four bytes per
color), then 1,024 row-major palette indexes per frame. The exact decoded size
must match the header and cannot exceed 17,450 bytes. Images are converted to
RGB565 plus alpha when displayed; animation changes frames without rebuilding
the screen. All uploads use the same fragmentation, CRC, session and encryption
rules as other messages.

The device returns `profile_ack` only after validation and NVS persistence. The
Windows registry switches to that exact saved snapshot after acknowledgement.
The companion automatically repeats synchronization after reconnect.

## Actions and responses

Commands notifications use registered button IDs, not executable commands:

```json
{"type":"action","id":"vscode","profile":"development","gesture":"press","sequence":12,"session":"0123456789abcdef0123456789abcdef"}
```

`gesture` is `press` or `long`; `long` requires an explicitly configured action.
The central looks it up in its approved saved profile registry. Unrecognized
fields, IDs, profiles or gestures are rejected. Paths, arguments, URLs, snippets,
action types and shell commands cannot be injected through this message.

The only value-bearing built-in command is:

```json
{"type":"action","id":"system_volume_set","value":42,"sequence":13,"session":"0123456789abcdef0123456789abcdef"}
```

It accepts only an integer from 0 through 100 and the exact listed fields.

Replies arrive through Action responses:

```json
{"type":"response","sequence":12,"session":"0123456789abcdef0123456789abcdef","status":"success","message":"Action completed"}
```

The device retries an unacknowledged action at 1.6-second intervals, up to three
transmissions. The central serializes actions and caches the latest 64 responses.
Repeated sequences return the cached reply; older uncached sequences are
rejected. A reconnect discards pending device actions and begins a new session.
The deck displays success/error borders and a readable error message.

Local profile selection sends a `profile_select` notification with `profile`,
`sequence` and `session`. Each action still names its profile explicitly.

## Telemetry

Telemetry is written approximately every 1.5 seconds when changed, with a forced
heartbeat at least every 5 seconds. Fields are:

```json
{"type":"telemetry","sequence":101,"session":"0123456789abcdef0123456789abcdef","cpu":24.5,"ram":58,"disk":65,"gpu":null,"gpu_temp":null,"cpu_temp":null,"battery":null,"plugged":null,"upload":2.4,"download":120,"volume":42,"muted":false,"pc_name":"Windows PC","epoch":1793952000,"utc_offset":19800,"media":null}
```

The numeric values above are protocol examples, not collected data. Upload and
download are **KiB/s**, derived from successive system network counters. Usage
and battery values are percentages; temperature is Celsius. Unavailable metrics
must be `null`. A media object, when provided by Windows, contains `title`,
`artist` and `playing`. Strings are bounded UTF-8.

Telemetry sequences must increase within their session. Percentages, temperatures,
network rates, time, booleans and strings are bounds-checked. JSON has a nesting
limit of 12, duplicate keys are rejected, escaped NUL and invalid UTF-8 are
rejected before firmware parsing, and unknown message types/fields are refused.

The deck hides stale live values after 6.5 seconds and terminates a stalled
application session after 15 seconds without valid traffic. BLE supervision
handles physical link loss; Windows discovery and reconnect use bounded backoff.
