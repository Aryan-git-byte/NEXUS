// Host-only ESP-IDF API boundary. The OTA state machine itself is production code.
#pragma once
#include <cstddef>
#include <cstdint>
#include "esp_err.h"
enum esp_partition_type_t { ESP_PARTITION_TYPE_APP = 0, ESP_PARTITION_TYPE_DATA = 1 };
enum esp_partition_subtype_t {
    ESP_PARTITION_SUBTYPE_APP_FACTORY = 0,
    ESP_PARTITION_SUBTYPE_APP_OTA_MIN = 0x10,
    ESP_PARTITION_SUBTYPE_APP_OTA_0 = 0x10,
    ESP_PARTITION_SUBTYPE_APP_OTA_1 = 0x11,
    ESP_PARTITION_SUBTYPE_APP_OTA_MAX = 0x20,
    ESP_PARTITION_SUBTYPE_DATA_OTA = 0
};
struct esp_partition_t {
    esp_partition_type_t type;
    esp_partition_subtype_t subtype;
    uint32_t address, size;
    char label[17];
};
enum esp_ota_img_states_t { ESP_OTA_IMG_NEW, ESP_OTA_IMG_PENDING_VERIFY, ESP_OTA_IMG_VALID, ESP_OTA_IMG_INVALID };
#define OTA_WITH_SEQUENTIAL_WRITES (SIZE_MAX - 1)
const esp_partition_t* esp_partition_find_first(esp_partition_type_t, esp_partition_subtype_t, const char*);
const esp_partition_t* esp_ota_get_running_partition();
const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*);
esp_err_t esp_ota_get_state_partition(const esp_partition_t*, esp_ota_img_states_t*);
esp_err_t esp_ota_begin(const esp_partition_t*, size_t, uint32_t*);
esp_err_t esp_ota_write(uint32_t, const void*, size_t);
esp_err_t esp_ota_abort(uint32_t);
esp_err_t esp_ota_end(uint32_t);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t*);
esp_err_t esp_ota_mark_app_valid_cancel_rollback();
