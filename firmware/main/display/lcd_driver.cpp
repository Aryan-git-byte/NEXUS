#pragma GCC optimize ("O3")
#include "lcd_driver.hpp"
#include "config.hpp"
#include <algorithm>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/gpio_reg.h"
#include "soc/soc.h"
#include "soc/usb_serial_jtag_reg.h"

namespace nexus {
namespace {
uint32_t low_byte[256], high_byte[256], data_mask;
constexpr uint32_t wr_bit = 1u << config::kWr, dc_bit = 1u << config::kDc;
inline __attribute__((always_inline)) void write16(uint16_t value) {
    REG_WRITE(GPIO_OUT_W1TC_REG, data_mask);
    REG_WRITE(GPIO_OUT_W1TS_REG, low_byte[value & 255] | high_byte[value >> 8]);
    REG_WRITE(GPIO_OUT_W1TC_REG, wr_bit);
    REG_WRITE(GPIO_OUT_W1TS_REG, wr_bit);
}
void command(uint8_t value) {
    REG_WRITE(GPIO_OUT_W1TC_REG, dc_bit);
    write16(value);
    REG_WRITE(GPIO_OUT_W1TS_REG, dc_bit);
}
void command_data(uint8_t value, const uint8_t* data, size_t count) {
    command(value);
    for (size_t i = 0; i < count; ++i) write16(data[i]);
}
esp_err_t output(int pin) {
    const auto gpio = static_cast<gpio_num_t>(pin);
    esp_err_t result = gpio_reset_pin(gpio);
    if (result == ESP_OK) result = gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
    if (result == ESP_OK) result = gpio_set_drive_capability(gpio, GPIO_DRIVE_CAP_3);
    return result;
}
}

esp_err_t LcdDriver::initialize() {
    // GPIO19/20 belong to the supplied LCD bus, not the USB serial/JTAG peripheral.
    CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
    data_mask = 0;
    for (auto pin : config::kData) {
        esp_err_t error = output(pin);
        if (error != ESP_OK) return error;
        data_mask |= 1u << pin;
    }
    for (unsigned value = 0; value < 256; ++value) {
        low_byte[value] = high_byte[value] = 0;
        for (unsigned bit = 0; bit < 8; ++bit) if (value & (1u << bit)) {
            low_byte[value] |= 1u << config::kData[bit];
            high_byte[value] |= 1u << config::kData[bit + 8];
        }
    }
    esp_err_t result = output(config::kDc);
    if (result != ESP_OK) return result;
    result = output(config::kWr);
    if (result != ESP_OK) return result;
    REG_WRITE(GPIO_OUT_W1TS_REG, wr_bit | dc_bit);
    if (config::kReset >= 0) {
        result = output(config::kReset);
        if (result != ESP_OK) return result;
        gpio_set_level(static_cast<gpio_num_t>(config::kReset), 1); vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(static_cast<gpio_num_t>(config::kReset), 0); vTaskDelay(pdMS_TO_TICKS(20));
        gpio_set_level(static_cast<gpio_num_t>(config::kReset), 1); vTaskDelay(pdMS_TO_TICKS(120));
    }
    // Exact initialization sequence from the working reference. RGB565 and BGR MADCTL stay intact.
    command(0x01); vTaskDelay(pdMS_TO_TICKS(150));
    command(0x11); vTaskDelay(pdMS_TO_TICKS(150));
    const uint8_t pg[] = {0x00,0x03,0x09,0x08,0x16,0x0A,0x3F,0x78,0x4C,0x09,0x0A,0x08,0x16,0x1A,0x0F};
    const uint8_t ng[] = {0x00,0x16,0x19,0x03,0x0F,0x05,0x32,0x45,0x46,0x04,0x0E,0x0D,0x35,0x37,0x0F};
    command_data(0xE0, pg, 15); command_data(0xE1, ng, 15);
    { const uint8_t d[] = {0x17, 0x15}; command_data(0xC0, d, 2); }
    { const uint8_t d[] = {0x41}; command_data(0xC1, d, 1); }
    { const uint8_t d[] = {0x00, 0x12, 0x80}; command_data(0xC5, d, 3); }
    { const uint8_t d[] = {0x48}; command_data(0x36, d, 1); }
    { const uint8_t d[] = {0x55}; command_data(0x3A, d, 1); }
    { const uint8_t d[] = {0x00}; command_data(0xB0, d, 1); }
    { const uint8_t d[] = {0xA0}; command_data(0xB1, d, 1); }
    { const uint8_t d[] = {0x02}; command_data(0xB4, d, 1); }
    { const uint8_t d[] = {0x02, 0x02}; command_data(0xB6, d, 2); }
    { const uint8_t d[] = {0x00}; command_data(0xE9, d, 1); }
    { const uint8_t d[] = {0xA9, 0x51, 0x2C, 0x82}; command_data(0xF7, d, 4); }
    if (config::kInvertColors) command(0x21);
    command(0x29); vTaskDelay(pdMS_TO_TICKS(50));
    fill_native(0, 0, 320, 480, 0x18c3);
    if (config::kBacklight >= 0) {
        ledc_timer_config_t timer{};
        timer.speed_mode = LEDC_LOW_SPEED_MODE; timer.duty_resolution = LEDC_TIMER_10_BIT;
        timer.timer_num = LEDC_TIMER_0; timer.freq_hz = 1000; timer.clk_cfg = LEDC_AUTO_CLK;
        result = ledc_timer_config(&timer);
        if (result != ESP_OK) return result;
        ledc_channel_config_t channel{};
        channel.gpio_num = config::kBacklight; channel.speed_mode = LEDC_LOW_SPEED_MODE;
        channel.channel = LEDC_CHANNEL_0; channel.timer_sel = LEDC_TIMER_0; channel.duty = 1023;
        result = ledc_channel_config(&channel);
    }
    return result;
}

esp_err_t LcdDriver::reclaim_bus() {
    // Radio/PHY initialization runs after LCD startup. Reassert the supplied
    // GPIO ownership before LVGL draws; no LCD register or bus mapping changes.
    CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG,USB_SERIAL_JTAG_USB_PAD_ENABLE);
    for (auto pin : config::kData) {
        const esp_err_t result = output(pin);
        if (result != ESP_OK) return result;
    }
    esp_err_t result = output(config::kDc);
    if (result == ESP_OK) result = output(config::kWr);
    REG_WRITE(GPIO_OUT_W1TS_REG,wr_bit | dc_bit);
    return result;
}
void LcdDriver::window(int x0, int y0, int x1, int y1) {
    command(0x2A); write16(x0 >> 8); write16(x0 & 255); write16(x1 >> 8); write16(x1 & 255);
    command(0x2B); write16(y0 >> 8); write16(y0 & 255); write16(y1 >> 8); write16(y1 & 255);
    command(0x2C);
}

void LcdDriver::fill_native(int x, int y, int w, int h, uint16_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    w = std::min(w, 320 - x); h = std::min(h, 480 - y);
    if (w <= 0 || h <= 0) return;
    window(x, y, x + w - 1, y + h - 1);
    const uint32_t bits = low_byte[color & 255] | high_byte[color >> 8];
    REG_WRITE(GPIO_OUT_W1TC_REG, data_mask); REG_WRITE(GPIO_OUT_W1TS_REG, bits);
    for (int count = w * h; count > 0; --count) {
        REG_WRITE(GPIO_OUT_W1TC_REG, wr_bit); REG_WRITE(GPIO_OUT_W1TS_REG, wr_bit);
    }
}

void LcdDriver::write_area(const lv_area_t& area, const uint16_t* pixels, uint32_t stride_bytes) {
    if (!pixels || area.x1 < 0 || area.y1 < 0 || area.x2 >= width() || area.y2 >= height()) return;
    const int w = lv_area_get_width(&area), h = lv_area_get_height(&area);
    if (w <= 0 || h <= 0 || stride_bytes < static_cast<uint32_t>(w * sizeof(uint16_t)) || stride_bytes % 2) return;
    const uint32_t stride = stride_bytes / sizeof(uint16_t);
    if (orientation_ == Orientation::Portrait) {
        window(area.x1, area.y1, area.x2, area.y2);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) write16(pixels[y * stride + x]);
    } else {
        // Logical (x,y) -> native (319-y,x). Rotate during transfer, with no extra framebuffer.
        window(319 - area.y2, area.x1, 319 - area.y1, area.x2);
        for (int x = 0; x < w; ++x)
            for (int y = h - 1; y >= 0; --y) write16(pixels[y * stride + x]);
    }
}

esp_err_t LcdDriver::attach_lvgl(Orientation orientation) {
    orientation_ = orientation;
    constexpr size_t bytes = 480 * config::kDrawLines * sizeof(uint16_t);
    buffer_ = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buffer_) return ESP_ERR_NO_MEM;
    display_ = lv_display_create(width(), height());
    if (!display_) { heap_caps_free(buffer_); buffer_ = nullptr; return ESP_ERR_NO_MEM; }
    lv_display_set_color_format(display_, LV_COLOR_FORMAT_RGB565);
    lv_display_set_user_data(display_, this);
    lv_display_set_buffers(display_, buffer_, nullptr, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display_, flush);
    return ESP_OK;
}

void LcdDriver::flush(lv_display_t* display, const lv_area_t* area, uint8_t* pixels) {
    auto* driver = static_cast<LcdDriver*>(lv_display_get_user_data(display));
    const uint32_t stride = lv_draw_buf_width_to_stride(lv_area_get_width(area),LV_COLOR_FORMAT_RGB565);
    driver->write_area(*area, reinterpret_cast<const uint16_t*>(pixels),stride);
    lv_display_flush_ready(display);
}
void LcdDriver::set_orientation(Orientation orientation) {
    orientation_ = orientation;
    if (display_) lv_display_set_resolution(display_, width(), height());
}
void LcdDriver::native_to_logical(int nx, int ny, int& x, int& y) const {
    if (orientation_ == Orientation::Portrait) { x = nx; y = ny; }
    else { x = ny; y = 319 - nx; }
}
void LcdDriver::logical_to_native(int x, int y, int& nx, int& ny) const {
    if (orientation_ == Orientation::Portrait) { nx = x; ny = y; }
    else { nx = 319 - y; ny = x; }
}
bool LcdDriver::has_backlight() const { return config::kBacklight >= 0; }
void LcdDriver::set_backlight(uint8_t value) {
    if (!has_backlight()) return;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, std::min<unsigned>(value, 100) * 1023 / 100);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}
LcdDriver::~LcdDriver() {
    if (display_) lv_display_delete(display_);
    heap_caps_free(buffer_);
}
}
