#include "ble_protocol.hpp"
#include <algorithm>
#include <cstring>

namespace nexus::wire {
namespace {
uint16_t read16(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
uint32_t read32(const uint8_t* p) { return uint32_t(read16(p)) | (uint32_t(read16(p + 2)) << 16); }
void write16(uint8_t* p, uint16_t n) { p[0] = n & 255; p[1] = n >> 8; }
void write32(uint8_t* p, uint32_t n) { write16(p, n & 65535); write16(p + 2, n >> 16); }
}
uint32_t crc32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xffffffffu;
}
size_t frame(uint8_t* output, size_t capacity, const uint8_t* data, size_t total,
             uint16_t id, size_t offset, uint32_t crc) {
    if (!output || !data || capacity <= kHeaderSize || capacity > kMaxPacket ||
        total == 0 || total > 12288 || offset >= total || id == 0) return 0;
    const size_t count = std::min(capacity - kHeaderSize, total - offset);
    output[0] = 'N'; output[1] = 'D'; output[2] = kVersion; output[3] = 0;
    write16(output + 4, id); write16(output + 6, total); write16(output + 8, offset);
    write16(output + 10, count); write32(output + 12, crc);
    std::memcpy(output + kHeaderSize, data + offset, count);
    return count + kHeaderSize;
}
void Reassembler::reset() { used_ = 0; message_id_ = total_ = 0; checksum_ = 0; last_packet_ms_ = 0; }
FeedResult Reassembler::feed(const uint8_t* packet, size_t size, uint64_t now) {
    auto invalid = [this]() { reset(); return FeedResult::Invalid; };
    if (!packet || size <= kHeaderSize || size > kMaxPacket) return invalid();
    const uint16_t id = read16(packet + 4), total = read16(packet + 6);
    const uint16_t offset = read16(packet + 8), count = read16(packet + 10);
    const uint32_t crc = read32(packet + 12);
    if (packet[0] != 'N' || packet[1] != 'D' || packet[2] != kVersion || packet[3] != 0 ||
        id == 0 || total == 0 || total > capacity_ || count != size - kHeaderSize ||
        size_t(offset) + count > total) return invalid();
    if (used_ && now - last_packet_ms_ > 10000) reset();
    if (offset == 0 && (id != message_id_ || used_ == 0)) {
        used_ = 0; message_id_ = id; total_ = total; checksum_ = crc;
    }
    if (id != message_id_ || total != total_ || crc != checksum_) return invalid();
    if (offset < used_) {
        if (size_t(offset) + count > used_ || std::memcmp(buffer_ + offset, packet + kHeaderSize, count) != 0)
            return invalid();
        last_packet_ms_ = now; return FeedResult::Incomplete;
    }
    if (offset != used_) return invalid();
    std::memcpy(buffer_ + offset, packet + kHeaderSize, count);
    used_ += count; last_packet_ms_ = now;
    if (used_ != total) return FeedResult::Incomplete;
    if (crc32(buffer_, used_) != checksum_) return invalid();
    // The owner consumes data before reset(). The extra byte belongs to the owner, not the protocol.
    return FeedResult::Complete;
}
}
