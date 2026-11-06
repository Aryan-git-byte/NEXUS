#include "storage.hpp"
#include <cstdlib>
#include <cstring>
#include "nvs.h"
#include "nvs_flash.h"
#include "assets/icon_data.hpp"
#include <array>
#include <vector>

namespace nexus {
namespace {
esp_err_t write_blob(const char* space, const char* key, const void* value, size_t size) {
    nvs_handle_t handle;
    esp_err_t result = nvs_open(space, NVS_READWRITE, &handle);
    if (result != ESP_OK) return result;
    result = nvs_set_blob(handle, key, value, size);
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle); return result;
}
esp_err_t read_blob(const char* space, const char* key, void* value, size_t size) {
    nvs_handle_t handle;
    esp_err_t result = nvs_open(space, NVS_READONLY, &handle);
    if (result != ESP_OK) return result;
    size_t length = size;
    result = nvs_get_blob(handle, key, value, &length);
    nvs_close(handle);
    return result == ESP_OK && length != size ? ESP_ERR_INVALID_SIZE : result;
}
}
esp_err_t Storage::initialize() {
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        result = nvs_flash_erase();
        if (result == ESP_OK) result = nvs_flash_init();
        recovered_ = result == ESP_OK;
    }
    if (result != ESP_OK) return result;
    result = nvs_flash_init_partition("icon_store");
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        result = nvs_flash_erase_partition("icon_store");
        if (result == ESP_OK) result = nvs_flash_init_partition("icon_store");
    }
    return result;
}
esp_err_t Storage::load_settings(DeviceSettings& settings) {
    DeviceSettings candidate;
    esp_err_t result = read_blob("nexus", "settings", &candidate, sizeof(candidate));
    if (result != ESP_OK) return result;
    if (candidate.signature != 0x4e445331 || static_cast<uint8_t>(candidate.orientation) > 1 ||
        candidate.brightness < 5 || candidate.brightness > 100 || candidate.theme > 1 ||
        candidate.idle_seconds < 15 || candidate.idle_seconds > 3600 || std::abs(candidate.utc_offset) > 50400 ||
        !std::memchr(candidate.active_profile, 0, sizeof(candidate.active_profile)) ||
        !valid_identifier(candidate.active_profile)) return ESP_ERR_INVALID_STATE;
    settings = candidate; return ESP_OK;
}
esp_err_t Storage::save_settings(const DeviceSettings& settings) {
    return write_blob("nexus", "settings", &settings, sizeof(settings));
}
esp_err_t Storage::load_apps(apps::Preferences& preferences) {
    apps::Preferences candidate;
    const esp_err_t result = read_blob("nexus_apps","prefs_v1",&candidate,sizeof(candidate));
    if (result != ESP_OK) return result;
    if (!apps::valid_preferences(candidate)) return ESP_ERR_INVALID_STATE;
    preferences = candidate; return ESP_OK;
}
esp_err_t Storage::save_apps(const apps::Preferences& preferences) {
    if (!apps::valid_preferences(preferences)) return ESP_ERR_INVALID_ARG;
    return write_blob("nexus_apps","prefs_v1",&preferences,sizeof(preferences));
}
esp_err_t Storage::load_profiles(ProfileSet& profiles) {
    nvs_handle_t handle;
    esp_err_t result = nvs_open("nexus", NVS_READONLY, &handle);
    if (result != ESP_OK) return result;
    size_t size = 0;
    result = nvs_get_str(handle, "profiles", nullptr, &size);
    if (result != ESP_OK || size < 2 || size > config::kMaxMessage + 1) { nvs_close(handle); return ESP_ERR_INVALID_SIZE; }
    char* buffer = static_cast<char*>(std::malloc(size));
    if (!buffer) { nvs_close(handle); return ESP_ERR_NO_MEM; }
    result = nvs_get_str(handle, "profiles", buffer, &size); nvs_close(handle);
    if (result == ESP_OK) {
        cJSON* root = parse_json(reinterpret_cast<uint8_t*>(buffer), size - 1);
        char error[96];
        if (!root || !parse_profiles(root, profiles, error, sizeof(error))) result = ESP_ERR_INVALID_STATE;
        cJSON_Delete(root);
    }
    std::free(buffer); return result;
}
esp_err_t Storage::save_profiles(const char* json) {
    if (!json || std::strlen(json) > config::kMaxMessage) return ESP_ERR_INVALID_SIZE;
    nvs_handle_t handle;
    esp_err_t result = nvs_open("nexus", NVS_READWRITE, &handle);
    if (result != ESP_OK) return result;
    result = nvs_set_str(handle, "profiles", json);
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle); return result;
}
esp_err_t Storage::load_identity(BondIdentity& identity) {
    esp_err_t result = read_blob("nexus_sec", "peer", &identity, sizeof(identity));
    if (result == ESP_OK && identity.type > 1) return ESP_ERR_INVALID_STATE;
    return result;
}
esp_err_t Storage::save_identity(const BondIdentity& identity) {
    return write_blob("nexus_sec", "peer", &identity, sizeof(identity));
}
esp_err_t Storage::forget_identity() {
    nvs_handle_t handle;
    esp_err_t result = nvs_open("nexus_sec", NVS_READWRITE, &handle);
    if (result != ESP_OK) return result;
    result = nvs_erase_all(handle);
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle); return result;
}
esp_err_t Storage::save_icon(const char* id, const uint8_t* data, size_t size) {
    icons::IconData image;
    if (!icons::valid_id(id) || !icons::IconData::parse(data,size,image)) return ESP_ERR_INVALID_ARG;
    uint8_t* old = nullptr; size_t old_size = 0;
    if (load_icon(id,old,old_size) == ESP_OK) {
        const bool same = size == old_size && std::memcmp(data,old,size) == 0;
        std::free(old); return same ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    nvs_handle_t handle;
    esp_err_t result = nvs_open_from_partition("icon_store","images",NVS_READWRITE,&handle);
    if (result != ESP_OK) return result;
    result = nvs_set_blob(handle,id,data,size);
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle); return result;
}
esp_err_t Storage::load_icon(const char* id, uint8_t*& data, size_t& size) {
    data = nullptr; size = 0;
    if (!icons::valid_id(id)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t result = nvs_open_from_partition("icon_store","images",NVS_READONLY,&handle);
    if (result != ESP_OK) return result;
    result = nvs_get_blob(handle,id,nullptr,&size);
    if (result != ESP_OK || size < 10 || size > icons::kMaxBytes) {
        nvs_close(handle); return result == ESP_OK ? ESP_ERR_INVALID_SIZE : result;
    }
    auto* buffer = static_cast<uint8_t*>(std::malloc(size));
    if (!buffer) { nvs_close(handle); return ESP_ERR_NO_MEM; }
    result = nvs_get_blob(handle,id,buffer,&size); nvs_close(handle);
    icons::IconData icon;
    if (result == ESP_OK && !icons::IconData::parse(buffer,size,icon)) result = ESP_ERR_INVALID_STATE;
    if (result != ESP_OK) { std::free(buffer); return result; }
    data = buffer; return ESP_OK;
}
bool Storage::has_icon(const char* id) {
    uint8_t* data = nullptr; size_t size = 0;
    const bool present = load_icon(id,data,size) == ESP_OK;
    std::free(data); return present;
}
esp_err_t Storage::purge_icons(const ProfileSet& profiles) {
    nvs_handle_t handle;
    esp_err_t result = nvs_open_from_partition("icon_store","images",NVS_READWRITE,&handle);
    if (result != ESP_OK) return result;
    std::vector<std::array<char,16>> stale;
    nvs_iterator_t iterator = nullptr;
    esp_err_t next = nvs_entry_find("icon_store","images",NVS_TYPE_BLOB,&iterator);
    while (next == ESP_OK) {
        nvs_entry_info_t info{}; nvs_entry_info(iterator,&info);
        bool used = false;
        for (unsigned p = 0; p < profiles.count && !used; ++p)
            for (unsigned b = 0; b < profiles.profiles[p].count; ++b)
                if (std::strcmp(info.key,profiles.profiles[p].buttons[b].icon_asset) == 0) { used = true; break; }
        if (!used) { std::array<char,16> key{}; std::memcpy(key.data(),info.key,16); stale.push_back(key); }
        next = nvs_entry_next(&iterator);
    }
    nvs_release_iterator(iterator);
    for (const auto& key : stale) {
        result = nvs_erase_key(handle,key.data());
        if (result != ESP_OK && result != ESP_ERR_NVS_NOT_FOUND) { nvs_close(handle); return result; }
    }
    result = nvs_commit(handle); nvs_close(handle); return result;
}
esp_err_t Storage::factory_reset() {
    esp_err_t result = nvs_flash_erase_partition("icon_store");
    return result == ESP_OK ? nvs_flash_erase() : result;
}
}
