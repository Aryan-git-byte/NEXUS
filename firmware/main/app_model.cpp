#include "app_model.hpp"
#include "assets/icon_data.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include "esp_timer.h"
#include "mbedtls/base64.h"

namespace nexus {
QueueHandle_t ui_events = nullptr, telemetry_mailbox = nullptr;
bool post_ui(const UiEvent& event) { return ui_events && xQueueSend(ui_events, &event, 0) == pdTRUE; }
uint64_t now_ms() { return esp_timer_get_time() / 1000; }

bool valid_identifier(const char* value) {
    if (!value || value[0] < 'a' || value[0] > 'z') return false;
    size_t size = std::strlen(value);
    if (size > 32) return false;
    for (size_t i = 0; i < size; ++i) {
        char c = value[i];
        if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '_' && c != '-') return false;
    }
    return true;
}
bool valid_session(const cJSON* root, const char* expected) {
    const auto* value = cJSON_GetObjectItemCaseSensitive(root, "session");
    if (!cJSON_IsString(value) || std::strlen(value->valuestring) != 32) return false;
    for (const char* c = value->valuestring; *c; ++c)
        if (!(*c >= '0' && *c <= '9') && !(*c >= 'a' && *c <= 'f')) return false;
    return !expected || std::strcmp(value->valuestring, expected) == 0;
}
bool valid_sequence(const cJSON* root, uint32_t& seq) {
    const auto* value = cJSON_GetObjectItemCaseSensitive(root, "sequence");
    if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble) || value->valuedouble < 1 ||
        value->valuedouble > 4294967295.0 || std::floor(value->valuedouble) != value->valuedouble) return false;
    seq = static_cast<uint32_t>(value->valuedouble); return true;
}
namespace {
bool preflight(const uint8_t* data, size_t length) {
    bool quoted = false;
    unsigned depth = 0;
    for (size_t i = 0; i < length; ++i) {
        uint8_t c = data[i];
        if (c >= 0x80) {
            unsigned extra = c >= 0xc2 && c <= 0xdf ? 1 : c >= 0xe0 && c <= 0xef ? 2 : c >= 0xf0 && c <= 0xf4 ? 3 : 0;
            if (!extra || i + extra >= length) return false;
            uint32_t point = c & ((1u << (6 - extra)) - 1);
            for (unsigned j = 0; j < extra; ++j) {
                uint8_t continuation = data[++i];
                if ((continuation & 0xc0) != 0x80) return false;
                point = (point << 6) | (continuation & 0x3f);
            }
            if ((extra == 1 && point < 0x80) || (extra == 2 && point < 0x800) ||
                (extra == 3 && point < 0x10000) || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff)) return false;
            continue;
        }
        if (quoted && c == '\\') {
            if (++i >= length) return false;
            // cJSON uses C strings; escaped NUL must be rejected before it can truncate a key or ID.
            if (data[i] == 'u' && i + 4 < length && data[i+1] == '0' && data[i+2] == '0' &&
                data[i+3] == '0' && data[i+4] == '0') return false;
            continue;
        }
        if (c == '"') quoted = !quoted;
        else if (!quoted && (c == '{' || c == '[')) { if (++depth > 12) return false; }
        else if (!quoted && (c == '}' || c == ']')) { if (depth == 0) return false; --depth; }
    }
    return !quoted && depth == 0;
}
bool tree_valid(const cJSON* node, unsigned depth) {
    if (depth > 12) return false;
    if (cJSON_IsNumber(node) && !std::isfinite(node->valuedouble)) return false;
    size_t count = 0;
    for (auto* child = node->child; child; child = child->next) {
        if (++count > (cJSON_IsObject(node) ? 32u : 256u)) return false;
        if (cJSON_IsObject(node)) {
            if (!child->string || std::strlen(child->string) > 64) return false;
            for (auto* other = child->next; other; other = other->next)
                if (other->string && std::strcmp(child->string, other->string) == 0) return false;
        }
        if (!tree_valid(child, depth + 1)) return false;
    }
    return true;
}
bool number(const cJSON* root, const char* key, float& value, float low, float high) {
    const auto* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!item || cJSON_IsNull(item)) { value = -1; return true; }
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) ||
        item->valuedouble < low || item->valuedouble > high) return false;
    value = item->valuedouble; return true;
}
bool boolean(const cJSON* root, const char* key, bool& output) {
    const auto* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!item || cJSON_IsNull(item)) { output = false; return true; }
    if (!cJSON_IsBool(item)) return false;
    output = cJSON_IsTrue(item); return true;
}
bool match(const char* text, const char* const* values, size_t count, size_t& index) {
    for (size_t i = 0; i < count; ++i) if (std::strcmp(text, values[i]) == 0) { index = i; return true; }
    return false;
}
}
cJSON* parse_json(const uint8_t* data, size_t length) {
    if (!data || length == 0 || length > config::kMaxMessage || std::memchr(data, 0, length) || !preflight(data,length)) return nullptr;
    // Buffers supplied by the transport always have one extra terminator byte.
    const char* end = nullptr;
    cJSON* root = cJSON_ParseWithLengthOpts(reinterpret_cast<const char*>(data), length + 1, &end, true);
    if (!root || !cJSON_IsObject(root) || !tree_valid(root, 0)) { cJSON_Delete(root); return nullptr; }
    return root;
}
bool json_fields(const cJSON* root, const char* const* allowed, size_t count) {
    if (!cJSON_IsObject(root)) return false;
    for (const auto* child = root->child; child; child = child->next) {
        size_t ignored;
        if (!child->string || !match(child->string, allowed, count, ignored)) return false;
    }
    return true;
}
bool json_text(const cJSON* root, const char* key, char* output, size_t size, bool empty) {
    const auto* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsString(item) || !item->valuestring) return false;
    size_t length = std::strlen(item->valuestring);
    if (length >= size || (!empty && length == 0)) return false;
    for (size_t i = 0; i < length; ++i) if (static_cast<uint8_t>(item->valuestring[i]) < 32) return false;
    std::memcpy(output, item->valuestring, length + 1); return true;
}

bool parse_profiles(const cJSON* root, ProfileSet& out, char* error, size_t error_size) {
    auto fail = [&](const char* why) { std::snprintf(error, error_size, "%s", why); return false; };
    const char* const root_fields[] = {"type", "schema", "sequence", "session", "active", "profiles"};
    if (!json_fields(root, root_fields, 6)) return fail("Unexpected profile bundle field");
    const auto* schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
    if (!cJSON_IsNumber(schema) || schema->valuedouble != 1) return fail("Unsupported profile schema");
    const auto* profiles = cJSON_GetObjectItemCaseSensitive(root, "profiles");
    const int count = cJSON_GetArraySize(profiles);
    if (!cJSON_IsArray(profiles) || count < 1 || count > int(config::kMaxProfiles)) return fail("Profile count out of range");
    char active[33];
    if (!json_text(root, "active", active, sizeof(active)) || !valid_identifier(active)) return fail("Invalid active profile");
    out.count = count; out.active = 255;
    const char* const categories[] = {"default", "development", "media", "gaming", "productivity", "custom"};
    const char* const icons[] = {"code","terminal","powershell","folder","github","project","server","camera",
        "play","next","previous","volume_up","volume_down","mute","copy","paste","cut","undo","save",
        "desktop","lock","browser","text","game","mail","calendar","search","star","custom"};
    for (int i = 0; i < count; ++i) {
        Profile& p = out.profiles[i];
        const auto* item = cJSON_GetArrayItem(profiles, i);
        const char* const fields[] = {"id", "name", "category", "revision", "buttons"};
        if (!json_fields(item, fields, 5) || !json_text(item, "id", p.id, sizeof(p.id)) || !valid_identifier(p.id) ||
            !json_text(item, "name", p.name, sizeof(p.name))) return fail("Invalid profile metadata");
        for (int j = 0; j < i; ++j) if (std::strcmp(p.id, out.profiles[j].id) == 0) return fail("Duplicate profile ID");
        if (std::strcmp(p.id, active) == 0) out.active = i;
        char category[17]; size_t category_index;
        if (!json_text(item, "category", category, sizeof(category)) || !match(category, categories, 6, category_index))
            return fail("Invalid profile category");
        p.category = static_cast<Category>(category_index);
        const auto* revision = cJSON_GetObjectItemCaseSensitive(item, "revision");
        if (!cJSON_IsNumber(revision) || revision->valuedouble < 1 || revision->valuedouble > 2147483647.0 ||
            std::floor(revision->valuedouble) != revision->valuedouble) return fail("Invalid revision");
        p.revision = static_cast<uint32_t>(revision->valuedouble);
        const auto* buttons = cJSON_GetObjectItemCaseSensitive(item, "buttons");
        int button_count = cJSON_GetArraySize(buttons);
        if (!cJSON_IsArray(buttons) || button_count < 1 || button_count > int(config::kMaxButtons)) return fail("Invalid button count");
        p.count = button_count;
        p.buttons.reset(new(std::nothrow) Button[button_count]);
        if (!p.buttons) return fail("Not enough memory for profile buttons");
        for (int j = 0; j < button_count; ++j) {
            Button& b = p.buttons[j];
            const auto* button = cJSON_GetArrayItem(buttons, j);
            const char* const keys[] = {"id", "label", "icon", "icon_data", "icon_asset", "has_long_press"};
            size_t ignored;
            if (!json_fields(button, keys, 6) || !json_text(button, "id", b.id, sizeof(b.id)) || !valid_identifier(b.id) ||
                !json_text(button, "label", b.label, sizeof(b.label)) || !json_text(button, "icon", b.icon, sizeof(b.icon)) ||
                !match(b.icon, icons, sizeof(icons) / sizeof(icons[0]), ignored) ||
                !boolean(button, "has_long_press", b.has_long_press)) return fail("Invalid button");
            for (int k = 0; k < j; ++k) if (std::strcmp(b.id, p.buttons[k].id) == 0) return fail("Duplicate button ID");
            const auto* image = cJSON_GetObjectItemCaseSensitive(button, "icon_data");
            b.has_icon = image != nullptr;
            if (cJSON_GetObjectItemCaseSensitive(button,"icon_asset")) {
                if (b.has_icon || !json_text(button,"icon_asset",b.icon_asset,sizeof(b.icon_asset)) ||
                    !icons::valid_id(b.icon_asset)) return fail("Invalid color icon reference");
            }
            if (b.has_icon) {
                size_t length = 0;
                if (!cJSON_IsString(image) || std::strlen(image->valuestring) != 96 ||
                    mbedtls_base64_decode(b.bitmap, sizeof(b.bitmap), &length,
                        reinterpret_cast<const uint8_t*>(image->valuestring), 96) != 0 || length != 72)
                    return fail("Invalid custom icon");
            }
        }
    }
    if (out.active == 255) return fail("Active profile does not exist");
    return true;
}

bool json_clock(const cJSON* root, int64_t& timestamp, int32_t& utc_offset) {
    const auto* epoch = cJSON_GetObjectItemCaseSensitive(root,"epoch");
    const auto* offset = cJSON_GetObjectItemCaseSensitive(root,"utc_offset");
    if (!cJSON_IsNumber(epoch) || !std::isfinite(epoch->valuedouble) ||
        epoch->valuedouble < 1700000000.0 || epoch->valuedouble > 4102444800.0 ||
        std::floor(epoch->valuedouble) != epoch->valuedouble || !cJSON_IsNumber(offset) ||
        !std::isfinite(offset->valuedouble) || std::fabs(offset->valuedouble) > 50400 ||
        std::floor(offset->valuedouble) != offset->valuedouble) return false;
    timestamp = static_cast<int64_t>(epoch->valuedouble);
    utc_offset = static_cast<int32_t>(offset->valuedouble);
    return true;
}

bool parse_telemetry(const cJSON* root, Telemetry& out) {
    const char* const fields[] = {"type","sequence","session","cpu","ram","disk","gpu","gpu_temp","cpu_temp",
        "battery","plugged","upload","download","volume","muted","epoch","utc_offset","pc_name","media"};
    if (!json_fields(root, fields, sizeof(fields) / sizeof(fields[0]))) return false;
    if (!number(root,"cpu",out.cpu,0,100) || !number(root,"ram",out.ram,0,100) || !number(root,"disk",out.disk,0,100) ||
        !number(root,"gpu",out.gpu,0,100) || !number(root,"battery",out.battery,0,100) ||
        !number(root,"cpu_temp",out.cpu_temp,0,150) || !number(root,"gpu_temp",out.gpu_temp,0,150) ||
        !number(root,"upload",out.upload,0,1e9) || !number(root,"download",out.download,0,1e9) ||
        !number(root,"volume",out.volume,0,100) || !boolean(root,"muted",out.muted) || !boolean(root,"plugged",out.plugged) ||
        !json_text(root,"pc_name",out.pc_name,sizeof(out.pc_name))) return false;
    if (!json_clock(root,out.epoch,out.utc_offset)) return false;
    const auto* media = cJSON_GetObjectItemCaseSensitive(root,"media");
    out.media_available = media && !cJSON_IsNull(media);
    if (out.media_available) {
        const char* const keys[] = {"title", "artist", "playing"};
        if (!json_fields(media,keys,3) || !json_text(media,"title",out.media_title,sizeof(out.media_title),true) ||
            !json_text(media,"artist",out.media_artist,sizeof(out.media_artist),true) || !boolean(media,"playing",out.playing)) return false;
    }
    out.received_ms = now_ms(); return true;
}
}
