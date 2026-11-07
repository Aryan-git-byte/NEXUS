#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "cJSON.h"

namespace nexus {
// Only the BLE worker owns transfer state and performs flash operations.
class OtaManager {
public:
    static constexpr size_t kChunkBytes = 4096, kMessageBytes = 8192;
    OtaManager() = default;
    ~OtaManager();
    bool handle(const cJSON* message, uint32_t sequence, const char* session,
                char* response, size_t capacity);
    void abort(const char* reason = nullptr);
    void poll(bool ui_and_radio_healthy);
    bool active() const { return active_; }
    static bool supported();
    static bool boot_confirmed();
    static const char* running_slot();
    static void elf_digest(char* output, size_t capacity);
private:
    void feedback(int progress, const char* text, uint32_t phase = 1);
    void reply(char* output, size_t capacity, uint32_t sequence, const char* session,
               bool success, const char* message);
    bool first_block(const uint8_t* bytes, size_t length) const;
    std::array<uint8_t,kChunkBytes> buffer_{};
    const void* partition_ = nullptr;
    void* hash_ = nullptr;
    uint32_t handle_ = 0, size_ = 0, offset_ = 0, previous_offset_ = 0;
    size_t previous_size_ = 0;
    uint32_t previous_crc_ = 0;
    uint64_t activity_ = 0, restart_at_ = 0, boot_started_ = 0, healthy_since_ = 0;
    bool active_ = false, committed_ = false, boot_checked_ = false;
    char transfer_[33]{}, sha256_[65]{}, version_[33]{}, slot_[17]{};
};
}
