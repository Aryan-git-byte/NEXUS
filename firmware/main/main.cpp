#include <memory>
#include <new>
#include <cstring>
#include "app_model.hpp"
#include "bluetooth/ble_manager.hpp"
#include "display/lcd_driver.hpp"
#include "display/touch_driver.hpp"
#include "storage/storage.hpp"
#include "ui/ui_manager.hpp"
#include "esp_timer.h"
#include "freertos/task.h"

extern const uint8_t default_profiles_start[] asm("_binary_device_defaults_json_start");
extern const uint8_t default_profiles_end[] asm("_binary_device_defaults_json_end");
namespace {
nexus::Storage storage;
nexus::LcdDriver display;
nexus::TouchDriver touch;
nexus::BleManager bluetooth(storage);
[[noreturn]] void boot_error(const char* message) {
    auto* label = lv_label_create(lv_screen_active());
    lv_obj_set_width(label,display.width() - 40);
    lv_label_set_text(label,message); lv_obj_center(label);
    while (true) { lv_timer_handler(); vTaskDelay(pdMS_TO_TICKS(10)); }
}
void ui_task(void* context) {
    auto* ui = static_cast<nexus::UiManager*>(context);
    if (ui->initialize() != ESP_OK) boot_error("UI allocation failed. Restart the device.");
    if (storage.recovered()) ui->toast("NVS recovered; recalibrate touch and pair again",true);
    ui->run();
}
}

extern "C" void app_main() {
    ESP_ERROR_CHECK(display.initialize());
    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return esp_timer_get_time() / 1000; });
    nexus::DeviceSettings settings;
    esp_err_t nvs_error = storage.initialize();
    if (nvs_error == ESP_OK) storage.load_settings(settings);
    ESP_ERROR_CHECK(display.attach_lvgl(settings.orientation));
    if (nvs_error != ESP_OK) boot_error("Persistent storage failed. See NVS recovery in the troubleshooting guide.");
    ESP_ERROR_CHECK(touch.initialize());
    touch.load_calibration();
    ESP_ERROR_CHECK(touch.start());
    auto profiles = std::unique_ptr<nexus::ProfileSet>(new(std::nothrow) nexus::ProfileSet);
    if (!profiles) boot_error("Profile allocation failed. Restart the device.");
    if (storage.load_profiles(*profiles) != ESP_OK) {
        *profiles = nexus::ProfileSet{};
        const size_t size = default_profiles_end - default_profiles_start - 1;
        cJSON* root = nexus::parse_json(default_profiles_start,size);
        char error[96];
        bool okay = root && nexus::parse_profiles(root,*profiles,error,sizeof(error));
        cJSON_Delete(root);
        if (!okay) boot_error("Bundled profiles are invalid. Rebuild firmware with the shipped profiles.");
    }
    for (uint8_t i = 0; i < profiles->count; ++i)
        if (std::strcmp(settings.active_profile,profiles->profiles[i].id) == 0) profiles->active = i;
    nexus::ui_events = xQueueCreate(16,sizeof(nexus::UiEvent));
    nexus::telemetry_mailbox = xQueueCreate(1,sizeof(nexus::Telemetry));
    if (!nexus::ui_events || !nexus::telemetry_mailbox) boot_error("Event queue allocation failed.");
    if (bluetooth.initialize() != ESP_OK) boot_error("Bluetooth initialization failed. Restart or reflash the device.");
    ESP_ERROR_CHECK(display.reclaim_bus());
    auto* ui = new(std::nothrow) nexus::UiManager(display,touch,bluetooth,storage,std::move(profiles),settings);
    if (!ui || xTaskCreatePinnedToCore(ui_task,"nexus_ui",12288,ui,4,nullptr,1) != pdPASS)
        boot_error("UI task allocation failed.");
}
