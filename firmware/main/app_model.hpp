#pragma once
#include <cstdint>
#include <memory>
#include "cJSON.h"
#include "config.hpp"
#include "display/lcd_driver.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace nexus {
enum class Category : uint8_t { Default, Development, Media, Gaming, Productivity, Custom };
struct Button {
    char id[33]{}, label[49]{}, icon[17]{};
    bool has_long_press = false, has_icon = false;
    uint8_t bitmap[72]{};
    char icon_asset[15]{};
};
struct Profile {
    char id[33]{}, name[41]{};
    Category category = Category::Custom;
    uint32_t revision = 1;
    uint8_t count = 0;
    std::unique_ptr<Button[]> buttons;
};
struct ProfileSet {
    uint8_t count = 0, active = 0;
    Profile profiles[config::kMaxProfiles]{};
};
struct DeviceSettings {
    uint32_t signature = 0x4e445331;
    Orientation orientation = Orientation::Landscape;
    uint8_t brightness = 100, theme = 0;
    bool screensaver = true;
    uint32_t idle_seconds = config::kIdleSeconds;
    int32_t utc_offset = 19800;
    char active_profile[33] = "default";
};
struct Telemetry {
    float cpu = -1, ram = -1, disk = -1, gpu = -1, gpu_temp = -1, cpu_temp = -1;
    float battery = -1, upload = -1, download = -1, volume = -1;
    bool muted = false, plugged = false, media_available = false, playing = false;
    int64_t epoch = 0;
    int32_t utc_offset = 0;
    uint64_t received_ms = 0;
    char pc_name[64]{}, media_title[128]{}, media_artist[64]{};
};
enum class UiEventKind : uint8_t { Link, Secure, Ready, Passkey, Response, Profiles, Notice, AppsSaved };
struct UiEvent {
    UiEventKind kind = UiEventKind::Notice;
    uint32_t sequence = 0;
    int32_t value = 0;
    ProfileSet* profiles = nullptr;
    char text[176]{};
};
extern QueueHandle_t ui_events, telemetry_mailbox;
bool post_ui(const UiEvent& event);
uint64_t now_ms();
bool valid_identifier(const char* text);
bool valid_session(const cJSON* root, const char* expected = nullptr);
bool valid_sequence(const cJSON* root, uint32_t& sequence);
cJSON* parse_json(const uint8_t* data, size_t length);
bool parse_profiles(const cJSON* root, ProfileSet& output, char* error, size_t error_size);
bool parse_telemetry(const cJSON* root, Telemetry& output);
bool json_clock(const cJSON* root, int64_t& epoch, int32_t& utc_offset);
bool json_fields(const cJSON* root, const char* const* allowed, size_t count);
bool json_text(const cJSON* root, const char* key, char* output, size_t size, bool empty = false);
}
