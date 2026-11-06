#pragma once
#include <cstddef>
#include <cstdint>

namespace nexus::icons {
inline constexpr unsigned kSize = 32, kPixels = kSize * kSize, kMaxFrames = 16;
inline constexpr size_t kMaxBytes = 10 + kMaxFrames * 2 + 256 * 4 + kPixels * kMaxFrames;
bool valid_id(const char* id);
struct IconData {
    const uint8_t *timings = nullptr, *palette = nullptr, *pixels = nullptr;
    uint16_t colors = 0;
    uint8_t frames = 0;
    static bool parse(const uint8_t* data, size_t size, IconData& out);
    uint16_t duration(unsigned frame) const;
    void decode(unsigned frame, uint8_t* rgb565_a8) const;
};
}
