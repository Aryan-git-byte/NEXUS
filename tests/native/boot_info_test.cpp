#include "diagnostics/boot_info.hpp"
#include "esp_system.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace {
unsigned random_calls = 0, reset_calls = 0;
esp_reset_reason_t simulated_reset = ESP_RST_BROWNOUT;
void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr,"Boot diagnostic check failed: %s\n",message); std::exit(1); }
}
}
uint32_t esp_random() { ++random_calls; return 0x12ab34cd; }
esp_reset_reason_t esp_reset_reason() { ++reset_calls; return simulated_reset; }
int main() {
    const char* names[] = {"unknown","power_on","external","software","panic","interrupt_watchdog",
        "task_watchdog","watchdog","deep_sleep","brownout","sdio","usb","jtag","efuse","power_glitch","cpu_lockup"};
    for (int reason = 0; reason < 16; ++reason)
        require(std::strcmp(nexus::reset_reason_name(reason),names[reason]) == 0,"reset-reason ABI classification");
    require(std::strcmp(nexus::reset_reason_name(-1),"unknown") == 0,"negative unknown reason");
    require(std::strcmp(nexus::reset_reason_name(999),"unknown") == 0,"future unknown reason");
    std::puts("Reset reason classification: all ESP-IDF 5.5 causes passed");
    const auto& first = nexus::boot_info();
    require(first.id == 0x12ab34cd && std::strcmp(first.reset_reason,"brownout") == 0,"actual boot snapshot");
    simulated_reset = ESP_RST_SW;
    for (unsigned read = 0; read < 1000; ++read) {
        const auto& next = nexus::boot_info();
        require(&next == &first && next.id == first.id && std::strcmp(next.reset_reason,"brownout") == 0,
                "descriptor/UI reads retain the same boot identity");
    }
    require(random_calls == 1 && reset_calls == 1,"reset/RNG captured once per boot");
    std::puts("Boot identity stability: 1000 reads passed");
    std::puts("2 native boot-diagnostic groups passed");
    return 0;
}
