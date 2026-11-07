# Hardware and preserved references

The source baseline is preserved in `references/original/`. Its `src/main.cpp`
contains the working parallel ILI9488 and calibrated XPT2046 implementations.
The separate plasma-only file mentioned in the brief was not present in this
workspace; the available combined hardware reference was used. The production
project does not compile the old HID service or its startup routine.

## LCD: 8080 16-bit parallel, 320 × 480 native

| Signal | GPIO |
|---|---:|
| D0 | 8 |
| D1 | 21 |
| D2 | 18 |
| D3 | 17 |
| D4 | 16 |
| D5 | 6 |
| D6 | 4 |
| D7 | 2 |
| D8 | 20 |
| D9 | 19 |
| D10 | 10 |
| D11 | 7 |
| D12 | 12 |
| D13 | 11 |
| D14 | 14 |
| D15 | 9 |
| DC / RS | 13 |
| WR | 15 |
| Reset | -1: no software-controlled reset wire |

There is no CS or RD pin in the supplied reference. The project assumes the
module is already selected and read-disabled by its existing wiring (typically
CS low and RD high). Keep the module's interface straps in its working 16-bit
8080 mode. There is no SPI display driver in NEXUS DECK.

All LCD GPIOs are below 32. Lookup tables translate the low/high RGB565 bytes to
the original bit mapping. Data and control outputs use the ESP32 low GPIO
register bank. WR strobes follow the same sequence as the reference.

The production initialization keeps the original positive/negative gamma,
power, VCOM, interface, frame-rate, display-function and F7 data, sleep/reset
delays, MADCTL `0x48`, COLMOD `0x55`, and inversion setting. Landscape is a
software address transform:

```text
logical landscape -> native portrait: nx = 319 - y; ny = x
native portrait -> logical landscape: x = ny; y = 319 - nx
```

Calibration coefficients always describe native portrait coordinates. Switching
orientation does not erase or recompute them. The device's default logical
layout is 480 × 320; select Portrait in Settings for 320 × 480.

## XPT2046

| Signal | GPIO |
|---|---:|
| CLK | 47 |
| CS | 48 |
| DIN | 43 |
| DOUT | 44 |

Pressure is `Z1 + 4095 - Z2`, with threshold 600. Commands remain `0xB1`, `0xC1`,
`0xD1`, and `0x91`. Each axis uses five readings and a median. Three targets solve
a full affine transform, including shear, axis swaps and inversion. Thirty
filtered samples are averaged per target after settling.

NVS stores six `float` coefficients under namespace `touch`, key `cal`, exactly
as the reference did. Existing valid calibration can be reused. Fresh or invalid
calibration opens the calibration screen automatically. Hold each target until
green, lift the stylus, and continue. Settings can start a new calibration.

## Pin conflicts and power

The 22 supplied LCD and touch signals have no overlapping GPIO assignments.
Compile-time checks reject overlaps and reserved flash/PSRAM GPIOs if optional
pins are changed.

Two shared peripheral interfaces matter on ESP32-S3 boards:

* GPIO19/20 are native USB D-/D+, and are already LCD D9/D8 here. The application
  disables the USB serial/JTAG pads, matching the reference. Native USB data
  communication is unavailable while the LCD bus is active.
* GPIO43/44 are UART0 TX/RX, and are already touch DIN/DOUT here. Runtime UART0
  logging is disabled with `CONFIG_ESP_CONSOLE_NONE=y`.

Software disabling a console cannot remove a USB-UART bridge or connector that
is electrically wired to these signals. Inspect the board schematic and the
existing working wiring. For operation, isolate conflicting USB data and UART
bridge drive paths while supplying USB power. Use a power-only USB connection or
the board's appropriate isolated power input. ROM download-mode flashing occurs
before the application takes these pins; use the board's supported programmer
connection and disconnect conflicting drive paths afterward.

Use a common ground and ESP32-compatible 3.3 V logic. Supply the TFT and backlight
from the module's documented power input; do not power its backlight directly
from a GPIO. GPIO48 can also feed a board RGB LED; NEXUS does not install or run
an RGB LED driver on that pin.

### Waveshare ESP32-S3-Touch-LCD-2 carrier

The user identified the actual carrier through this
[manufacturer schematic](https://files.waveshare.com/wiki/ESP32-S3-LCD-2/ESP32-S3-Touch-LCD-2-SchDoc.pdf).
It has a W25Q128 16 MB flash, an ETA6098 battery charger and a TMI3112H
step-down converter. With no USB supply, the path is
`VBAT -> AO3401 Q1 -> VCC -> TMI3112H U7 -> 3V3`.
USB 5 V reaches VCC through Schottky D1; Q1 isolates the battery supply path
when USB is present. A single 3.7 V nominal, 4.2 V full lithium-ion cell,
including a suitable 18650, belongs on VBAT. Feed USB power through the USB/5V
input, not VBAT. An external 3.3 V regulator before VBAT leaves the onboard
buck without enough input headroom under load; it cannot boost that input.

The current enclosure uses one raw 18650 on VBAT. The external TFT/backlight
adds load to the board. A weak cell, low charge, protection-board resistance,
wiring/contact resistance or a supply transient can expose a reset during radio
activity. Confirm rail behavior or the actual reset cause before diagnosing it
as a brownout. Dimming pixels does not reduce the backlight current.

This carrier also connects GPIO47/48 to its onboard IMU and capacitive-touch
I2C bus, while the external XPT2046 requires CLK/CS on these pins. GPIO16/21
reach the camera's control bus, and GPIO19/20 reach the USB connector through
22-ohm resistors. Do not run the onboard I2C/camera/USB drivers concurrently
with the preserved external wiring. Disabling a driver does not electrically
isolate a connected peripheral. If either touch reliability or power behavior
changes with attached peripherals, inspect those shared nets before rewiring.

## Brightness and board assumptions

No backlight control pin was supplied. Brightness therefore works through an
LVGL black dimming layer. It reduces visible brightness, but does not reduce
backlight electrical power. The screensaver dims the display and updates the
clock only when its text changes.

If a PWM backlight input/MOSFET exists, set `NEXUS_BACKLIGHT_GPIO` in
`firmware/main/config.hpp` to a verified free GPIO. The compile-time conflict
check must pass. The driver then uses active-high 1 kHz, 10-bit LEDC PWM and
disables the visual dimming layer. No GPIO is guessed or enabled by default.

Flash is **16 MB**, matching the existing workspace configuration. The OTA
partition table requires that capacity; changing only the menuconfig flash size
does not make it fit an 8 MB board. No PSRAM is assumed or required. The factory
image and each of two OTA images have a 4 MB slot. Firmware updates use the
existing BLE radio, with no new GPIOs or Wi-Fi requirements.

| Partition | Offset | Size |
|---|---|---|
| Calibration, settings and BLE NVS | `0x9000` | `0x50000` |
| PHY initialization | `0x59000` | `0x1000` |
| OTA boot metadata | `0x5A000` | `0x2000` |
| USB factory/recovery application | `0x60000` | `0x400000` |
| Icon storage | `0x460000` | `0x200000` |
| OTA application 0 | `0x660000` | `0x400000` |
| OTA application 1 | `0xA60000` | `0x400000` |

The 1.2.0 NVS, factory and icon offsets are preserved. USB bootstrap initializes
the new boot metadata in an unused gap, then writes the OTA-enabled factory
application. USB recovery resets only boot metadata to select that factory
image; normal OTA alternates the two update slots. Calibration, pairing and
profiles survive. [OTA.md](OTA.md) describes the recovery flow.
