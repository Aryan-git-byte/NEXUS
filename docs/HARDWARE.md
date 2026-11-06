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

## Brightness and board assumptions

No backlight control pin was supplied. Brightness therefore works through an
LVGL black dimming layer. It reduces visible brightness, but does not reduce
backlight electrical power. The screensaver dims the display and updates the
clock only when its text changes.

If a PWM backlight input/MOSFET exists, set `NEXUS_BACKLIGHT_GPIO` in
`firmware/main/config.hpp` to a verified free GPIO. The compile-time conflict
check must pass. The driver then uses active-high 1 kHz, 10-bit LEDC PWM and
disables the visual dimming layer. No GPIO is guessed or enabled by default.

Flash defaults to 16 MB, matching the existing workspace configuration. No PSRAM
is assumed or required. For an 8 MB board, change flash size in `idf.py menuconfig`;
the supplied partition table also fits 8 MB. The factory application partition
is 4 MB. Updates use the wired bootloader; OTA is not part of this BLE controller.
