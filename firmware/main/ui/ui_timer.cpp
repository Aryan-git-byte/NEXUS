#include "ui_manager.hpp"
#include "ui_styles.hpp"
#include "assets/clock_fonts.h"
#include <cstdio>

namespace nexus {
namespace {
void duration_text(char* text, size_t size, uint64_t milliseconds, bool countdown) {
    const uint64_t seconds = (milliseconds + (countdown ? 999 : 0)) / 1000;
    const unsigned long hours = seconds / 3600, minutes = seconds / 60 % 60, remainder = seconds % 60;
    if (hours) std::snprintf(text,size,"%02lu:%02lu:%02lu",hours,minutes,remainder);
    else std::snprintf(text,size,"%02lu:%02lu",minutes,remainder);
}
}
void UiManager::build_timer() {
    const int width = lcd_.width() - 24, height = lcd_.height() - 98;
    const int mode_w = (width - 12) / 3;
    const char* modes[] = {"Timer","Focus","Stopwatch"};
    for (unsigned i = 0; i < 3; ++i) {
        auto* button = app_button(content_,modes[i],static_cast<AppControl>(300 + i),i * (mode_w + 6),0,mode_w,38);
        if (i == static_cast<unsigned>(timer_.mode())) lv_obj_add_style(button,&style::selected,0);
    }
    auto* card = style::card(content_); lv_obj_set_style_pad_all(card,0,0);
    lv_obj_set_pos(card,0,44); lv_obj_set_size(card,width,height - 140);
    timer_value_ = style::label(card,"05:00",&lv_font_montserrat_40);
    lv_obj_set_width(timer_value_,width - 20); lv_label_set_long_mode(timer_value_,LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(timer_value_,LV_TEXT_ALIGN_CENTER,0); lv_obj_align(timer_value_,LV_ALIGN_CENTER,0,-12);
    timer_status_ = style::label(card,"Ready",&lv_font_montserrat_14,true);
    lv_obj_set_width(timer_status_,width - 20); lv_obj_set_height(timer_status_,18);
    lv_label_set_long_mode(timer_status_,LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(timer_status_,LV_TEXT_ALIGN_CENTER,0); lv_obj_align(timer_status_,LV_ALIGN_BOTTOM_MID,0,-7);
    timer_progress_ = lv_bar_create(card); lv_obj_set_size(timer_progress_,width - 18,3);
    lv_obj_align(timer_progress_,LV_ALIGN_BOTTOM_MID,0,-2);
    lv_obj_set_style_bg_color(timer_progress_,style::control,LV_PART_MAIN);
    lv_obj_set_style_bg_color(timer_progress_,style::accent,LV_PART_INDICATOR);
    timer_start_ = app_button(content_,"Start",AppControl::TimerStart,0,height - 88,(width - 6) / 2,40);
    lv_obj_set_style_bg_color(timer_start_,lv_color_hex(0xddebff),0);
    app_button(content_,"Reset",AppControl::TimerReset,(width - 6) / 2 + 6,height - 88,(width - 6) / 2,40);
    if (timer_.mode() == apps::TimerMode::Countdown) {
        const int cell_w = (width - 18) / 4;
        app_button(content_,"Set",AppControl::TimerSet,0,height - 40,cell_w,40);
        app_button(content_,"1m",AppControl::TimerPreset1,cell_w + 6,height - 40,cell_w,40);
        app_button(content_,"5m",AppControl::TimerPreset5,2 * (cell_w + 6),height - 40,cell_w,40);
        timer_sound_ = app_button(content_,"PC On",AppControl::TimerSound,3 * (cell_w + 6),height - 40,cell_w,40);
    } else {
        const int control_w = width / 3;
        if (timer_.mode() == apps::TimerMode::Stopwatch)
            app_button(content_,"Lap",AppControl::TimerLap,0,height - 40,control_w,40);
        else timer_sound_ = app_button(content_,"PC On",AppControl::TimerSound,0,height - 40,control_w,40);
        timer_detail_ = style::label(content_,"",&lv_font_montserrat_14,true);
        lv_obj_set_pos(timer_detail_,control_w + 8,height - 40);
        lv_obj_set_size(timer_detail_,width - control_w - 8,40);
        lv_label_set_long_mode(timer_detail_,LV_LABEL_LONG_WRAP);
    }
    update_timer(now_ms());
}
void UiManager::update_timer(uint64_t now) {
    if (!timer_value_) return;
    const uint64_t milliseconds = timer_.milliseconds(now);
    char text[96]; duration_text(text,sizeof(text),milliseconds,timer_.mode() != apps::TimerMode::Stopwatch);
    const lv_font_t* font = lcd_.width() < lcd_.height() && milliseconds < 3600000 ? &nexus_clock_80 : &lv_font_montserrat_40;
    if (lv_obj_get_style_text_font(timer_value_,0) != font) lv_obj_set_style_text_font(timer_value_,font,0);
    style::set_text(timer_value_,text); lv_obj_align(timer_value_,LV_ALIGN_CENTER,0,-12);
    const auto state = timer_.state();
    const char* states[] = {"Ready","Running","Paused","Complete"};
    if (timer_.mode() == apps::TimerMode::Focus)
        std::snprintf(text,sizeof(text),"%s  |  %s  |  Session %lu",states[static_cast<unsigned>(state)],
                      timer_.on_break() ? "Break" : "Focus",static_cast<unsigned long>(timer_.focus_sessions() + 1));
    else std::snprintf(text,sizeof(text),"%s",states[static_cast<unsigned>(state)]);
    style::set_text(timer_status_,text);
    style::set_text(lv_obj_get_child(timer_start_,0),state == apps::TimerState::Running ? "Pause" :
                    state == apps::TimerState::Paused ? "Resume" : state == apps::TimerState::Finished ? "Again" : "Start");
    if (timer_sound_) style::set_text(lv_obj_get_child(timer_sound_,0),
                                     local_.timer_sound ? LV_SYMBOL_VOLUME_MAX " On" : LV_SYMBOL_MUTE " Off");
    if (timer_progress_) lv_bar_set_value(timer_progress_,timer_.progress(now),LV_ANIM_OFF);
    if (timer_detail_) {
        if (timer_.mode() == apps::TimerMode::Stopwatch && timer_.lap_count()) {
            char value[32]; duration_text(value,sizeof(value),timer_.last_lap(),false);
            std::snprintf(text,sizeof(text),"Lap %lu  %s",static_cast<unsigned long>(timer_.lap_count()),value);
        } else std::snprintf(text,sizeof(text),"%s",timer_.mode() == apps::TimerMode::Focus ?
                            "25m focus / 5m break\n15m break after four" : "Tap Lap while running");
        style::set_text(timer_detail_,text);
    }
}
void UiManager::open_duration() {
    if (duration_dialog_) return;
    if (timer_.mode() != apps::TimerMode::Countdown || timer_.state() == apps::TimerState::Running) {
        toast("Pause the countdown before changing its duration",true); return;
    }
    const uint32_t seconds = local_.timer_seconds;
    duration_values_[0] = seconds / 3600; duration_values_[1] = seconds / 60 % 60; duration_values_[2] = seconds % 60;
    duration_dialog_ = lv_obj_create(lv_layer_top()); lv_obj_remove_style_all(duration_dialog_);
    lv_obj_set_size(duration_dialog_,lcd_.width(),lcd_.height());
    lv_obj_set_style_bg_color(duration_dialog_,lv_color_black(),0); lv_obj_set_style_bg_opa(duration_dialog_,LV_OPA_40,0);
    lv_obj_remove_flag(duration_dialog_,LV_OBJ_FLAG_SCROLLABLE); lv_obj_add_flag(duration_dialog_,LV_OBJ_FLAG_CLICKABLE);
    auto* panel = style::card(duration_dialog_); lv_obj_set_style_pad_all(panel,0,0);
    const int width = lcd_.width() - 32, cell = (width - 40) / 3;
    lv_obj_set_size(panel,width,260); lv_obj_center(panel);
    auto* title = style::label(panel,"Set countdown",&lv_font_montserrat_20); lv_obj_set_pos(title,12,10);
    const char* names[] = {"Hours","Minutes","Seconds"};
    for (unsigned i = 0; i < 3; ++i) {
        const int x = 12 + i * (cell + 8);
        auto* label = style::label(panel,names[i],&lv_font_montserrat_14,true);
        lv_obj_set_width(label,cell); lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0); lv_obj_set_pos(label,x,42);
        auto* plus = app_button(panel,LV_SYMBOL_PLUS,static_cast<AppControl>(330 + i),x,66,cell,40);
        lv_obj_add_event_cb(plus,apps_event,LV_EVENT_LONG_PRESSED_REPEAT,this);
        duration_labels_[i] = style::label(panel,"00",&lv_font_montserrat_28);
        lv_obj_set_pos(duration_labels_[i],x,114); lv_obj_set_width(duration_labels_[i],cell);
        lv_obj_set_style_text_align(duration_labels_[i],LV_TEXT_ALIGN_CENTER,0);
        auto* minus = app_button(panel,"-",static_cast<AppControl>(333 + i),x,156,cell,40);
        lv_obj_add_event_cb(minus,apps_event,LV_EVENT_LONG_PRESSED_REPEAT,this);
    }
    app_button(panel,"Cancel",AppControl::DurationCancel,12,206,(width - 30) / 2,40);
    auto* apply = app_button(panel,"Set timer",AppControl::DurationApply,18 + (width - 30) / 2,206,(width - 30) / 2,40);
    lv_obj_set_style_bg_color(apply,lv_color_hex(0xddebff),0);
    update_duration(); position_pet(); lv_obj_move_foreground(duration_dialog_);
}
void UiManager::update_duration() {
    for (unsigned i = 0; i < 3; ++i) {
        char text[8]; std::snprintf(text,sizeof(text),"%02u",duration_values_[i]);
        style::set_text(duration_labels_[i],text);
    }
}
void UiManager::close_duration() {
    if (!duration_dialog_) return;
    lv_obj_delete(duration_dialog_); duration_dialog_ = nullptr;
    for (auto& label : duration_labels_) label = nullptr;
    position_pet();
}
}
