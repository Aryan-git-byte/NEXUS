#pragma once
#include <cstddef>
#include <cstdint>

namespace nexus::wire {
inline constexpr size_t kHeaderSize = 16, kMaxPacket = 512;
inline constexpr uint8_t kVersion = 1;
uint32_t crc32(const uint8_t* data, size_t size);
size_t frame(uint8_t* output, size_t capacity, const uint8_t* message, size_t total,
             uint16_t message_id, size_t offset, uint32_t checksum);
enum class FeedResult { Incomplete, Complete, Invalid };
class Reassembler {
public:
    Reassembler(uint8_t* buffer, size_t capacity) : buffer_(buffer), capacity_(capacity) {}
    FeedResult feed(const uint8_t* packet, size_t size, uint64_t now_ms);
    void reset();
    uint8_t* data() const { return buffer_; }
    size_t size() const { return used_; }
private:
    uint8_t* buffer_;
    size_t capacity_, used_ = 0;
    uint16_t message_id_ = 0, total_ = 0;
    uint32_t checksum_ = 0;
    uint64_t last_packet_ms_ = 0;
};
}
