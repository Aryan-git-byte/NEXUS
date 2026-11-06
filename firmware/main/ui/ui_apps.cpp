#include "ui_manager.hpp"
#include "ui_styles.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace nexus {
lv_obj_t* UiManager::app_button(lv_obj_t* parent, const char* text, AppControl control,
                              int x, int y, int width, int height) {
    auto* object = style::action_button(parent,text);
    lv_obj_set_pos(object,x,y); lv_obj_set_size(object,width,height);
    auto* label = lv_obj_get_child(object,0);
    lv_obj_set_width(label,width - 12); lv_label_set_long_mode(label,LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0); lv_obj_center(label);
    lv_obj_set_user_data(object,reinterpret_cast<void*>(static_cast<uintptr_t>(control)));
    lv_obj_add_event_cb(object,apps_event,LV_EVENT_CLICKED,this);
    return object;
}

void UiManager::clear_app_views() {
    calc_value_ = calc_expression_ = game_score_ = game_note_ = nullptr;
    snake_image_ = snake_start_ = snake_speed_ = nullptr;
    timer_value_ = timer_status_ = timer_detail_ = timer_start_ = timer_sound_ = timer_progress_ = nullptr;
    pet_large_image_ = pet_mood_ = pet_pats_ = pet_rest_ = pet_visible_ = nullptr;
    std::fill(std::begin(game_cells_),std::end(game_cells_),nullptr);
    std::fill(std::begin(game_labels_),std::end(game_labels_),nullptr);
    std::fill(std::begin(pet_stat_labels_),std::end(pet_stat_labels_),nullptr);
    std::fill(std::begin(pet_stat_bars_),std::end(pet_stat_bars_),nullptr);
}

void UiManager::build_apps() {
    const bool portrait = lcd_.width() < lcd_.height();
    const int width = lcd_.width() - 24, height = lcd_.height() - 98;
    auto* title = style::label(content_,"On your deck",&lv_font_montserrat_20);
    lv_obj_set_pos(title,0,0);
    auto* note = style::label(content_,"Works offline",&lv_font_montserrat_14,true);
    lv_obj_align(note,LV_ALIGN_TOP_RIGHT,0,4);
    struct App { const char* title; const char* icon; AppControl control; };
    static const App items[] = {
        {"Calculator","calculator",AppControl::Calculator},{"Timers","timer",AppControl::Timer},
        {"2048","2048",AppControl::Game2048},{"Snake","snake",AppControl::Snake},
        {"Pixel cat","pet",AppControl::Pet},{"Macros","custom",AppControl::Macros}
    };
    const int columns = portrait ? 2 : 3, rows = portrait ? 3 : 2;
    const int cell_w = (width - (columns - 1) * 8) / columns;
    const int cell_h = (height - 30 - (rows - 1) * 8) / rows;
    for (int i = 0; i < 6; ++i) {
        auto* button = app_button(content_,items[i].title,items[i].control,
                                  i % columns * (cell_w + 8),30 + i / columns * (cell_h + 8),cell_w,cell_h);
        lv_obj_set_style_bg_color(button,style::tile_color(items[i].icon),0);
        auto* label = lv_obj_get_child(button,0); lv_obj_align(label,LV_ALIGN_BOTTOM_MID,0,-4);
        if (items[i].control == AppControl::Pet) {
            pet_large_image_ = lv_image_create(button);
            lv_image_set_src(pet_large_image_,&pet_descriptor_); lv_image_set_pivot(pet_large_image_,0,0);
            lv_image_set_scale(pet_large_image_,384); lv_image_set_antialias(pet_large_image_,false);
            lv_obj_set_pos(pet_large_image_,(cell_w - 36) / 2,(cell_h - 36) / 2 - 10);
            lv_obj_remove_flag(pet_large_image_,LV_OBJ_FLAG_CLICKABLE);
        } else if (items[i].control == AppControl::Snake) {
            auto* badge = lv_obj_create(button); lv_obj_remove_style_all(badge);
            lv_obj_remove_flag(badge,LV_OBJ_FLAG_SCROLLABLE); lv_obj_remove_flag(badge,LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_size(badge,44,38); lv_obj_set_pos(badge,(cell_w - 44) / 2,(cell_h - 38) / 2 - 10);
            const int segments[][4] = {{2,2,30,8},{24,2,8,18},{12,12,20,8},{12,12,8,22},{12,26,28,8}};
            for (const auto& segment : segments) {
                auto* part = lv_obj_create(badge); lv_obj_remove_style_all(part);
                lv_obj_remove_flag(part,LV_OBJ_FLAG_SCROLLABLE); lv_obj_remove_flag(part,LV_OBJ_FLAG_CLICKABLE);
                lv_obj_set_pos(part,segment[0],segment[1]); lv_obj_set_size(part,segment[2],segment[3]);
                lv_obj_set_style_bg_color(part,lv_color_hex(0x1686e4),0);
                lv_obj_set_style_bg_opa(part,LV_OPA_COVER,0); lv_obj_set_style_radius(part,3,0);
            }
            auto* eye = lv_obj_create(badge); lv_obj_remove_style_all(eye);
            lv_obj_remove_flag(eye,LV_OBJ_FLAG_CLICKABLE); lv_obj_set_pos(eye,35,28); lv_obj_set_size(eye,3,3);
            lv_obj_set_style_bg_color(eye,lv_color_white(),0); lv_obj_set_style_bg_opa(eye,LV_OPA_COVER,0);
        } else {
            auto* symbol = style::label(button,style::icon(items[i].icon),&lv_font_montserrat_28);
            lv_obj_align(symbol,LV_ALIGN_CENTER,0,-10);
        }
    }
}

void UiManager::local_changed() {
    if (!local_dirty_) next_local_save_ = now_ms() + 2500;
    local_dirty_ = true;
}

void UiManager::apps_event(lv_event_t* event) {
    auto* self = static_cast<UiManager*>(lv_event_get_user_data(event));
    if (now_ms() < self->transition_until_) return;
    auto* object = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    const auto control = static_cast<AppControl>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(object)));
    const uint16_t code = static_cast<uint16_t>(control);
    self->last_interaction_ = now_ms();
    if (code >= 100 && code <= 119) {
        self->calculator_.press(calculator_keys[code - 100]); self->update_calculator(); return;
    }
    if (code >= 200 && code <= 203) {
        const bool won = self->game2048_.reached_2048();
        if (self->game2048_.move(static_cast<apps::Direction>(code - 200),self->random_)) {
            if (self->game2048_.score() > self->local_.best_2048) {
                self->local_.best_2048 = self->game2048_.score(); self->local_changed();
            }
            if (!won && self->game2048_.reached_2048()) self->toast("2048! Keep going for a new best.");
        }
        self->update_2048(); return;
    }
    if (code >= 220 && code <= 223) {
        self->snake_.turn(static_cast<apps::Direction>(code - 220)); return;
    }
    if (code >= 330 && code <= 335) {
        const int part = (code - 330) % 3, maximum = part == 0 ? 24 : 59;
        int value = self->duration_values_[part] + (code < 333 ? 1 : -1);
        self->duration_values_[part] = std::clamp(value,0,maximum);
        if (self->duration_values_[0] == 24) self->duration_values_[1] = self->duration_values_[2] = 0;
        self->update_duration(); return;
    }
    switch (control) {
    case AppControl::Hub: self->schedule(Page::Apps); break;
    case AppControl::Calculator: self->schedule(Page::Calculator); break;
    case AppControl::Timer: self->schedule(Page::Timer); break;
    case AppControl::Game2048: self->schedule(Page::Game2048); break;
    case AppControl::Snake: self->schedule(Page::Snake); break;
    case AppControl::Pet: self->schedule(Page::Pet); break;
    case AppControl::Macros: self->schedule(Page::Custom); break;
    case AppControl::GameNew: self->game2048_.reset(self->random_); self->update_2048(); break;
    case AppControl::GameUndo:
        if (!self->game2048_.undo()) self->toast("One undo is available after a move");
        self->update_2048(); break;
    case AppControl::SnakeNew:
        self->snake_.reset(self->random_); self->snake_running_ = false; self->update_snake(); break;
    case AppControl::SnakeStart:
        if (self->snake_.over()) self->snake_.reset(self->random_);
        self->snake_running_ = !self->snake_running_;
        self->next_snake_ = now_ms() + 300; self->update_snake(); break;
    case AppControl::SnakeSpeed:
        self->local_.snake_speed = (self->local_.snake_speed + 1) % 3;
        self->local_changed(); self->update_snake(); break;
    case AppControl::TimerCountdown: case AppControl::TimerFocus: case AppControl::TimerWatch:
        if (self->timer_.state() == apps::TimerState::Running) {
            self->toast("Pause or reset before changing timer mode",true); break;
        }
        self->local_.timer_mode = code - 300;
        self->timer_.set_mode(static_cast<apps::TimerMode>(self->local_.timer_mode));
        self->local_changed(); self->schedule(Page::Timer); break;
    case AppControl::TimerStart:
        if (self->timer_.state() == apps::TimerState::Running) self->timer_.pause(now_ms());
        else self->timer_.start(now_ms());
        self->update_timer(now_ms()); break;
    case AppControl::TimerReset: self->timer_.reset(); self->update_timer(now_ms()); break;
    case AppControl::TimerSet: self->open_duration(); break;
    case AppControl::TimerSound:
        self->local_.timer_sound = !self->local_.timer_sound;
        self->local_changed(); self->update_timer(now_ms()); break;
    case AppControl::TimerLap: self->timer_.lap(now_ms()); self->update_timer(now_ms()); break;
    case AppControl::TimerPreset1: case AppControl::TimerPreset5:
        if (self->timer_.set_duration(control == AppControl::TimerPreset1 ? 60 : 300)) {
            self->local_.timer_seconds = control == AppControl::TimerPreset1 ? 60 : 300;
            self->local_changed(); self->update_timer(now_ms());
        } else self->toast("Pause before setting a new countdown",true);
        break;
    case AppControl::DurationApply: {
        const uint32_t seconds = self->duration_values_[0] * 3600 + self->duration_values_[1] * 60 + self->duration_values_[2];
        if (!self->timer_.set_duration(seconds)) { self->toast("Choose 1 second to 24 hours",true); break; }
        self->local_.timer_seconds = seconds; self->local_changed();
        self->close_duration(); self->update_timer(now_ms()); break;
    }
    case AppControl::DurationCancel: self->close_duration(); break;
    case AppControl::PetPat: self->pet_.pat(self->local_,now_ms()); self->local_changed(); self->update_pet(); break;
    case AppControl::PetFeed: self->pet_.feed(self->local_); self->local_changed(); self->update_pet(); break;
    case AppControl::PetPlay: self->pet_.play(self->local_); self->local_changed(); self->update_pet(); break;
    case AppControl::PetRest: self->pet_.rest(self->local_); self->local_changed(); self->update_pet(); break;
    case AppControl::PetVisible:
        self->local_.pet_visible = !self->local_.pet_visible; self->local_changed();
        self->position_pet(); self->update_pet(); break;
    case AppControl::PetName: {
        const char* names[] = {"Mochi","Nori","Pixel","Coco"};
        unsigned index = 0;
        for (unsigned i = 0; i < 4; ++i) if (std::strcmp(self->local_.pet_name,names[i]) == 0) index = (i + 1) % 4;
        std::snprintf(self->local_.pet_name,sizeof(self->local_.pet_name),"%s",names[index]);
        self->local_changed(); self->schedule(Page::Pet); break;
    }
    default: break;
    }
    self->pet_frame_ = -1; self->draw_pet(now_ms());
}

void UiManager::update_apps(uint64_t now) {
    const apps::TimerEvent timer_event = timer_.tick(now);
    if (timer_event != apps::TimerEvent::None) {
        if (page_ == Page::Screensaver) schedule(Page::Timer);
        const char* message = timer_event == apps::TimerEvent::Countdown ? "Timer complete" :
                              timer_event == apps::TimerEvent::Focus ? "Focus complete. Time for a break." : "Break complete. Ready to focus.";
        toast(message);
        if (local_.timer_sound) {
            timer_alert_sequence_ = ble_.timer_alert(timer_event);
            if (!timer_alert_sequence_ && !ble_.ready()) toast("Timer complete. PC sound needs a connection.");
        }
    }
    if (page_ == Page::Snake && snake_running_ && now >= next_snake_) {
        snake_.step(random_);
        if (snake_.over()) snake_running_ = false;
        if (snake_.score() > local_.best_snake) { local_.best_snake = snake_.score(); local_changed(); }
        static const unsigned periods[] = {320,230,160};
        next_snake_ = now + periods[local_.snake_speed];
        update_snake();
    }
    if (pet_.tick(local_,now,static_cast<int64_t>(std::time(nullptr)))) { local_changed(); update_pet(); }
    if (now >= next_app_refresh_) { update_timer(now); next_app_refresh_ = now + 100; }
    if (now >= next_pet_frame_) { draw_pet(now); next_pet_frame_ = now + 250; }
    if (local_save_pending_ && now - local_save_sent_ > 8000) {
        local_save_pending_ = false; local_dirty_ = true; next_local_save_ = now + 30000;
    }
    if (local_dirty_ && !local_save_pending_ && now >= next_local_save_) {
        const int64_t epoch = std::time(nullptr);
        if (epoch >= 1700000000) local_.pet_epoch = epoch;
        if (ble_.save_apps(local_)) {
            local_dirty_ = false; local_save_pending_ = true; local_save_sent_ = now;
        } else next_local_save_ = now + 3000;
    }
}
}
