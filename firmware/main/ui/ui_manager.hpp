#pragma once
#include <memory>
#include "app_model.hpp"
#include "bluetooth/ble_manager.hpp"
#include "display/touch_driver.hpp"
#include "apps/local_apps.hpp"
#include "ui_apps.hpp"

namespace nexus {
enum class Page : uint8_t { Home, Development, Media, Monitor, Custom, Settings, Screensaver, Calibration,
                            Apps, Calculator, Timer, Game2048, Snake, Pet };
class UiManager {
public:
    UiManager(LcdDriver& lcd, TouchDriver& touch, BleManager& ble, Storage& storage,
              std::unique_ptr<ProfileSet> profiles, const DeviceSettings& settings);
    esp_err_t initialize();
    void run();
    void schedule(Page page);
    void toast(const char* text, bool error = false);
    void save_preferences();
    void confirm(const char* title, const char* text, uint8_t operation);
private:
    friend class UiPreview;
    struct ButtonView {
        UiManager* manager = nullptr;
        lv_obj_t* object = nullptr;
        uint8_t profile = 0, button = 0;
        bool long_sent = false;
        uint32_t sequence = 0;
        uint64_t feedback_until = 0;
        lv_image_dsc_t image{};
        uint8_t image_alpha[576]{};
    };
    static void touch_read(lv_indev_t* device, lv_indev_data_t* data);
    static void button_event(lv_event_t* event);
    static void nav_event(lv_event_t* event);
    static void settings_event(lv_event_t* event);
    static void volume_event(lv_event_t* event);
    static void page_event(lv_event_t* event);
    static void confirm_event(lv_event_t* event);
    static void wake_event(lv_event_t* event);
    static void apps_event(lv_event_t* event);
    static void pet_event(lv_event_t* event);
    void build(Page page, bool animate = true);
    void build_shell();
    void build_home();
    void build_grid(uint8_t profile, int top, int height, int columns, int rows, bool footer = true);
    void build_media();
    void build_monitor();
    void build_settings();
    void build_screensaver();
    void build_calibration();
    void build_apps();
    void build_calculator();
    void build_timer();
    void build_2048();
    void build_snake();
    void build_pet();
    lv_obj_t* app_button(lv_obj_t* parent, const char* text, AppControl control,
                         int x, int y, int width, int height);
    void clear_app_views();
    void update_apps(uint64_t now);
    void update_calculator();
    void update_timer(uint64_t now);
    void update_2048();
    void update_snake();
    void update_pet();
    void position_pet();
    void draw_pet(uint64_t now);
    void open_duration();
    void close_duration();
    void update_duration();
    void local_changed();
    void calibration_tick(const TouchSample& sample);
    void calibration_target();
    void update_values();
    void update_clock();
    void position_toast();
    void update_toast(uint64_t now);
    void event(const UiEvent& event);
    void press(ButtonView& view, bool long_press);
    uint8_t profile_for(Category category) const;
    void brightness();
    LcdDriver& lcd_; TouchDriver& touch_; BleManager& ble_;
    Storage& storage_;
    std::unique_ptr<ProfileSet> profiles_;
    DeviceSettings settings_;
    Telemetry telemetry_{};
    lv_indev_t* input_ = nullptr;
    lv_obj_t *root_ = nullptr, *content_ = nullptr, *status_label_ = nullptr, *status_dot_ = nullptr;
    lv_obj_t *clock_ = nullptr, *date_ = nullptr, *pc_name_ = nullptr, *home_cpu_ = nullptr, *home_ram_ = nullptr, *home_battery_ = nullptr;
    lv_obj_t* day_ = nullptr;
    lv_obj_t *media_title_ = nullptr, *media_artist_ = nullptr, *volume_slider_ = nullptr, *volume_label_ = nullptr;
    lv_obj_t *settings_connection_ = nullptr, *toast_ = nullptr, *toast_label_ = nullptr, *dim_ = nullptr;
    lv_obj_t *dialog_ = nullptr, *pairing_ = nullptr, *pairing_label_ = nullptr;
    lv_obj_t *cal_target_ = nullptr, *cal_instruction_ = nullptr;
    lv_obj_t *metric_labels_[10]{}, *metric_bars_[6]{}, *chart_ = nullptr;
    lv_chart_series_t *cpu_series_ = nullptr, *ram_series_ = nullptr;
    ButtonView buttons_[12]{};
    uint8_t button_count_ = 0, grid_page_ = 0, grid_pages_ = 1, confirmation_ = 0;
    Page page_ = Page::Home, pending_page_ = Page::Home, previous_page_ = Page::Home;
    bool rebuild_ = false, orientation_change_ = false, theme_change_ = false;
    bool link_ = false, secure_ = false, ready_ = false;
    uint64_t last_interaction_ = 0, transition_until_ = 0, toast_until_ = 0, next_clock_ = 0;
    uint32_t cal_counter_ = 0;
    uint8_t cal_index_ = 0, cal_phase_ = 0, cal_count_ = 0;
    int raw_x_[3]{}, raw_y_[3]{}, native_x_[3]{56,264,160}, native_y_[3]{72,240,408};
    int cal_sum_x_ = 0, cal_sum_y_ = 0, cal_min_x_ = 4095, cal_max_x_ = 0, cal_min_y_ = 4095, cal_max_y_ = 0;
    uint64_t cal_settle_ = 0;
    apps::Preferences local_{};
    apps::Random random_{1};
    apps::Calculator calculator_;
    apps::Timer timer_;
    apps::Game2048 game2048_;
    apps::Snake snake_;
    apps::Pet pet_;
    lv_obj_t *calc_value_ = nullptr, *calc_expression_ = nullptr;
    lv_obj_t *game_score_ = nullptr, *game_note_ = nullptr;
    lv_obj_t *game_cells_[16]{}, *game_labels_[16]{};
    lv_obj_t *snake_image_ = nullptr, *snake_start_ = nullptr, *snake_speed_ = nullptr;
    lv_obj_t *timer_value_ = nullptr, *timer_status_ = nullptr, *timer_detail_ = nullptr;
    lv_obj_t *timer_start_ = nullptr, *timer_sound_ = nullptr, *timer_progress_ = nullptr;
    lv_obj_t *duration_dialog_ = nullptr, *duration_labels_[3]{};
    uint16_t duration_values_[3]{};
    lv_obj_t *pet_overlay_ = nullptr, *pet_small_image_ = nullptr, *pet_large_image_ = nullptr;
    lv_obj_t *pet_mood_ = nullptr, *pet_pats_ = nullptr, *pet_rest_ = nullptr, *pet_visible_ = nullptr;
    lv_obj_t *pet_stat_labels_[3]{}, *pet_stat_bars_[3]{};
    lv_image_dsc_t snake_descriptor_{}, pet_descriptor_{};
    std::array<uint16_t,80 * 64> snake_pixels_{};
    std::array<uint32_t,24 * 24> pet_pixels_{};
    uint64_t next_snake_ = 0, next_app_refresh_ = 0, next_pet_frame_ = 0, next_local_save_ = 0;
    uint64_t local_save_sent_ = 0;
    uint32_t timer_alert_sequence_ = 0;
    bool snake_running_ = false, local_dirty_ = false, local_save_pending_ = false, pet_long_sent_ = false;
    int pet_frame_ = -1;
};
}
