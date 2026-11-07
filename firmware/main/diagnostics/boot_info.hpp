#pragma once
#include <cstdint>

namespace nexus {
struct BootInfo {
    uint32_t id;
    const char* reset_reason;
};
const char* reset_reason_name(int reason);
// Captured once per boot, shared by the UI and authenticated BLE descriptor.
const BootInfo& boot_info();
}
