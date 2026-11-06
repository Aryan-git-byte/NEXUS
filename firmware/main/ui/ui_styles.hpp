#pragma once
#include "lvgl.h"

namespace nexus::style {
extern lv_style_t screen, surface, button, pressed, selected, text, secondary;
extern lv_color_t background, foreground, muted, accent, success, warning, danger, border, control;
void initialize(uint8_t theme);
void apply_dropdown(lv_obj_t* dropdown);
lv_obj_t* label(lv_obj_t* parent, const char* value, const lv_font_t* font = &lv_font_montserrat_14,
                bool subdued = false);
lv_obj_t* card(lv_obj_t* parent);
lv_obj_t* action_button(lv_obj_t* parent, const char* value);
const char* icon(const char* name);
void set_text(lv_obj_t* label, const char* text);
lv_color_t tile_color(const char* icon);
}
