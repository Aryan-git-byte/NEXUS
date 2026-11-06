#include "ui_manager.hpp"
#include "ui_styles.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cctype>
#include "freertos/task.h"
#include "sys/time.h"

namespace nexus {
UiManager::UiManager(LcdDriver& lcd, TouchDriver& touch, BleManager& ble, Storage& storage,
                     std::unique_ptr<ProfileSet> profiles, const DeviceSettings& settings)
    : lcd_(lcd),touch_(touch),ble_(ble),storage_(storage),profiles_(std::move(profiles)),settings_(settings) {}

esp_err_t UiManager::initialize() {
    style::initialize(settings_.theme);
    lv_display_set_theme(lcd_.display(),lv_theme_default_init(lcd_.display(),style::accent,
                         style::muted,false,&lv_font_montserrat_14));
    input_ = lv_indev_create();
    if (!input_) return ESP_ERR_NO_MEM;
    lv_indev_set_type(input_,LV_INDEV_TYPE_POINTER);
    lv_indev_set_user_data(input_,this); lv_indev_set_read_cb(input_,touch_read);
    lv_indev_set_long_press_time(input_,600);
    lv_indev_set_scroll_limit(input_,12);
    last_interaction_ = now_ms();
    dim_ = lv_obj_create(lv_layer_top()); lv_obj_remove_style_all(dim_);
    lv_obj_set_style_bg_color(dim_,lv_color_black(),0);
    lv_obj_remove_flag(dim_,LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_flag(dim_,LV_OBJ_FLAG_SCROLLABLE);
    toast_ = style::card(lv_layer_top()); lv_obj_add_flag(toast_,LV_OBJ_FLAG_HIDDEN);
    toast_label_ = style::label(toast_,""); lv_label_set_long_mode(toast_label_,LV_LABEL_LONG_WRAP);
    build(touch_.calibrated() ? Page::Home : Page::Calibration,false);
    return ESP_OK;
}

void UiManager::touch_read(lv_indev_t* device, lv_indev_data_t* data) {
    auto* self = static_cast<UiManager*>(lv_indev_get_user_data(device));
    const TouchSample sample = self->touch_.sample();
    bool pressed = sample.pressed && self->touch_.calibrated() && self->page_ != Page::Calibration && now_ms() >= self->transition_until_;
    if (pressed) self->last_interaction_ = now_ms();
    int x, y; self->lcd_.native_to_logical(sample.native_x,sample.native_y,x,y);
    data->point.x = std::clamp(x,0,self->lcd_.width() - 1); data->point.y = std::clamp(y,0,self->lcd_.height() - 1);
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void UiManager::build_shell() {
    auto* header = lv_obj_create(root_); lv_obj_remove_style_all(header);
    lv_obj_set_size(header,lcd_.width(),44); lv_obj_set_pos(header,0,0);
    lv_obj_remove_flag(header,LV_OBJ_FLAG_SCROLLABLE);
    auto* name = style::label(header,"NEXUS",&lv_font_montserrat_16); lv_obj_set_pos(name,12,7);
    status_dot_ = lv_obj_create(header); lv_obj_remove_style_all(status_dot_);
    lv_obj_set_size(status_dot_,5,5); lv_obj_set_style_radius(status_dot_,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_bg_opa(status_dot_,LV_OPA_COVER,0); lv_obj_set_pos(status_dot_,13,30);
    status_label_ = style::label(header,"Offline",&lv_font_montserrat_14,true); lv_obj_set_pos(status_label_,24,23);
    pc_name_ = style::label(header,"",&lv_font_montserrat_14,true);
    lv_obj_set_height(pc_name_,lv_font_montserrat_14.line_height);
    lv_obj_set_width(pc_name_,lcd_.width() - 230); lv_label_set_long_mode(pc_name_,LV_LABEL_LONG_DOT);
    lv_obj_set_pos(pc_name_,92,8);
    clock_ = style::label(header,"--:--",&lv_font_montserrat_20); lv_obj_align(clock_,LV_ALIGN_TOP_RIGHT,-12,0);
    date_ = style::label(header,"Sync clock",&lv_font_montserrat_14,true); lv_obj_align(date_,LV_ALIGN_BOTTOM_RIGHT,-12,0);
    content_ = lv_obj_create(root_); lv_obj_remove_style_all(content_);
    lv_obj_set_pos(content_,12,48); lv_obj_set_size(content_,lcd_.width() - 24,lcd_.height() - 98);
    lv_obj_remove_flag(content_,LV_OBJ_FLAG_SCROLLABLE);
    static const char* names[] = {"Home","Dev","Media","Stats","Macros","Setup"};
    const bool portrait = lcd_.width() < lcd_.height();
    int width = lcd_.width() / 6;
    for (uint8_t i = 0; i < 6; ++i) {
        auto* button = style::action_button(root_,portrait && i == 4 ? "More" : names[i]);
        lv_obj_set_size(button,i == 5 ? lcd_.width() - i * width : width - 2,40);
        lv_obj_set_pos(button,i * width,lcd_.height() - 42);
        lv_obj_set_style_radius(button,8,0); lv_obj_set_style_border_width(button,0,0);
        auto* label = lv_obj_get_child(button,0);
        lv_obj_set_width(label,width - 6); lv_label_set_long_mode(label,LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0); lv_obj_center(label);
        if (i == static_cast<uint8_t>(page_)) lv_obj_add_style(button,&style::selected,0);
        else lv_obj_set_style_bg_color(button,style::background,0);
        lv_obj_set_user_data(button,reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
        lv_obj_add_event_cb(button,nav_event,LV_EVENT_CLICKED,this);
    }
}

void UiManager::build(Page page, bool animate) {
    lv_obj_t* previous = lv_screen_active();
    page_ = page;
    home_cpu_ = home_ram_ = home_battery_ = media_title_ = media_artist_ = volume_slider_ = volume_label_ = nullptr;
    status_dot_ = status_label_ = pc_name_ = clock_ = date_ = day_ = settings_connection_ = chart_ = nullptr;
    cpu_series_ = ram_series_ = nullptr; button_count_ = 0;
    std::fill(std::begin(metric_labels_),std::end(metric_labels_),nullptr);
    std::fill(std::begin(metric_bars_),std::end(metric_bars_),nullptr);
    root_ = lv_obj_create(nullptr); lv_obj_remove_style_all(root_); lv_obj_add_style(root_,&style::screen,0);
    lv_obj_remove_flag(root_,LV_OBJ_FLAG_SCROLLABLE);
    if (page == Page::Calibration) build_calibration();
    else if (page == Page::Screensaver) build_screensaver();
    else {
        build_shell();
        switch (page) {
        case Page::Home: case Page::Development: case Page::Custom: build_home(); break;
        case Page::Media: build_media(); break;
        case Page::Monitor: build_monitor(); break;
        case Page::Settings: build_settings(); break;
        default: break;
        }
    }
    if (animate) { lv_screen_load_anim(root_,LV_SCR_LOAD_ANIM_FADE_IN,100,0,true); transition_until_ = now_ms() + 160; }
    else { lv_screen_load(root_); if (previous && previous != root_) lv_obj_delete(previous); }
    lv_obj_set_size(dim_,lcd_.width(),lcd_.height()); lv_obj_set_pos(dim_,0,0);
    brightness(); update_values(); update_clock();
    lv_indev_reset(input_,nullptr);
}
void UiManager::schedule(Page page) {
    if (page != page_) grid_page_ = 0;
    pending_page_ = page; rebuild_ = true; last_interaction_ = now_ms();
}
uint8_t UiManager::profile_for(Category category) const {
    if (profiles_->profiles[profiles_->active].category == category) return profiles_->active;
    for (uint8_t i = 0; i < profiles_->count; ++i) if (profiles_->profiles[i].category == category) return i;
    return profiles_->active;
}
void UiManager::brightness() {
    const bool sleeping = page_ == Page::Screensaver;
    const uint8_t value = sleeping && lcd_.has_backlight() ? std::min<uint8_t>(settings_.brightness,35) : settings_.brightness;
    lcd_.set_backlight(value);
    // Black is the sleep screen's background; preserve pure white lettering.
    lv_obj_set_style_bg_opa(dim_,lcd_.has_backlight() || sleeping ? LV_OPA_TRANSP : (100 - value) * 255 / 100,0);
    lv_obj_move_foreground(dim_);
    lv_obj_move_foreground(toast_);
    if (pairing_) lv_obj_move_foreground(pairing_);
    if (dialog_) lv_obj_move_foreground(dialog_);
}
void UiManager::toast(const char* message, bool error) {
    lv_obj_set_width(toast_,lcd_.width() - 24); lv_obj_set_height(toast_,LV_SIZE_CONTENT);
    lv_obj_set_width(toast_label_,lcd_.width() - 46);
    style::set_text(toast_label_,message);
    lv_obj_set_style_text_color(toast_label_,error ? style::warning : style::foreground,0);
    lv_obj_align(toast_,LV_ALIGN_BOTTOM_MID,0,-46);
    lv_obj_remove_flag(toast_,LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(toast_);
    toast_until_ = now_ms() + 2700;
}
void UiManager::press(ButtonView& view, bool long_press) {
    const auto& profile = profiles_->profiles[view.profile];
    const auto& button = profile.buttons[view.button];
    uint32_t seq = ble_.action(profile.id,button.id,long_press);
    if (!seq) {
        toast("Connect your PC to run this action",true);
        lv_obj_set_style_border_color(view.object,style::warning,0); view.feedback_until = now_ms() + 800;
    } else {
        view.sequence = seq; lv_obj_set_style_border_color(view.object,style::accent,0);
        view.feedback_until = now_ms() + 5500;
    }
}
void UiManager::button_event(lv_event_t* event) {
    auto* view = static_cast<ButtonView*>(lv_event_get_user_data(event));
    auto* self = view->manager;
    if (now_ms() < self->transition_until_) return;
    const auto code = lv_event_get_code(event);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_LONG_PRESSED && code != LV_EVENT_CLICKED) return;
    self->last_interaction_ = now_ms();
    if (code == LV_EVENT_PRESSED) view->long_sent = false;
    else if (code == LV_EVENT_LONG_PRESSED && self->profiles_->profiles[view->profile].buttons[view->button].has_long_press) {
        view->long_sent = true; self->press(*view,true);
    } else if (code == LV_EVENT_CLICKED && !view->long_sent) self->press(*view,false);
}
void UiManager::nav_event(lv_event_t* event) {
    auto* self = static_cast<UiManager*>(lv_event_get_user_data(event));
    auto* object = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    self->schedule(static_cast<Page>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(object))));
}
void UiManager::page_event(lv_event_t* event) {
    auto* self = static_cast<UiManager*>(lv_event_get_user_data(event));
    auto* object = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    int direction = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(object)));
    self->grid_page_ = (self->grid_page_ + self->grid_pages_ + direction) % self->grid_pages_;
    self->schedule(self->page_);
}
void UiManager::volume_event(lv_event_t* event) {
    auto* self = static_cast<UiManager*>(lv_event_get_user_data(event));
    auto* object = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    if (!self->ble_.volume(lv_slider_get_value(object))) self->toast("Connect your PC to change volume",true);
    self->last_interaction_ = now_ms();
}

void UiManager::confirm(const char* title, const char* text, uint8_t operation) {
    if (dialog_) return;
    confirmation_ = operation;
    dialog_ = lv_msgbox_create(nullptr); lv_obj_add_style(dialog_,&style::surface,0);
    lv_obj_set_width(dialog_,lcd_.width() - 40);
    lv_msgbox_add_title(dialog_,title); lv_msgbox_add_text(dialog_,text);
    auto* cancel = lv_msgbox_add_footer_button(dialog_,"Cancel");
    lv_obj_set_user_data(cancel,reinterpret_cast<void*>(0)); lv_obj_add_event_cb(cancel,confirm_event,LV_EVENT_CLICKED,this);
    auto* accept = lv_msgbox_add_footer_button(dialog_,"Confirm");
    lv_obj_set_user_data(accept,reinterpret_cast<void*>(1)); lv_obj_add_event_cb(accept,confirm_event,LV_EVENT_CLICKED,this);
    lv_obj_center(dialog_); lv_obj_move_foreground(dialog_);
}
void UiManager::confirm_event(lv_event_t* event) {
    auto* self = static_cast<UiManager*>(lv_event_get_user_data(event));
    auto* object = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    bool accepted = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(object)) == 1;
    lv_msgbox_close(self->dialog_); self->dialog_ = nullptr;
    if (accepted) {
        bool queued = self->confirmation_ == 2 ? self->ble_.forget_pc() : self->ble_.restart(self->confirmation_ == 1);
        if (!queued) self->toast("Device queue busy; try again",true);
    }
}

void UiManager::update_clock() {
    time_t epoch = time(nullptr);
    if (epoch < 1700000000) {
        style::set_text(clock_,"--:--");
        style::set_text(day_,"CLOCK NOT SYNCED");
        style::set_text(date_,page_ == Page::Screensaver ? "Connect your computer" : "Sync clock");
        return;
    }
    epoch += settings_.utc_offset; tm local{}; gmtime_r(&epoch,&local);
    char clock[16], date[40], day[24]; std::strftime(clock,sizeof(clock),"%H:%M",&local);
    std::strftime(date,sizeof(date),page_ == Page::Screensaver ? "%d %B %Y" : "%a %d %b",&local);
    std::strftime(day,sizeof(day),"%A",&local);
    for (char* letter = day; *letter; ++letter) *letter = static_cast<char>(std::toupper(static_cast<unsigned char>(*letter)));
    style::set_text(day_,day);
    style::set_text(clock_,clock); style::set_text(date_,date);
}
void UiManager::update_values() {
    const bool live = ble_.ready() && telemetry_.received_ms && now_ms() - telemetry_.received_ms <= 6500;
    char text[192];
    style::set_text(status_label_,ble_.ready() ? "Connected" : ble_.connected() ? "Pairing" :
                    ble_.radio_error() ? "BLE error" : ble_.advertising() ? "Offline" : "BLE starting");
    if (status_dot_) lv_obj_set_style_bg_color(status_dot_,ble_.ready() ? style::success : ble_.connected() ? style::warning : style::muted,0);
    style::set_text(pc_name_,page_ == Page::Screensaver ?
                    (telemetry_.pc_name[0] ? telemetry_.pc_name : "No computer connected") :
                    (ble_.ready() ? telemetry_.pc_name : ""));
    auto percent = [&](lv_obj_t* object, const char* name, float value) {
        if (!object) return;
        if (!live || value < 0) std::snprintf(text,sizeof(text),"%s --",name);
        else std::snprintf(text,sizeof(text),"%s %.0f%%",name,value);
        style::set_text(object,text);
    };
    percent(home_cpu_,"CPU",telemetry_.cpu); percent(home_ram_,"RAM",telemetry_.ram); percent(home_battery_,"BAT",telemetry_.battery);
    if (media_title_) {
        style::set_text(media_title_,live && telemetry_.media_available && telemetry_.media_title[0] ? telemetry_.media_title : "No active media session");
        style::set_text(media_artist_,!live ? "Connect your PC to control media" : telemetry_.media_available ? telemetry_.media_artist : "Open a media app on Windows");
        if (live && telemetry_.volume >= 0) {
            lv_obj_remove_state(volume_slider_,LV_STATE_DISABLED);
            if (!lv_obj_has_state(volume_slider_,LV_STATE_PRESSED)) lv_slider_set_value(volume_slider_,telemetry_.volume,LV_ANIM_OFF);
            std::snprintf(text,sizeof(text),"%.0f%%%s",telemetry_.volume,telemetry_.muted ? " M" : "");
        } else { lv_obj_add_state(volume_slider_,LV_STATE_DISABLED); std::snprintf(text,sizeof(text),"--"); }
        style::set_text(volume_label_,text);
    }
    if (metric_labels_[0]) {
        const float values[] = {telemetry_.cpu,telemetry_.ram,telemetry_.gpu,telemetry_.disk,telemetry_.battery,
                               telemetry_.cpu_temp,telemetry_.gpu_temp,telemetry_.upload,telemetry_.download};
        for (int i = 0; i < 9; ++i) {
            if (!live || values[i] < 0) std::snprintf(text,sizeof(text),"--");
            else if (i < 5) std::snprintf(text,sizeof(text),"%.0f%%",values[i]);
            else if (i < 7) std::snprintf(text,sizeof(text),"%.0f C",values[i]);
            else if (values[i] >= 1024) std::snprintf(text,sizeof(text),"%.1f M/s",values[i] / 1024);
            else std::snprintf(text,sizeof(text),"%.0f K/s",values[i]);
            style::set_text(metric_labels_[i],text);
            if (i < 5) lv_bar_set_value(metric_bars_[i],live && values[i] >= 0 ? values[i] : 0,LV_ANIM_OFF);
        }
    }
    if (settings_connection_) {
        uint32_t remaining = ble_.pairing_remaining();
        const char* state = ble_.ready() ? "Encrypted PC session" : ble_.connected() ? "Establishing secure link" :
                            ble_.advertising() ? "Advertising NEXUS DECK" : ble_.host_synced() ? "Radio retrying" : "Starting BLE host";
        if (ble_.radio_error()) std::snprintf(text,sizeof(text),"%s\nRadio error %d; retrying",state,ble_.radio_error());
        else std::snprintf(text,sizeof(text),"%s\n%s%s",state,ble_.bonded() ? "One saved PC bond" :
                          remaining ? "Pairing open" : "Pairing closed",remaining ? " (120 s window)" : "");
        style::set_text(settings_connection_,text);
    }
}

void UiManager::event(const UiEvent& incoming) {
    switch (incoming.kind) {
    case UiEventKind::Link:
        link_ = incoming.value; ready_ = secure_ = false;
        if (!link_) {
            telemetry_.received_ms = 0;
            if (pairing_) { lv_obj_delete(pairing_); pairing_ = pairing_label_ = nullptr; }
            toast("PC disconnected. Your profiles are available.");
        }
        update_values(); break;
    case UiEventKind::Secure:
        secure_ = incoming.value;
        if (pairing_) { lv_obj_delete(pairing_); pairing_ = pairing_label_ = nullptr; }
        update_values(); break;
    case UiEventKind::Ready:
        ready_ = incoming.value;
        std::snprintf(telemetry_.pc_name,sizeof(telemetry_.pc_name),"%.63s",incoming.text);
        update_values(); break;
    case UiEventKind::Passkey: {
        if (!pairing_) {
            pairing_ = style::card(lv_layer_top()); lv_obj_set_size(pairing_,lcd_.width() - 32,lcd_.width() < 400 ? 170 : 142);
            lv_obj_center(pairing_); pairing_label_ = style::label(pairing_,"",&lv_font_montserrat_20);
            lv_obj_set_width(pairing_label_,lcd_.width() - 56); lv_label_set_long_mode(pairing_label_,LV_LABEL_LONG_WRAP);
            lv_obj_set_style_text_align(pairing_label_,LV_TEXT_ALIGN_CENTER,0); lv_obj_center(pairing_label_);
        }
        char text[128]; std::snprintf(text,sizeof(text),"Pair NEXUS DECK\n\n%06ld\n\nEnter this passkey on Windows",static_cast<long>(incoming.value));
        style::set_text(pairing_label_,text); lv_obj_move_foreground(pairing_); break;
    }
    case UiEventKind::Response:
        for (uint8_t i = 0; i < button_count_; ++i) if (buttons_[i].sequence == incoming.sequence) {
            lv_obj_set_style_border_color(buttons_[i].object,incoming.value ? style::success : style::danger,0);
            buttons_[i].feedback_until = now_ms() + 900; buttons_[i].sequence = 0;
        }
        if (!incoming.value) toast(incoming.text,true);
        break;
    case UiEventKind::Profiles:
        profiles_.reset(incoming.profiles); grid_page_ = 0;
        std::snprintf(settings_.active_profile,sizeof(settings_.active_profile),"%s",profiles_->profiles[profiles_->active].id);
        save_preferences(); if (page_ != Page::Calibration) schedule(page_); toast("Profiles synchronized"); break;
    case UiEventKind::Notice: toast(incoming.text,true); update_values(); break;
    }
}

void UiManager::wake_event(lv_event_t* event) {
    auto* self = static_cast<UiManager*>(lv_event_get_user_data(event));
    self->schedule(self->previous_page_); self->transition_until_ = now_ms() + 220;
}
void UiManager::build_calibration() {
    cal_index_ = cal_phase_ = cal_count_ = 0; cal_counter_ = 0;
    auto* title = style::label(root_,"Touch calibration",&lv_font_montserrat_20);
    lv_obj_align(title,LV_ALIGN_TOP_MID,0,8);
    cal_instruction_ = style::label(root_,"",&lv_font_montserrat_14,true);
    lv_obj_set_style_text_align(cal_instruction_,LV_TEXT_ALIGN_CENTER,0);
    cal_target_ = lv_obj_create(root_); lv_obj_remove_style_all(cal_target_);
    lv_obj_set_size(cal_target_,42,42); lv_obj_set_style_radius(cal_target_,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_border_width(cal_target_,2,0); lv_obj_set_style_border_color(cal_target_,style::accent,0);
    auto* center = lv_obj_create(cal_target_); lv_obj_remove_style_all(center); lv_obj_set_size(center,6,6);
    lv_obj_set_style_bg_color(center,style::foreground,0); lv_obj_set_style_bg_opa(center,LV_OPA_COVER,0);
    lv_obj_set_style_radius(center,LV_RADIUS_CIRCLE,0); lv_obj_center(center);
    calibration_target();
}
void UiManager::calibration_target() {
    int x, y; lcd_.native_to_logical(native_x_[cal_index_],native_y_[cal_index_],x,y);
    lv_obj_set_pos(cal_target_,x - 21,y - 21); lv_obj_set_style_border_color(cal_target_,style::accent,0);
    char text[128]; std::snprintf(text,sizeof(text),"Hold the target until it turns green\nPoint %d of 3",cal_index_ + 1);
    style::set_text(cal_instruction_,text);
    lv_obj_set_width(cal_instruction_,lcd_.width() - 40); lv_label_set_long_mode(cal_instruction_,LV_LABEL_LONG_WRAP);
    lv_obj_align(cal_instruction_,LV_ALIGN_CENTER,0,y < lcd_.height() / 2 ? 75 : -75);
}
void UiManager::calibration_tick(const TouchSample& sample) {
    if (sample.counter == cal_counter_) return;
    cal_counter_ = sample.counter;
    switch (cal_phase_) {
    case 0: if (!sample.pressed) cal_phase_ = 1; break;
    case 1: if (sample.pressed) { cal_settle_ = now_ms() + 80; cal_phase_ = 2; } break;
    case 2:
        if (!sample.pressed) cal_phase_ = 1;
        else if (now_ms() >= cal_settle_) {
            cal_count_ = 0; cal_sum_x_ = cal_sum_y_ = 0;
            cal_min_x_ = cal_min_y_ = 4095; cal_max_x_ = cal_max_y_ = 0; cal_phase_ = 3;
        }
        break;
    case 3:
        if (!sample.pressed) { cal_phase_ = 1; style::set_text(cal_instruction_,"Hold a little longer. Try this point again."); break; }
        cal_sum_x_ += sample.raw_x; cal_sum_y_ += sample.raw_y;
        cal_min_x_ = std::min(cal_min_x_,sample.raw_x); cal_max_x_ = std::max(cal_max_x_,sample.raw_x);
        cal_min_y_ = std::min(cal_min_y_,sample.raw_y); cal_max_y_ = std::max(cal_max_y_,sample.raw_y);
        if (++cal_count_ >= 30) {
            if (cal_max_x_ - cal_min_x_ > 160 || cal_max_y_ - cal_min_y_ > 160) {
                cal_phase_ = 0; style::set_text(cal_instruction_,"Keep the stylus steady and try again."); break;
            }
            raw_x_[cal_index_] = cal_sum_x_ / cal_count_; raw_y_[cal_index_] = cal_sum_y_ / cal_count_;
            lv_obj_set_style_border_color(cal_target_,style::success,0); cal_phase_ = 4;
        }
        break;
    case 4:
        if (!sample.pressed) {
            if (++cal_index_ < 3) { cal_phase_ = 1; calibration_target(); }
            else {
                const esp_err_t result = touch_.calibrate(raw_x_,raw_y_,native_x_,native_y_);
                if (result == ESP_OK) { schedule(Page::Home); toast("Touch calibration saved"); }
                else { cal_index_ = cal_phase_ = 0; calibration_target(); toast(result == ESP_ERR_INVALID_ARG ?
                       "Calibration points invalid. Please repeat." : "Calibration NVS save failed. Please repeat.",true); }
            }
        }
        break;
    default: break;
    }
}

void UiManager::run() {
    while (true) {
        UiEvent incoming;
        for (int i = 0; i < 8 && xQueueReceive(ui_events,&incoming,0) == pdTRUE; ++i) event(incoming);
        Telemetry telemetry;
        if (xQueueReceive(telemetry_mailbox,&telemetry,0) == pdTRUE) {
            telemetry_ = telemetry;
            timeval time{}; time.tv_sec = telemetry.epoch; settimeofday(&time,nullptr);
            if (settings_.utc_offset != telemetry.utc_offset) { settings_.utc_offset = telemetry.utc_offset; save_preferences(); }
            update_values(); update_clock();
            if (chart_) {
                lv_chart_set_next_value(chart_,cpu_series_,telemetry.cpu < 0 ? LV_CHART_POINT_NONE : static_cast<int32_t>(telemetry.cpu));
                lv_chart_set_next_value(chart_,ram_series_,telemetry.ram < 0 ? LV_CHART_POINT_NONE : static_cast<int32_t>(telemetry.ram));
            }
        }
        TouchSample sample = touch_.sample();
        if (page_ == Page::Calibration) calibration_tick(sample);
        else if (sample.pressed && page_ != Page::Screensaver) last_interaction_ = now_ms();
        if (rebuild_) {
            rebuild_ = false;
            if (orientation_change_) { lcd_.set_orientation(settings_.orientation); orientation_change_ = false; }
            if (theme_change_) {
                style::initialize(settings_.theme);
                lv_display_set_theme(lcd_.display(),lv_theme_default_init(lcd_.display(),style::accent,
                                     style::muted,false,&lv_font_montserrat_14));
                theme_change_ = false;
            }
            build(pending_page_);
        }
        const uint64_t now = now_ms();
        if (now >= next_clock_) { update_clock(); update_values(); next_clock_ = now + 1000; }
        for (uint8_t i = 0; i < button_count_; ++i) if (buttons_[i].feedback_until && now >= buttons_[i].feedback_until) {
            lv_obj_set_style_border_color(buttons_[i].object,style::border,0);
            buttons_[i].feedback_until = 0; buttons_[i].sequence = 0;
        }
        if (toast_until_ && now >= toast_until_) { lv_obj_add_flag(toast_,LV_OBJ_FLAG_HIDDEN); toast_until_ = 0; }
        if (settings_.screensaver && page_ != Page::Calibration && page_ != Page::Screensaver && !dialog_ && !pairing_ &&
            now - last_interaction_ >= settings_.idle_seconds * 1000) { previous_page_ = page_; schedule(Page::Screensaver); }
        uint32_t delay = lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(std::clamp<uint32_t>(delay,2,10)));
    }
}
}
