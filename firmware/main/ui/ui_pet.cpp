#include "ui_manager.hpp"
#include "ui_styles.hpp"
#include <algorithm>
#include <cstdio>

namespace nexus {
void UiManager::position_pet() {
    if (!pet_overlay_) return;
    const bool blocked = page_ == Page::Calibration || dialog_ || pairing_ || duration_dialog_ || !local_.pet_visible;
    if (blocked) { lv_obj_add_flag(pet_overlay_,LV_OBJ_FLAG_HIDDEN); return; }
    lv_obj_remove_flag(pet_overlay_,LV_OBJ_FLAG_HIDDEN);
    const bool sleeping = page_ == Page::Screensaver;
    lv_obj_set_size(pet_overlay_,40,sleeping ? 24 : 42);
    lv_obj_set_pos(pet_overlay_,sleeping ? lcd_.width() - 48 : lcd_.width() - 153,sleeping ? lcd_.height() - 24 : 1);
    lv_obj_set_pos(pet_small_image_,sleeping ? 8 : 2,sleeping ? 0 : 2);
    lv_image_set_scale(pet_small_image_,sleeping ? 256 : 384);
    lv_image_set_antialias(pet_small_image_,false);
    if (sleeping) lv_obj_remove_flag(pet_overlay_,LV_OBJ_FLAG_CLICKABLE);
    else lv_obj_add_flag(pet_overlay_,LV_OBJ_FLAG_CLICKABLE);
}
void UiManager::pet_event(lv_event_t* event) {
    auto* self = static_cast<UiManager*>(lv_event_get_user_data(event));
    if (now_ms() < self->transition_until_ || self->page_ == Page::Screensaver) return;
    const auto code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) self->pet_long_sent_ = false;
    else if (code == LV_EVENT_LONG_PRESSED) {
        self->pet_long_sent_ = true; self->schedule(Page::Pet);
    } else if (code == LV_EVENT_CLICKED && !self->pet_long_sent_) {
        if (self->pet_.pat(self->local_,now_ms())) {
            self->last_interaction_ = now_ms(); self->local_changed(); self->update_pet();
            self->pet_frame_ = -1; self->draw_pet(now_ms());
        }
    }
}
void UiManager::draw_pet(uint64_t now) {
    if (!pet_small_image_) return;
    const bool mono = page_ == Page::Screensaver, asleep = mono || local_.pet_sleeping;
    const bool reacting = pet_.reacting(now) && !mono;
    const int phase = asleep ? 0 : (now / 500) % 8;
    const int frame = phase + (asleep ? 16 : 0) + (reacting ? 32 : 0) + (mono ? 64 : 0);
    if (frame == pet_frame_) return;
    pet_frame_ = frame; pet_pixels_.fill(0);
    const uint32_t outline = mono ? 0xfff5f5f5 : 0xff293246;
    const uint32_t fur = mono ? 0xfff5f5f5 : 0xffedaa66;
    const uint32_t light = mono ? 0xfff5f5f5 : 0xffffe9c8;
    const uint32_t pink = mono ? 0xff000000 : 0xffe85f8e;
    const uint32_t eyes = mono ? 0xff000000 : outline;
    auto dot = [&](int x,int y,uint32_t color) {
        if (x >= 0 && x < 24 && y >= 0 && y < 24) pet_pixels_[y * 24 + x] = color;
    };
    auto rect = [&](int x,int y,int w,int h,uint32_t color) {
        for (int yy = y; yy < y + h; ++yy) for (int xx = x; xx < x + w; ++xx) dot(xx,yy,color);
    };
    if (asleep) {
        rect(4,14,16,7,outline); rect(5,13,14,9,outline);
        rect(5,15,14,5,fur); rect(6,14,12,7,fur);
        rect(5,12,9,7,outline); rect(6,13,7,5,light);
        rect(5,10,2,3,outline); rect(12,10,2,3,outline);
        rect(7,15,2,1,eyes); rect(11,15,2,1,eyes);
        rect(17,16,4,4,outline); rect(18,17,2,2,fur);
        if (!mono) { dot(18,3,outline); dot(19,3,outline); dot(19,4,outline); dot(18,5,outline); dot(19,5,outline); }
    } else {
        const int bob = reacting ? phase % 2 : 0;
        rect(8,15 + bob,10,8,outline); rect(9,16 + bob,8,6,fur);
        rect(10,18 + bob,6,4,light);
        rect(7,22,5,2,outline); rect(15,22,5,2,outline);
        rect(8,22,3,1,light); rect(16,22,3,1,light);
        rect(19,16,3,6,outline); rect(21,phase % 2 ? 12 : 14,2,5,outline);
        rect(20,17,1,4,fur); rect(22,phase % 2 ? 13 : 15,1,3,fur);
        for (int row = 0; row < 5; ++row) {
            rect(5,3 + row,1 + row,1,outline);
            rect(19 - row,3 + row,1 + row,1,outline);
        }
        rect(6,5,2,3,pink); rect(17,5,2,3,pink);
        rect(5,8 + bob,16,9,outline); rect(7,7 + bob,12,11,outline);
        rect(6,9 + bob,14,7,fur); rect(8,8 + bob,10,9,fur);
        rect(8,13 + bob,10,4,light);
        if (phase == 7 || reacting) {
            rect(8,11 + bob,3,1,eyes); rect(15,11 + bob,3,1,eyes);
        } else {
            rect(8,10 + bob,2,3,eyes); rect(16,10 + bob,2,3,eyes);
            dot(8,10 + bob,0xffffffff); dot(16,10 + bob,0xffffffff);
        }
        rect(12,13 + bob,2,1,pink); dot(13,14 + bob,eyes);
        rect(11,15 + bob,2,1,eyes); rect(14,15 + bob,2,1,eyes);
        rect(3,13 + bob,3,1,eyes); rect(20,13 + bob,3,1,eyes);
        rect(10,17 + bob,7,1,mono ? outline : 0xff0875db);
        dot(13,18 + bob,mono ? outline : 0xffffdc60);
    }
    if (reacting) {
        rect(1,1,2,2,pink); rect(4,1,2,2,pink); rect(1,3,5,1,pink);
        rect(2,4,3,1,pink); dot(3,5,pink);
    }
    lv_image_cache_drop(&pet_descriptor_);
    lv_obj_invalidate(pet_small_image_);
    if (pet_large_image_) lv_obj_invalidate(pet_large_image_);
    update_pet();
}

void UiManager::build_pet() {
    const bool portrait = lcd_.width() < lcd_.height();
    const int width = lcd_.width() - 24, height = lcd_.height() - 98;
    const int right = portrait ? 0 : 184, controls_w = width - right;
    auto* name = app_button(content_,local_.pet_name,AppControl::PetName,0,0,portrait ? (width - 8) / 2 : 168,38);
    lv_obj_set_style_text_font(lv_obj_get_child(name,0),&lv_font_montserrat_20,0);
    pet_visible_ = app_button(content_,"Follow On",AppControl::PetVisible,
        portrait ? (width - 8) / 2 + 8 : right,0,portrait ? (width - 8) / 2 : controls_w,38);
    auto* stage = style::card(content_); lv_obj_set_style_pad_all(stage,0,0);
    lv_obj_set_style_bg_color(stage,lv_color_hex(0xfff0da),0);
    lv_obj_set_pos(stage,0,44); lv_obj_set_size(stage,portrait ? width : 168,portrait ? 178 : 174);
    lv_obj_add_flag(stage,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(stage,reinterpret_cast<void*>(static_cast<uintptr_t>(AppControl::PetPat)));
    lv_obj_add_event_cb(stage,apps_event,LV_EVENT_CLICKED,this);
    pet_large_image_ = lv_image_create(stage); lv_image_set_src(pet_large_image_,&pet_descriptor_);
    lv_image_set_pivot(pet_large_image_,0,0); lv_image_set_scale(pet_large_image_,1536);
    lv_image_set_antialias(pet_large_image_,false); lv_obj_remove_flag(pet_large_image_,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(pet_large_image_,portrait ? (width - 144) / 2 : 12,0);
    pet_mood_ = style::label(stage,"",&lv_font_montserrat_14,true);
    lv_obj_set_pos(pet_mood_,8,portrait ? 154 : 150);
    lv_obj_set_size(pet_mood_,(portrait ? width : 168) - 16,18);
    lv_label_set_long_mode(pet_mood_,LV_LABEL_LONG_DOT); lv_obj_set_style_text_align(pet_mood_,LV_TEXT_ALIGN_CENTER,0);
    const int stat_top = portrait ? 232 : 48;
    for (unsigned i = 0; i < 3; ++i) {
        pet_stat_labels_[i] = style::label(content_,"",&lv_font_montserrat_14);
        lv_obj_set_pos(pet_stat_labels_[i],right,stat_top + i * 32);
        pet_stat_bars_[i] = lv_bar_create(content_); lv_obj_set_size(pet_stat_bars_[i],controls_w,6);
        lv_obj_set_pos(pet_stat_bars_[i],right,stat_top + i * 32 + 21);
        lv_obj_set_style_bg_color(pet_stat_bars_[i],style::control,LV_PART_MAIN);
        const uint32_t colors[] = {0xdc5d90,0xea9a28,0x0875db};
        lv_obj_set_style_bg_color(pet_stat_bars_[i],lv_color_hex(colors[i]),LV_PART_INDICATOR);
    }
    pet_pats_ = style::label(portrait ? stage : content_,"",&lv_font_montserrat_14,true);
    lv_obj_set_pos(pet_pats_,portrait ? 8 : right,portrait ? 8 : 148);
    lv_obj_set_width(pet_pats_,portrait ? 68 : controls_w); lv_label_set_long_mode(pet_pats_,LV_LABEL_LONG_DOT);
    const int cell_w = (controls_w - 12) / 3;
    app_button(content_,"Feed",AppControl::PetFeed,right,height - 40,cell_w,40);
    app_button(content_,"Play",AppControl::PetPlay,right + cell_w + 6,height - 40,cell_w,40);
    pet_rest_ = app_button(content_,"Nap",AppControl::PetRest,right + 2 * (cell_w + 6),height - 40,cell_w,40);
    update_pet();
}
void UiManager::update_pet() {
    if (!pet_mood_) return;
    style::set_text(pet_mood_,pet_.reacting(now_ms()) ? "Purr purr!" : pet_.mood(local_));
    const unsigned values[] = {local_.happiness,local_.fullness,local_.energy};
    const char* names[] = {"Happy","Food","Energy"};
    char text[64];
    for (unsigned i = 0; i < 3; ++i) {
        std::snprintf(text,sizeof(text),"%s  %u%%",names[i],values[i]);
        style::set_text(pet_stat_labels_[i],text); lv_bar_set_value(pet_stat_bars_[i],values[i],LV_ANIM_OFF);
    }
    std::snprintf(text,sizeof(text),"%lu pats",static_cast<unsigned long>(local_.pats));
    style::set_text(pet_pats_,text);
    style::set_text(lv_obj_get_child(pet_rest_,0),local_.pet_sleeping ? "Wake" : "Nap");
    style::set_text(lv_obj_get_child(pet_visible_,0),local_.pet_visible ? "Follow On" : "Follow Off");
}
}
