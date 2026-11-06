#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "sdkconfig.h"

namespace nexus::config {
inline constexpr char kName[] = "NEXUS DECK";
inline constexpr char kVersion[] = "1.1.1";
inline constexpr int kDc = 13, kWr = 15, kReset = -1;
inline constexpr int kNativeWidth = 320, kNativeHeight = 480;
// Bit zero is D0. Never reorder this working 8080 bus.
inline constexpr std::array<int8_t, 16> kData = {
    8, 21, 18, 17, 16, 6, 4, 2, 20, 19, 10, 7, 12, 11, 14, 9};
inline constexpr int kTouchClock = 47, kTouchCs = 48;
inline constexpr int kTouchDin = 43, kTouchDout = 44;
inline constexpr int kPressureThreshold = 600;
// No backlight control wire was supplied. -1 uses a real LVGL dimming layer.
// Assign only a verified, free GPIO connected to a backlight MOSFET/PWM input.
#ifndef NEXUS_BACKLIGHT_GPIO
#define NEXUS_BACKLIGHT_GPIO -1
#endif
inline constexpr int kBacklight = NEXUS_BACKLIGHT_GPIO;
inline constexpr bool kInvertColors = false;
inline constexpr size_t kMaxMessage = 49152;
inline constexpr size_t kMaxProfiles = 8, kMaxButtons = 24;
inline constexpr int kDrawLines = 24;
inline constexpr uint32_t kIdleSeconds = 90;
inline constexpr uint32_t kPairingSeconds = 120;
inline constexpr uint32_t kHeartbeatTimeoutMs = 15000;

constexpr bool valid_pins() {
    std::array<int, 23> pins{};
    for (size_t i = 0; i < kData.size(); ++i) pins[i] = kData[i];
    pins[16] = kDc; pins[17] = kWr;
    pins[18] = kTouchClock; pins[19] = kTouchCs;
    pins[20] = kTouchDin; pins[21] = kTouchDout; pins[22] = kBacklight;
    for (size_t i = 0; i < pins.size(); ++i) {
        if (pins[i] < 0) continue;
        if (pins[i] > 48 || (pins[i] >= 22 && pins[i] <= 25) ||
            (pins[i] >= 26 && pins[i] <= 37)) return false;
        for (size_t j = i + 1; j < pins.size(); ++j)
            if (pins[i] == pins[j]) return false;
        if (kReset >= 0 && pins[i] == kReset) return false;
    }
    for (auto pin : kData) if (pin >= 32) return false;
    return true;
}
static_assert(valid_pins(), "NEXUS pin conflict or reserved flash/PSRAM GPIO");
static_assert(kDc < 32 && kWr < 32, "Low-register LCD control pins required");
#if !CONFIG_ESP_CONSOLE_NONE
#error "GPIO43/44 are the XPT2046 bus: disable the UART0 console"
#endif
#ifdef CONFIG_ESP_PHY_ENABLE_USB
#error "GPIO19/20 are the LCD bus: RF calibration must not re-enable USB pads"
#endif
}
