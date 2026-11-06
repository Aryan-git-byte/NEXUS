#include "ui_manager.hpp"
#include "ui_styles.hpp"

namespace nexus {
void UiManager::build_media() {
    const bool portrait = lcd_.orientation() == Orientation::Portrait;
    media_title_ = style::label(content_,"No active media session",&lv_font_montserrat_20);
    lv_obj_set_height(media_title_,lv_font_montserrat_20.line_height);
    lv_obj_set_width(media_title_,lcd_.width() - 24); lv_label_set_long_mode(media_title_,LV_LABEL_LONG_DOT);
    lv_obj_set_pos(media_title_,0,0);
    media_artist_ = style::label(content_,"Connect your PC to control media",&lv_font_montserrat_14,true);
    lv_obj_set_height(media_artist_,lv_font_montserrat_14.line_height);
    lv_obj_set_width(media_artist_,lcd_.width() - 24); lv_label_set_long_mode(media_artist_,LV_LABEL_LONG_DOT);
    lv_obj_set_pos(media_artist_,0,28);
    const int width = lcd_.width() - 24, height = lcd_.height() - 98;
    build_grid(profile_for(Category::Media),48,height - 92,portrait ? 2 : 3,portrait ? 3 : 2,false);
    auto* volume = style::card(content_);
    lv_obj_set_size(volume,width,36); lv_obj_set_pos(volume,0,height - 36);
    lv_obj_set_style_pad_all(volume,8,0);
    volume_slider_ = lv_slider_create(volume);
    lv_obj_set_size(volume_slider_,width - 94,8);
    lv_obj_align(volume_slider_,LV_ALIGN_LEFT_MID,4,0);
    lv_slider_set_range(volume_slider_,0,100);
    lv_obj_set_style_bg_color(volume_slider_,style::control,LV_PART_MAIN);
    lv_obj_set_style_bg_color(volume_slider_,style::accent,LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(volume_slider_,style::accent,LV_PART_KNOB);
    lv_obj_set_style_pad_all(volume_slider_,6,LV_PART_KNOB);
    lv_obj_add_event_cb(volume_slider_,volume_event,LV_EVENT_RELEASED,this);
    volume_label_ = style::label(volume,"--",&lv_font_montserrat_14);
    lv_obj_set_width(volume_label_,64); lv_label_set_long_mode(volume_label_,LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(volume_label_,LV_TEXT_ALIGN_RIGHT,0);
    lv_obj_align(volume_label_,LV_ALIGN_RIGHT_MID,0,0);
    lv_obj_add_state(volume_slider_,LV_STATE_DISABLED);
}
}
