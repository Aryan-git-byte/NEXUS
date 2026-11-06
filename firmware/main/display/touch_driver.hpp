#pragma once
#include <cstdint>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace nexus {
struct TouchSample {
    int raw_x = 0, raw_y = 0, native_x = 0, native_y = 0;
    bool pressed = false;
    uint32_t counter = 0;
};

class TouchDriver {
public:
    esp_err_t initialize();
    esp_err_t start();
    TouchSample sample();
    bool calibrated() const { return calibrated_; }
    esp_err_t calibrate(const int raw_x[3], const int raw_y[3],
                        const int native_x[3], const int native_y[3]);
    esp_err_t load_calibration();
private:
    static void task(void* context);
    uint16_t conversion(uint8_t command);
    bool read_raw(int& x, int& y);
    bool coefficients_valid(const float values[6]) const;
    void poll();
    portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
    TouchSample latest_{};
    float coefficients_[6]{};
    bool calibrated_ = false;
    TaskHandle_t task_ = nullptr;
};
}
