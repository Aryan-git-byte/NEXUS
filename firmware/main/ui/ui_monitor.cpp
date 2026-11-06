#include "ui_manager.hpp"
#include "ui_styles.hpp"

namespace nexus {
void UiManager::build_monitor() {
    bool portrait = lcd_.orientation() == Orientation::Portrait;
    const char* names[] = {"CPU", "RAM", "GPU", "Disk", "Battery", "CPU temp", "GPU temp", "Upload", "Download"};
    const int columns = portrait ? 2 : 3, width = (lcd_.width() - 24 - (columns - 1) * 8) / columns;
    int height = portrait ? 48 : 42;
    for (int index = 0; index < 9; ++index) {
        auto* card = style::card(content_);
        lv_obj_set_size(card,width,height);
        lv_obj_set_pos(card,(index % columns) * (width + 8),(index / columns) * (height + 6));
        lv_obj_set_style_pad_all(card,6,0);
        auto* name = style::label(card,names[index],&lv_font_montserrat_14,true);
        lv_obj_align(name,LV_ALIGN_TOP_LEFT,0,0);
        metric_labels_[index] = style::label(card,"--");
        if (index >= 5) {
            lv_obj_set_style_pad_all(card,portrait ? 6 : 4,0);
            lv_obj_align(metric_labels_[index],LV_ALIGN_BOTTOM_RIGHT,0,0);
        } else lv_obj_align(metric_labels_[index],LV_ALIGN_TOP_RIGHT,0,0);
        if (index < 5) {
            metric_bars_[index] = lv_bar_create(card);
            lv_obj_set_size(metric_bars_[index],width - 14,4);
            lv_obj_align(metric_bars_[index],LV_ALIGN_BOTTOM_MID,0,0);
            lv_obj_set_style_bg_color(metric_bars_[index],style::control,LV_PART_MAIN);
            lv_obj_set_style_bg_color(metric_bars_[index],index == 1 ? style::success : style::accent,LV_PART_INDICATOR);
        }
    }
    chart_ = lv_chart_create(content_);
    lv_obj_set_size(chart_,lcd_.width() - 24,portrait ? 84 : 56);
    lv_obj_set_pos(chart_,0,portrait ? 274 : 144);
    lv_obj_add_style(chart_,&style::surface,0);
    lv_obj_set_style_pad_all(chart_,6,0);
    lv_obj_set_style_line_width(chart_,2,LV_PART_ITEMS);
    lv_obj_set_style_size(chart_,0,0,LV_PART_INDICATOR);
    lv_chart_set_type(chart_,LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart_,60);
    lv_chart_set_range(chart_,LV_CHART_AXIS_PRIMARY_Y,0,100);
    lv_chart_set_div_line_count(chart_,3,0);
    cpu_series_ = lv_chart_add_series(chart_,style::accent,LV_CHART_AXIS_PRIMARY_Y);
    ram_series_ = lv_chart_add_series(chart_,style::success,LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(chart_,cpu_series_,LV_CHART_POINT_NONE);
    lv_chart_set_all_value(chart_,ram_series_,LV_CHART_POINT_NONE);
    auto* legend = style::label(content_,"CPU / RAM    60 samples",&lv_font_montserrat_14,true);
    lv_obj_set_pos(legend,0,portrait ? 364 : 204);
}
}
