#include "ui_manager.hpp"
#include "ui_styles.hpp"
#include "diagnostics/boot_info.hpp"
#include <cstdio>
#include <cstring>
#include "esp_chip_info.h"
#include "esp_heap_caps.h"

namespace nexus {
namespace {
void bind(lv_obj_t* object, uint8_t operation, UiManager* manager, lv_event_cb_t callback) {
    lv_obj_set_user_data(object,reinterpret_cast<void*>(static_cast<uintptr_t>(operation)));
    lv_obj_add_event_cb(object,callback,LV_EVENT_ALL,manager);
}
}
void UiManager::build_settings() {
    lv_obj_add_flag(content_,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(content_,LV_SCROLLBAR_MODE_AUTO);
    const int width = lcd_.width() - 24;
    lv_obj_set_flex_flow(content_,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content_,12,0);
    lv_obj_set_style_bg_color(content_,style::accent,LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(content_,LV_OPA_50,LV_PART_SCROLLBAR);
    lv_obj_t* section = nullptr;
    auto heading = [&](const char* name) {
        section = style::card(content_);
        lv_obj_set_size(section,width,LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(section,12,0);
        lv_obj_set_flex_flow(section,LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(section,10,0);
        style::label(section,name,&lv_font_montserrat_16);
    };
    auto row = [&]() {
        auto* object = lv_obj_create(section);
        lv_obj_remove_style_all(object);
        lv_obj_remove_flag(object,LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(object,lv_pct(100),40);
        lv_obj_set_flex_flow(object,LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(object,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(object,8,0);
        return object;
    };
    auto dropdown = [&](const char* name, const char* options, int selected, uint8_t operation) {
        auto* container = row();
        auto* label = style::label(container,name,&lv_font_montserrat_14);
        lv_obj_set_width(label,width < 350 ? 108 : 152);
        auto* control = lv_dropdown_create(container);
        // Styles are installed before geometry: remove_style_all also removes
        // local position/size properties. Flex rows keep all four controls apart.
        style::apply_dropdown(control);
        lv_obj_set_height(control,40); lv_obj_set_flex_grow(control,1);
        lv_dropdown_set_options(control,options); lv_dropdown_set_selected(control,selected);
        bind(control,operation,this,settings_event);
    };
    auto action = [&](const char* text, uint8_t operation, bool destructive = false) {
        auto* button = style::action_button(section,text);
        lv_obj_set_size(button,lv_pct(100),40);
        if (destructive) {
            lv_obj_set_style_bg_color(button,lv_color_hex(0xffe4e7),0);
            lv_obj_set_style_text_color(lv_obj_get_child(button,0),style::danger,0);
        }
        bind(button,operation,this,settings_event);
    };
    heading("Bluetooth");
    settings_connection_ = style::label(section,"Disconnected",&lv_font_montserrat_14,true);
    lv_obj_set_width(settings_connection_,lv_pct(100));
    lv_label_set_long_mode(settings_connection_,LV_LABEL_LONG_WRAP);
    action("Pair new PC",8); action("Forget saved PC",9);
    heading("Display");
    style::label(section,"Brightness",&lv_font_montserrat_14);
    auto* slider_row = row();
    lv_obj_set_style_pad_left(slider_row,12,0);
    auto* slider = lv_slider_create(slider_row);
    lv_obj_set_size(slider,width - 50,10); lv_obj_center(slider);
    lv_slider_set_range(slider,5,100); lv_slider_set_value(slider,settings_.brightness,LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider,style::control,LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider,style::accent,LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider,style::accent,LV_PART_KNOB);
    bind(slider,1,this,settings_event);
    dropdown("Orientation","Landscape\nPortrait",settings_.orientation == Orientation::Landscape ? 0 : 1,2);
    dropdown("Theme","Sky light\nPeach light",settings_.theme,3);
    auto* saver_row = row();
    auto* label = style::label(saver_row,"Clock screensaver",&lv_font_montserrat_14);
    lv_obj_set_flex_grow(label,1);
    auto* saver = lv_switch_create(saver_row);
    lv_obj_set_size(saver,48,28);
    lv_obj_set_style_bg_color(saver,style::control,LV_PART_MAIN);
    lv_obj_set_style_bg_color(saver,style::accent,static_cast<lv_style_selector_t>(LV_PART_INDICATOR) | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(saver,lv_color_white(),LV_PART_KNOB);
    if (settings_.screensaver) lv_obj_add_state(saver,LV_STATE_CHECKED);
    bind(saver,6,this,settings_event);
    const uint32_t idle[] = {60,90,180,300}; int selected = 1;
    for (int i = 0; i < 4; ++i) if (idle[i] == settings_.idle_seconds) selected = i;
    dropdown("Idle time","1 minute\n90 seconds\n3 minutes\n5 minutes",selected,7);
    action("Calibrate touchscreen",5);
    heading("Profiles");
    char names[384]{};
    for (uint8_t i = 0; i < profiles_->count; ++i) {
        if (i) std::strcat(names,"\n");
        std::strcat(names,profiles_->profiles[i].name);
    }
    dropdown("Active profile",names,profiles_->active,4);
    heading("Device");
    char info[256];
    std::snprintf(info,sizeof(info),"NEXUS DECK  %s\nESP32-S3 / ILI9488 / XPT2046\n%d x %d  RGB565\nFree internal RAM: %u KB\nLast reset: %s",
        config::kVersion,lcd_.width(),lcd_.height(),static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
        boot_info().reset_reason);
    label = style::label(section,info,&lv_font_montserrat_14,true);
    lv_obj_set_width(label,lv_pct(100)); lv_label_set_long_mode(label,LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(label,4,0);
    action("Restart device",10); action("Factory reset",11,true);
}

void UiManager::settings_event(lv_event_t* event) {
    auto* self = static_cast<UiManager*>(lv_event_get_user_data(event));
    auto* object = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    const auto code = lv_event_get_code(event);
    if (code != LV_EVENT_VALUE_CHANGED && code != LV_EVENT_RELEASED && code != LV_EVENT_CLICKED) return;
    const auto operation = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(object));
    self->last_interaction_ = now_ms();
    if (operation == 1) {
        if (code == LV_EVENT_VALUE_CHANGED) { self->settings_.brightness = lv_slider_get_value(object); self->brightness(); }
        if (code == LV_EVENT_RELEASED) self->save_preferences();
    } else if (code == LV_EVENT_VALUE_CHANGED) {
        switch (operation) {
        case 2:
            self->settings_.orientation = lv_dropdown_get_selected(object) == 0 ? Orientation::Landscape : Orientation::Portrait;
            self->orientation_change_ = true; self->schedule(Page::Settings); self->save_preferences(); break;
        case 3: self->settings_.theme = lv_dropdown_get_selected(object); self->theme_change_ = true;
                self->schedule(Page::Settings); self->save_preferences(); break;
        case 4:
            if (lv_dropdown_get_selected(object) >= self->profiles_->count) return;
            self->profiles_->active = lv_dropdown_get_selected(object);
            std::snprintf(self->settings_.active_profile,sizeof(self->settings_.active_profile),"%s",
                          self->profiles_->profiles[self->profiles_->active].id);
            self->ble_.select_profile(self->settings_.active_profile); self->save_preferences(); break;
        case 6: self->settings_.screensaver = lv_obj_has_state(object,LV_STATE_CHECKED); self->save_preferences(); break;
        case 7: { const uint32_t values[] = {60,90,180,300};
                  const auto index = lv_dropdown_get_selected(object);
                  if (index >= 4) return;
                  self->settings_.idle_seconds = values[index];
                  self->save_preferences(); break; }
        default: break;
        }
    } else if (code == LV_EVENT_CLICKED) {
        switch (operation) {
        case 5: self->schedule(Page::Calibration); break;
        case 8: if (!self->ble_.open_pairing()) self->toast("Bluetooth queue busy",true); break;
        case 9: self->confirm("Forget PC","Remove this PC's bond? Also remove NEXUS DECK from Windows Bluetooth devices.",2); break;
        case 10: self->confirm("Restart","Restart NEXUS DECK now?",0); break;
        case 11: self->confirm("Factory reset","Erase profiles, settings, touch calibration, and the PC bond?",1); break;
        default: break;
        }
    }
}
void UiManager::save_preferences() {
    if (!ble_.save_settings(settings_)) toast("Settings queue busy; try again",true);
}
}
