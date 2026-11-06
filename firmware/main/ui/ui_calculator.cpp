#include "ui_manager.hpp"
#include "ui_styles.hpp"
#include <cstring>

namespace nexus {
void UiManager::build_calculator() {
    const bool portrait = lcd_.width() < lcd_.height();
    const int width = lcd_.width() - 24, height = lcd_.height() - 98;
    auto* title = style::label(content_,"Calculator",&lv_font_montserrat_16);
    lv_obj_set_pos(title,0,0);
    calc_expression_ = style::label(content_,"",&lv_font_montserrat_14,true);
    lv_obj_set_width(calc_expression_,width - 120); lv_obj_set_height(calc_expression_,18);
    lv_label_set_long_mode(calc_expression_,LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(calc_expression_,LV_TEXT_ALIGN_RIGHT,0);
    lv_obj_align(calc_expression_,LV_ALIGN_TOP_RIGHT,0,0);
    auto* display = style::card(content_);
    lv_obj_set_pos(display,0,portrait ? 24 : 20); lv_obj_set_size(display,width,portrait ? 58 : 32);
    lv_obj_set_style_pad_all(display,portrait ? 4 : 2,0);
    calc_value_ = style::label(display,"0",portrait ? &lv_font_montserrat_28 : &lv_font_montserrat_20);
    lv_obj_set_width(calc_value_,width - 16); lv_label_set_long_mode(calc_value_,LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(calc_value_,LV_TEXT_ALIGN_RIGHT,0); lv_obj_align(calc_value_,LV_ALIGN_RIGHT_MID,0,0);
    const unsigned landscape[] = {4,5,6,0,1,8,9,10,3,7,12,13,14,11,15,16,17,18,2,19};
    const int columns = portrait ? 4 : 5, rows = portrait ? 5 : 4;
    const int top = portrait ? 90 : 58, gap = 4;
    const int cell_w = (width - (columns - 1) * gap) / columns;
    const int cell_h = (height - top - (rows - 1) * gap) / rows;
    for (unsigned i = 0; i < 20; ++i) {
        const unsigned key = portrait ? i : landscape[i];
        auto* button = app_button(content_,key == 7 ? "x" : calculator_keys[key],
                                  static_cast<AppControl>(100 + key),
                                  i % columns * (cell_w + gap),top + i / columns * (cell_h + gap),cell_w,cell_h);
        auto* label = lv_obj_get_child(button,0);
        lv_obj_set_style_text_font(label,portrait ? &lv_font_montserrat_28 : &lv_font_montserrat_20,0);
        if (key == 19) {
            lv_obj_set_style_bg_color(button,style::accent,0); lv_obj_set_style_text_color(label,lv_color_white(),0);
        } else if (key == 3 || key == 7 || key == 11 || key == 15)
            lv_obj_set_style_bg_color(button,lv_color_hex(0xffe8c5),0);
        else if (key < 3) lv_obj_set_style_bg_color(button,style::control,0);
        lv_obj_center(label);
    }
    update_calculator();
}
void UiManager::update_calculator() {
    if (calc_value_) lv_obj_set_style_text_font(calc_value_,
        lcd_.width() < lcd_.height() && std::strlen(calculator_.display()) <= 12 ?
        &lv_font_montserrat_28 : &lv_font_montserrat_20,0);
    style::set_text(calc_value_,calculator_.display());
    style::set_text(calc_expression_,calculator_.expression());
}
}
