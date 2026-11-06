#pragma once
#include "app_model.hpp"
#include "esp_err.h"

namespace nexus {
struct BondIdentity { uint8_t type = 0, address[6]{}; };
class Storage {
public:
    esp_err_t initialize();
    esp_err_t load_settings(DeviceSettings& settings);
    esp_err_t save_settings(const DeviceSettings& settings);
    esp_err_t load_profiles(ProfileSet& profiles);
    esp_err_t save_profiles(const char* json);
    esp_err_t load_identity(BondIdentity& identity);
    esp_err_t save_identity(const BondIdentity& identity);
    esp_err_t forget_identity();
    esp_err_t factory_reset();
    esp_err_t save_icon(const char* id, const uint8_t* data, size_t size);
    esp_err_t load_icon(const char* id, uint8_t*& data, size_t& size);
    bool has_icon(const char* id);
    esp_err_t purge_icons(const ProfileSet& profiles);
    bool recovered() const { return recovered_; }
private:
    bool recovered_ = false;
};
}
