#include "touch_driver.hpp"
#include "config.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "nvs.h"

namespace nexus {
esp_err_t TouchDriver::initialize() {
    for (int pin : {config::kTouchDin, config::kTouchClock, config::kTouchCs, config::kTouchDout}) {
        esp_err_t result = gpio_reset_pin(static_cast<gpio_num_t>(pin));
        if (result != ESP_OK) return result;
        result = gpio_set_direction(static_cast<gpio_num_t>(pin),
                                   pin == config::kTouchDout ? GPIO_MODE_INPUT : GPIO_MODE_OUTPUT);
        if (result != ESP_OK) return result;
    }
    gpio_set_level(static_cast<gpio_num_t>(config::kTouchCs), 1);
    gpio_set_level(static_cast<gpio_num_t>(config::kTouchClock), 0);
    return ESP_OK;
}

uint16_t TouchDriver::conversion(uint8_t command) {
    uint32_t result = 0;
    gpio_set_level(static_cast<gpio_num_t>(config::kTouchCs), 0);
    esp_rom_delay_us(1);
    for (int bit = 7; bit >= 0; --bit) {
        gpio_set_level(static_cast<gpio_num_t>(config::kTouchDin), (command >> bit) & 1);
        gpio_set_level(static_cast<gpio_num_t>(config::kTouchClock), 1); esp_rom_delay_us(1);
        gpio_set_level(static_cast<gpio_num_t>(config::kTouchClock), 0); esp_rom_delay_us(1);
    }
    gpio_set_level(static_cast<gpio_num_t>(config::kTouchDin), 0);
    for (int bit = 0; bit < 16; ++bit) {
        gpio_set_level(static_cast<gpio_num_t>(config::kTouchClock), 1); esp_rom_delay_us(1);
        result = (result << 1) | gpio_get_level(static_cast<gpio_num_t>(config::kTouchDout));
        gpio_set_level(static_cast<gpio_num_t>(config::kTouchClock), 0); esp_rom_delay_us(1);
    }
    gpio_set_level(static_cast<gpio_num_t>(config::kTouchCs), 1);
    return (result >> 3) & 0xfff;
}

bool TouchDriver::read_raw(int& x, int& y) {
    const int z1 = conversion(0xB1), z2 = conversion(0xC1);
    if (z1 + 4095 - z2 < config::kPressureThreshold) return false;
    uint16_t xs[5], ys[5];
    for (int i = 0; i < 5; ++i) { xs[i] = conversion(0xD1); ys[i] = conversion(0x91); }
    // The reference's five-sample median (insertion sort has bounded execution time).
    for (auto* array : {xs, ys}) for (int i = 1; i < 5; ++i) {
        uint16_t value = array[i]; int j = i - 1;
        while (j >= 0 && array[j] > value) { array[j + 1] = array[j]; --j; }
        array[j + 1] = value;
    }
    x = xs[2]; y = ys[2];
    return true;
}

bool TouchDriver::coefficients_valid(const float v[6]) const {
    for (int i = 0; i < 6; ++i) if (!std::isfinite(v[i])) return false;
    float sx = std::fabs(v[0]) + std::fabs(v[1]), sy = std::fabs(v[3]) + std::fabs(v[4]);
    return sx > 0.03f && sx < 0.2f && sy > 0.02f && sy < 0.2f &&
           std::fabs(v[2]) < 2000 && std::fabs(v[5]) < 2000 &&
           std::fabs(v[0] * v[4] - v[1] * v[3]) > 0.0001f;
}

esp_err_t TouchDriver::load_calibration() {
    nvs_handle_t handle;
    esp_err_t result = nvs_open("touch", NVS_READONLY, &handle);
    if (result != ESP_OK) return result;
    float values[6]{}; size_t length = sizeof(values);
    result = nvs_get_blob(handle, "cal", values, &length);
    nvs_close(handle);
    if (result != ESP_OK) return result;
    if (length != sizeof(values) || !coefficients_valid(values)) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&lock_);
    std::memcpy(coefficients_, values, sizeof(values)); calibrated_ = true;
    portEXIT_CRITICAL(&lock_);
    return ESP_OK;
}

esp_err_t TouchDriver::calibrate(const int tx[3], const int ty[3], const int sx[3], const int sy[3]) {
    const float det = float(tx[0] - tx[2]) * (ty[1] - ty[2]) - float(tx[1] - tx[2]) * (ty[0] - ty[2]);
    if (std::fabs(det) < 10000) return ESP_ERR_INVALID_ARG;
    float values[6];
    values[0] = ((sx[0] - sx[2]) * float(ty[1] - ty[2]) - (sx[1] - sx[2]) * float(ty[0] - ty[2])) / det;
    values[1] = ((tx[0] - tx[2]) * float(sx[1] - sx[2]) - (tx[1] - tx[2]) * float(sx[0] - sx[2])) / det;
    values[2] = sx[0] - values[0] * tx[0] - values[1] * ty[0];
    values[3] = ((sy[0] - sy[2]) * float(ty[1] - ty[2]) - (sy[1] - sy[2]) * float(ty[0] - ty[2])) / det;
    values[4] = ((tx[0] - tx[2]) * float(sy[1] - sy[2]) - (tx[1] - tx[2]) * float(sy[0] - sy[2])) / det;
    values[5] = sy[0] - values[3] * tx[0] - values[4] * ty[0];
    if (!coefficients_valid(values)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t result = nvs_open("touch", NVS_READWRITE, &handle);
    if (result != ESP_OK) return result;
    result = nvs_set_blob(handle, "cal", values, sizeof(values));
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    if (result == ESP_OK) {
        portENTER_CRITICAL(&lock_);
        std::memcpy(coefficients_, values, sizeof(values)); calibrated_ = true;
        portEXIT_CRITICAL(&lock_);
    }
    return result;
}

void TouchDriver::poll() {
    int misses = 0;
    TickType_t wake = xTaskGetTickCount();
    while (true) {
        TouchSample next = sample();
        int x, y;
        if (read_raw(x, y)) {
            misses = 0; next.pressed = true; next.raw_x = x; next.raw_y = y;
            float c[6];
            portENTER_CRITICAL(&lock_); std::memcpy(c, coefficients_, sizeof(c)); portEXIT_CRITICAL(&lock_);
            next.native_x = std::clamp(int(c[0] * x + c[1] * y + c[2]), 0, 319);
            next.native_y = std::clamp(int(c[3] * x + c[4] * y + c[5]), 0, 479);
        } else if (++misses >= 2) next.pressed = false;
        ++next.counter;
        portENTER_CRITICAL(&lock_); latest_ = next; portEXIT_CRITICAL(&lock_);
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(8));
    }
}
TouchSample TouchDriver::sample() {
    portENTER_CRITICAL(&lock_); TouchSample value = latest_; portEXIT_CRITICAL(&lock_); return value;
}
void TouchDriver::task(void* context) { static_cast<TouchDriver*>(context)->poll(); }
esp_err_t TouchDriver::start() {
    return xTaskCreatePinnedToCore(task, "nexus_touch", 3072, this, 4, &task_, 0) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
}
