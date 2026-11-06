#include "icon_data.hpp"
#include <cstring>

namespace nexus::icons {
bool valid_id(const char* id) {
    if (!id || std::strlen(id) != 14) return false;
    for (int i = 0; i < 14; ++i)
        if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f'))) return false;
    return true;
}
bool IconData::parse(const uint8_t* data, size_t size, IconData& out) {
    if (!data || size < 10 || size > kMaxBytes || std::memcmp(data,"NDI2",4) != 0 ||
        data[4] != kSize || data[5] != kSize || data[9] != 0) return false;
    const unsigned colors = data[6] | (static_cast<unsigned>(data[7]) << 8), frames = data[8];
    if (!colors || colors > 256 || !frames || frames > kMaxFrames) return false;
    const size_t offset = 10 + frames * 2;
    if (size != offset + colors * 4 + frames * kPixels) return false;
    IconData result;
    result.timings = data + 10; result.palette = data + offset;
    result.pixels = result.palette + colors * 4; result.colors = colors; result.frames = frames;
    for (unsigned frame = 0; frame < frames; ++frame)
        if (result.duration(frame) < 40 || result.duration(frame) > 5000) return false;
    for (unsigned pixel = 0; pixel < frames * kPixels; ++pixel)
        if (result.pixels[pixel] >= colors) return false;
    out = result; return true;
}
uint16_t IconData::duration(unsigned frame) const {
    frame %= frames;
    return timings[frame * 2] | (static_cast<uint16_t>(timings[frame * 2 + 1]) << 8);
}
void IconData::decode(unsigned frame, uint8_t* output) const {
    const auto* indices = pixels + (frame % frames) * kPixels;
    auto* rgb = reinterpret_cast<uint16_t*>(output);
    auto* alpha = output + kPixels * 2;
    for (unsigned pixel = 0; pixel < kPixels; ++pixel) {
        const auto* color = palette + indices[pixel] * 4;
        rgb[pixel] = ((color[0] & 0xf8) << 8) | ((color[1] & 0xfc) << 3) | (color[2] >> 3);
        alpha[pixel] = color[3];
    }
}
}
