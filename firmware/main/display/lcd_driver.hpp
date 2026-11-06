#pragma once
#include <cstdint>
#include "esp_err.h"
#include "lvgl.h"

namespace nexus {
enum class Orientation : uint8_t { Portrait = 0, Landscape = 1 };

class LcdDriver {
public:
    esp_err_t initialize();
    esp_err_t reclaim_bus();
    esp_err_t attach_lvgl(Orientation orientation);
    void set_orientation(Orientation orientation);
    void set_backlight(uint8_t brightness);
    bool has_backlight() const;
    int width() const { return orientation_ == Orientation::Portrait ? 320 : 480; }
    int height() const { return orientation_ == Orientation::Portrait ? 480 : 320; }
    Orientation orientation() const { return orientation_; }
    lv_display_t* display() const { return display_; }
    void native_to_logical(int nx, int ny, int& x, int& y) const;
    void logical_to_native(int x, int y, int& nx, int& ny) const;
    void write_area(const lv_area_t& area, const uint16_t* pixels, uint32_t stride_bytes);
    void fill_native(int x, int y, int width, int height, uint16_t rgb565);
    ~LcdDriver();
private:
    static void flush(lv_display_t* display, const lv_area_t* area, uint8_t* pixels);
    void window(int x0, int y0, int x1, int y1);
    Orientation orientation_ = Orientation::Portrait;
    lv_display_t* display_ = nullptr;
    void* buffer_ = nullptr;
};
}
