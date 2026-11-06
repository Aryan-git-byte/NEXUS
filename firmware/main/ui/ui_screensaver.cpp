#include "ui_manager.hpp"
#include "ui_styles.hpp"
#include "assets/clock_fonts.h"

namespace nexus {
void UiManager::build_screensaver() {
    const bool portrait = lcd_.width() < lcd_.height();
    lv_obj_set_style_bg_color(root_,lv_color_black(),0);
    lv_obj_set_style_bg_opa(root_,LV_OPA_COVER,0);

    auto white_label = [&](const char* value,const lv_font_t* font) {
        auto* label = style::label(root_,value,font);
        lv_obj_set_width(label,lcd_.width() - 40);
        lv_obj_set_height(label,font->line_height);
        lv_obj_set_style_text_color(label,lv_color_white(),0);
        lv_obj_set_style_text_opa(label,LV_OPA_COVER,0);
        lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
        lv_label_set_long_mode(label,LV_LABEL_LONG_DOT);
        return label;
    };
    day_ = white_label("CLOCK NOT SYNCED",&nexus_bold_22);
    lv_obj_align(day_,LV_ALIGN_CENTER,0,-118);
    clock_ = white_label("--:--",portrait ? &nexus_clock_80 : &nexus_clock_112);
    lv_label_set_long_mode(clock_,LV_LABEL_LONG_CLIP);
    lv_obj_align(clock_,LV_ALIGN_CENTER,0,portrait ? -24 : -26);
    date_ = white_label("Connect your computer",&nexus_bold_22);
    lv_obj_align(date_,LV_ALIGN_CENTER,0,66);
    pc_name_ = white_label("No computer connected",&nexus_bold_22);
    lv_obj_align(pc_name_,LV_ALIGN_BOTTOM_MID,0,portrait ? -54 : -26);

    lv_obj_add_flag(root_,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(root_,wake_event,LV_EVENT_CLICKED,this);
}
}
