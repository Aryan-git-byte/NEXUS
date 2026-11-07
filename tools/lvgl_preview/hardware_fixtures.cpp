// Hardware test doubles used only by the headless LVGL renderer. The firmware
// build uses the real GPIO, touch, NVS, FreeRTOS and NimBLE implementations.
#include "ui/ui_manager.hpp"
#include "diagnostics/boot_info.hpp"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

std::vector<uint16_t> preview_pixels;
std::filesystem::path preview_icon_directory;
static const auto clock_origin = std::chrono::steady_clock::now();
uint64_t preview_time_offset_ms = 0;
std::vector<nexus::apps::TimerEvent> preview_timer_events;
int64_t esp_timer_get_time() {
    return 1000 + preview_time_offset_ms * 1000 + std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - clock_origin).count();
}
int xQueueSend(QueueHandle_t,const void*,unsigned) { return pdFALSE; }
int xQueueReceive(QueueHandle_t,void*,unsigned) { return pdFALSE; }
void vTaskDelay(unsigned milliseconds) { std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds)); }

namespace nexus {
const BootInfo& boot_info() {
    static const BootInfo fixture{0,"unknown"}; return fixture;
}
OtaManager::~OtaManager() {}
bool OtaManager::supported() { return true; }
esp_err_t LcdDriver::attach_lvgl(Orientation orientation) {
    orientation_ = orientation;
    buffer_ = std::calloc(480 * config::kDrawLines,2);
    if (!buffer_) return ESP_ERR_NO_MEM;
    display_ = lv_display_create(width(),height());
    if (!display_) return ESP_ERR_NO_MEM;
    lv_display_set_color_format(display_,LV_COLOR_FORMAT_RGB565);
    lv_display_set_user_data(display_,this);
    lv_display_set_buffers(display_,buffer_,nullptr,480 * config::kDrawLines * 2,LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display_,flush);
    preview_pixels.assign(width() * height(),0);
    return ESP_OK;
}
void LcdDriver::flush(lv_display_t* display,const lv_area_t* area,uint8_t* pixels) {
    auto* lcd = static_cast<LcdDriver*>(lv_display_get_user_data(display));
    const auto stride = lv_draw_buf_width_to_stride(lv_area_get_width(area),LV_COLOR_FORMAT_RGB565);
    for (int y = area->y1; y <= area->y2; ++y) {
        const auto* row = reinterpret_cast<uint16_t*>(pixels + (y - area->y1) * stride);
        for (int x = area->x1; x <= area->x2; ++x) preview_pixels[y * lcd->width() + x] = row[x - area->x1];
    }
    lv_display_flush_ready(display);
}
void LcdDriver::set_orientation(Orientation orientation) {
    orientation_ = orientation;
    preview_pixels.assign(width() * height(),0);
    lv_display_set_resolution(display_,width(),height());
}
void LcdDriver::native_to_logical(int nx,int ny,int& x,int& y) const {
    if (orientation_ == Orientation::Portrait) { x = nx; y = ny; }
    else { x = ny; y = 319 - nx; }
}
bool LcdDriver::has_backlight() const { return false; }
void LcdDriver::set_backlight(uint8_t) {}
LcdDriver::~LcdDriver() { lv_display_delete(display_); std::free(buffer_); }
TouchSample TouchDriver::sample() { return {}; }
esp_err_t TouchDriver::calibrate(const int[3],const int[3],const int[3],const int[3]) {
    calibrated_ = true; return ESP_OK;
}
esp_err_t BleManager::initialize() {
    connection_ = 1; ready_ = true; encrypted_ = true;
    host_synced_ = true; has_identity_ = true; return ESP_OK;
}
uint32_t BleManager::action(const char*,const char*,bool) { return ready() ? ++sequence_ : 0; }
uint32_t BleManager::volume(int) { return ready() ? ++sequence_ : 0; }
uint32_t BleManager::timer_alert(apps::TimerEvent event) {
    if (!ready()) return 0;
    preview_timer_events.push_back(event); return ++sequence_;
}
bool BleManager::save_settings(const DeviceSettings&) { return true; }
bool BleManager::save_apps(const apps::Preferences&) { return true; }
esp_err_t Storage::load_apps(apps::Preferences&) { return ESP_ERR_NOT_FOUND; }
bool BleManager::select_profile(const char*) { return ready(); }
bool BleManager::open_pairing() { return !connected(); }
bool BleManager::forget_pc() { has_identity_ = false; return true; }
bool BleManager::restart(bool) { ready_ = false; connection_ = 0xffff; advertising_ = true; return true; }
uint32_t BleManager::pairing_remaining() const { return 0; }
esp_err_t Storage::load_icon(const char* id,uint8_t*& output,size_t& size) {
    output = nullptr; size = 0;
    std::ifstream input(preview_icon_directory / (std::string(id) + ".ndi"),std::ios::binary);
    if (!input) return ESP_ERR_NOT_FOUND;
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
    size = bytes.size(); output = static_cast<uint8_t*>(std::malloc(size));
    if (!output) return ESP_ERR_NO_MEM;
    std::memcpy(output,bytes.data(),size); return ESP_OK;
}
}
