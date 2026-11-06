#include "ui_manager.hpp"
#include "ui_styles.hpp"
#include "ui_icon.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace nexus {
void UiManager::build_grid(uint8_t profile_index, int top, int height, int columns, int rows, bool footer) {
    const auto& profile = profiles_->profiles[profile_index];
    const int per_page = std::min(12,columns * rows);
    grid_pages_ = std::max(1,(profile.count + per_page - 1) / per_page);
    if (grid_page_ >= grid_pages_) grid_page_ = 0;
    int width = lcd_.width() - 24, gap = 8;
    int cell_width = (width - gap * (columns - 1)) / columns;
    const bool show_footer = footer || grid_pages_ > 1;
    int cell_height = (height - (show_footer ? 32 : 0) - gap * (rows - 1)) / rows;
    const bool compact = cell_height < 66;
    button_count_ = 0;
    const int first = grid_page_ * per_page;
    for (int i = first; i < std::min<int>(profile.count,first + per_page); ++i) {
        ButtonView& view = buttons_[button_count_++];
        view.manager = this; view.profile = profile_index; view.button = i; view.long_sent = false;
        view.sequence = 0; view.feedback_until = 0;
        const auto& button = profile.buttons[i];
        auto* object = lv_button_create(content_);
        view.object = object;
        lv_obj_remove_style_all(object); lv_obj_add_style(object,&style::button,0);
        lv_obj_add_style(object,&style::pressed,LV_STATE_PRESSED);
        lv_obj_remove_flag(object,LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(object,style::tile_color(button.icon),0);
        lv_obj_set_style_clip_corner(object,true,0);
        if (compact) lv_obj_set_style_pad_all(object,4,0);
        lv_obj_set_size(object,cell_width,cell_height);
        int slot = i - first;
        lv_obj_set_pos(object,(slot % columns) * (cell_width + gap),top + (slot / columns) * (cell_height + gap));
        lv_obj_add_event_cb(object,button_event,LV_EVENT_ALL,&view);
        lv_obj_t* glyph = nullptr;
        if (button.has_icon) {
            for (unsigned pixel = 0; pixel < 576; ++pixel)
                view.image_alpha[pixel] = (button.bitmap[pixel / 8] & (0x80 >> (pixel % 8))) ? 255 : 0;
            view.image = {};
            view.image.header.magic = LV_IMAGE_HEADER_MAGIC; view.image.header.cf = LV_COLOR_FORMAT_A8;
            view.image.header.w = view.image.header.h = 24; view.image.header.stride = 24;
            view.image.data_size = 576; view.image.data = view.image_alpha;
            glyph = lv_image_create(object); lv_image_set_src(glyph,&view.image);
            lv_obj_set_style_image_recolor(glyph,style::foreground,0);
            lv_obj_set_style_image_recolor_opa(glyph,LV_OPA_COVER,0);
        } else {
            glyph = color_icon(object,button,storage_,compact ? 28 : 32);
            if (!glyph) glyph = style::label(object,style::icon(button.icon),&lv_font_montserrat_20);
        }
        auto* label = style::label(object,button.label);
        lv_obj_set_height(label,lv_font_montserrat_14.line_height);
        lv_label_set_long_mode(label,LV_LABEL_LONG_DOT);
        if (compact && cell_width >= 120) {
            // Short landscape media rows place text beside the icon, with no overlap.
            lv_obj_align(glyph,LV_ALIGN_LEFT_MID,4,0);
            lv_obj_set_width(label,cell_width - 54);
            lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_LEFT,0);
            lv_obj_align(label,LV_ALIGN_RIGHT_MID,-2,0);
        } else {
            lv_obj_align(glyph,LV_ALIGN_TOP_MID,0,0);
            lv_obj_set_width(label,cell_width - 16);
            lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
            lv_obj_align(label,LV_ALIGN_BOTTOM_MID,0,-1);
        }
    }
    if (!show_footer) return;
    auto* caption = style::label(content_,profile.name,&lv_font_montserrat_14,true);
    lv_obj_set_width(caption,width - 128); lv_label_set_long_mode(caption,LV_LABEL_LONG_DOT);
    lv_obj_set_pos(caption,0,top + height - 22);
    char pages[24]; std::snprintf(pages,sizeof(pages),"%d / %d",grid_page_ + 1,grid_pages_);
    auto* pagination = style::label(content_,pages,&lv_font_montserrat_14,true);
    lv_obj_set_width(pagination,44); lv_obj_set_pos(pagination,width - 112,top + height - 22);
    for (int direction : {-1,1}) {
        auto* control = style::action_button(content_,direction < 0 ? LV_SYMBOL_LEFT : LV_SYMBOL_RIGHT);
        lv_obj_set_size(control,30,28); lv_obj_set_pos(control,width - (direction < 0 ? 64 : 30),top + height - 28);
        lv_obj_set_user_data(control,reinterpret_cast<void*>(static_cast<intptr_t>(direction)));
        lv_obj_add_event_cb(control,page_event,LV_EVENT_CLICKED,this);
        if (grid_pages_ <= 1) lv_obj_add_state(control,LV_STATE_DISABLED);
    }
}
void UiManager::build_home() {
    bool portrait = lcd_.orientation() == Orientation::Portrait;
    uint8_t profile = profiles_->active;
    int top = 0;
    if (page_ == Page::Home) {
        auto* summary = style::card(content_);
        lv_obj_set_size(summary,lcd_.width() - 24,36); lv_obj_set_pos(summary,0,0);
        lv_obj_set_style_pad_all(summary,8,0);
        home_cpu_ = style::label(summary,"CPU --"); lv_obj_align(home_cpu_,LV_ALIGN_LEFT_MID,0,0);
        home_ram_ = style::label(summary,"RAM --"); lv_obj_align(home_ram_,LV_ALIGN_CENTER,0,0);
        home_battery_ = style::label(summary,"BAT --"); lv_obj_align(home_battery_,LV_ALIGN_RIGHT_MID,0,0);
        top = 44;
    } else {
        const auto category = page_ == Page::Development ? Category::Development : Category::Custom;
        profile = profile_for(category);
        auto* title = style::label(content_,page_ == Page::Development ? "Development" : "Custom macros",&lv_font_montserrat_20);
        lv_obj_set_pos(title,0,0); top = 30;
    }
    build_grid(profile,top,lcd_.height() - 98 - top,portrait ? 3 : 4,portrait ? 4 : 2);
}
}
