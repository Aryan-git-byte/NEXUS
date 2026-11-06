#include "ui_styles.hpp"
#include <cstring>
#include <initializer_list>

namespace nexus::style {
lv_style_t screen, surface, button, pressed, selected, text, secondary;
lv_color_t background, foreground, muted, accent, success, warning, danger, border, control;
namespace {
bool initialized = false;
lv_style_transition_dsc_t transition;
const lv_style_prop_t transition_properties[] = {LV_STYLE_BG_COLOR, LV_STYLE_BORDER_COLOR, LV_STYLE_TRANSLATE_Y, 0};
}
void initialize(uint8_t theme) {
    if (initialized) for (auto* s : {&screen,&surface,&button,&pressed,&selected,&text,&secondary}) lv_style_reset(s);
    else for (auto* s : {&screen,&surface,&button,&pressed,&selected,&text,&secondary}) lv_style_init(s);
    initialized = true;
    background = lv_color_hex(theme ? 0xfff4f0 : 0xf3f5fb);
    foreground = lv_color_hex(0x20202a); muted = lv_color_hex(0x586174); accent = lv_color_hex(0x0067d8);
    success = lv_color_hex(0x15803d); warning = lv_color_hex(0x985000); danger = lv_color_hex(0xd9283e);
    border = lv_color_hex(0xcbd2e0); control = lv_color_hex(0xe5eaf3);
    lv_style_set_bg_color(&screen,background); lv_style_set_bg_opa(&screen,LV_OPA_COVER);
    lv_style_set_border_width(&screen,0); lv_style_set_pad_all(&screen,0);
    lv_style_set_text_font(&screen,&lv_font_montserrat_14); lv_style_set_text_color(&screen,foreground);
    lv_style_set_bg_color(&surface,lv_color_hex(0xffffff));
    lv_style_set_bg_opa(&surface,LV_OPA_COVER); lv_style_set_radius(&surface,12);
    lv_style_set_border_color(&surface,border); lv_style_set_border_width(&surface,1);
    lv_style_set_pad_all(&surface,10);
    lv_style_set_text_color(&surface,foreground);
    lv_style_set_text_font(&surface,&lv_font_montserrat_14);
    lv_style_set_bg_color(&button,lv_color_hex(0xffffff));
    lv_style_set_bg_opa(&button,LV_OPA_COVER); lv_style_set_radius(&button,12);
    lv_style_set_border_color(&button,border); lv_style_set_border_width(&button,1);
    lv_style_set_shadow_width(&button,0); lv_style_set_pad_all(&button,6);
    lv_style_set_text_color(&button,foreground);
    lv_style_transition_dsc_init(&transition,transition_properties,lv_anim_path_ease_out,90,0,nullptr);
    lv_style_set_transition(&button,&transition);
    lv_style_set_bg_color(&pressed,lv_color_hex(0xd4e6ff)); lv_style_set_border_color(&pressed,accent);
    lv_style_set_translate_y(&pressed,1); lv_style_set_transition(&pressed,&transition);
    lv_style_set_bg_color(&selected,lv_color_hex(0xdcecff)); lv_style_set_border_color(&selected,accent);
    lv_style_set_text_color(&text,foreground); lv_style_set_text_font(&text,&lv_font_montserrat_14);
    lv_style_set_text_color(&secondary,muted); lv_style_set_text_font(&secondary,&lv_font_montserrat_14);
}
void apply_dropdown(lv_obj_t* dropdown) {
    lv_obj_remove_style_all(dropdown);
    lv_obj_add_style(dropdown,&surface,LV_PART_MAIN);
    lv_obj_add_style(dropdown,&pressed,LV_STATE_PRESSED);
    lv_obj_add_style(dropdown,&selected,LV_STATE_CHECKED);
    lv_obj_set_style_pad_left(dropdown,12,LV_PART_MAIN);
    lv_obj_set_style_pad_right(dropdown,30,LV_PART_MAIN);
    lv_obj_set_style_text_color(dropdown,foreground,LV_PART_MAIN);
    lv_obj_set_style_text_font(dropdown,&lv_font_montserrat_14,LV_PART_MAIN);
    lv_obj_set_style_text_opa(dropdown,LV_OPA_COVER,LV_PART_MAIN);
    lv_obj_set_style_text_color(dropdown,foreground,LV_PART_INDICATOR);
    lv_obj_set_style_text_font(dropdown,&lv_font_montserrat_14,LV_PART_INDICATOR);
    lv_obj_set_style_text_opa(dropdown,LV_OPA_COVER,LV_PART_INDICATOR);
    lv_dropdown_set_symbol(dropdown,LV_SYMBOL_DOWN);
    auto* list = lv_dropdown_get_list(dropdown);
    if (!list) return;
    lv_obj_remove_style_all(list);
    lv_obj_add_style(list,&surface,LV_PART_MAIN);
    lv_obj_set_style_text_color(list,foreground,LV_PART_MAIN);
    lv_obj_set_style_text_font(list,&lv_font_montserrat_14,LV_PART_MAIN);
    lv_obj_set_style_text_line_space(list,10,LV_PART_MAIN);
    lv_obj_set_style_bg_color(list,accent,LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(list,LV_OPA_COVER,LV_PART_SELECTED);
    lv_obj_set_style_text_color(list,lv_color_white(),LV_PART_SELECTED);
    const auto checked = static_cast<lv_style_selector_t>(LV_PART_SELECTED) | LV_STATE_CHECKED;
    const auto pressed_state = static_cast<lv_style_selector_t>(LV_PART_SELECTED) | LV_STATE_PRESSED;
    lv_obj_set_style_bg_color(list,accent,checked);
    lv_obj_set_style_text_color(list,lv_color_white(),checked);
    lv_obj_set_style_bg_color(list,accent,pressed_state);
    lv_obj_set_style_text_color(list,lv_color_white(),pressed_state);
    lv_obj_set_style_max_height(list,190,LV_PART_MAIN);
    lv_obj_set_style_bg_color(list,muted,LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(list,LV_OPA_40,LV_PART_SCROLLBAR);
    auto* text = lv_obj_get_child(list,0);
    if (text) {
        lv_obj_remove_style_all(text);
        lv_obj_set_style_text_color(text,foreground,LV_PART_MAIN);
        lv_obj_set_style_text_font(text,&lv_font_montserrat_14,LV_PART_MAIN);
        lv_obj_set_style_text_line_space(text,10,LV_PART_MAIN);
    }
}
lv_color_t tile_color(const char* name) {
    struct Tone { const char* name; uint32_t color; };
    static const Tone colors[] = {
        {"code",0xe1efff},{"terminal",0xe7ebf2},{"powershell",0xe0eaff},{"folder",0xffefc9},
        {"github",0xefe5ff},{"project",0xe1efff},{"server",0xddf5e7},{"camera",0xffe2df},
        {"play",0xffe1ed},{"next",0xe5e7ff},{"previous",0xe5e7ff},{"volume_up",0xe0f5e8},
        {"volume_down",0xdff2f7},{"mute",0xffe7d4},{"copy",0xe0f1ff},{"paste",0xe1f4e6},
        {"cut",0xe1f4f6},{"undo",0xeee5ff},{"save",0xe2ecff},{"desktop",0xe0f0ff},
        {"lock",0xffefca},{"browser",0xe0f3ff},{"text",0xeee5ff},{"game",0xede3ff},
        {"mail",0xffe8d5},{"calendar",0xffe2e9},{"search",0xe0f1ff},{"star",0xffefc9}};
    for (const auto& item : colors) if (std::strcmp(name,item.name) == 0) return lv_color_hex(item.color);
    return lv_color_hex(0xe7ecff);
}
lv_obj_t* label(lv_obj_t* parent, const char* value, const lv_font_t* font, bool subdued) {
    auto* result = lv_label_create(parent);
    lv_obj_add_style(result,subdued ? &secondary : &text,0);
    lv_obj_set_style_text_font(result,font,0); lv_label_set_text(result,value);
    lv_obj_remove_flag(result,LV_OBJ_FLAG_CLICKABLE);
    return result;
}
lv_obj_t* card(lv_obj_t* parent) {
    auto* result = lv_obj_create(parent); lv_obj_remove_style_all(result); lv_obj_add_style(result,&surface,0);
    lv_obj_remove_flag(result,LV_OBJ_FLAG_SCROLLABLE); return result;
}
lv_obj_t* action_button(lv_obj_t* parent, const char* value) {
    auto* result = lv_button_create(parent); lv_obj_remove_style_all(result);
    lv_obj_add_style(result,&button,0); lv_obj_add_style(result,&pressed,LV_STATE_PRESSED);
    lv_obj_remove_flag(result,LV_OBJ_FLAG_SCROLLABLE);
    auto* text = label(result,value); lv_obj_center(text); return result;
}
const char* icon(const char* name) {
    struct Icon { const char* name; const char* glyph; };
    static const Icon icons[] = {
        {"code","<>"},{"terminal",">_"},{"powershell","PS"},{"folder",LV_SYMBOL_DIRECTORY},
        {"github","GH"},{"project",LV_SYMBOL_FILE},{"server",LV_SYMBOL_DRIVE},{"camera",LV_SYMBOL_IMAGE},
        {"play",LV_SYMBOL_PLAY},{"next",LV_SYMBOL_NEXT},{"previous",LV_SYMBOL_PREV},
        {"volume_up",LV_SYMBOL_VOLUME_MAX},{"volume_down",LV_SYMBOL_VOLUME_MID},{"mute",LV_SYMBOL_MUTE},
        {"copy",LV_SYMBOL_COPY},{"paste",LV_SYMBOL_PASTE},{"cut",LV_SYMBOL_CUT},{"undo",LV_SYMBOL_REFRESH},
        {"save",LV_SYMBOL_SAVE},{"desktop",LV_SYMBOL_VIDEO},{"lock",LV_SYMBOL_EYE_CLOSE},
        {"browser",LV_SYMBOL_GPS},{"text","Aa"},{"game","+"},{"mail",LV_SYMBOL_ENVELOPE},
        {"calendar",LV_SYMBOL_LIST},{"search",LV_SYMBOL_EYE_OPEN},{"star",LV_SYMBOL_OK},{"custom",LV_SYMBOL_PLUS}};
    for (const auto& item : icons) if (std::strcmp(name,item.name) == 0) return item.glyph;
    return LV_SYMBOL_PLUS;
}
void set_text(lv_obj_t* target, const char* value) {
    if (target && std::strcmp(lv_label_get_text(target),value) != 0) lv_label_set_text(target,value);
}
}
