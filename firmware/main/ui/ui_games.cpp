#include "ui_manager.hpp"
#include "ui_styles.hpp"
#include <algorithm>
#include <cstdio>

namespace nexus {
namespace {
uint16_t rgb565(uint32_t rgb) {
    return ((rgb >> 8) & 0xf800) | ((rgb >> 5) & 0x07e0) | ((rgb >> 3) & 0x001f);
}
}
void UiManager::build_2048() {
    const bool portrait = lcd_.width() < lcd_.height();
    const int width = lcd_.width() - 24;
    const int size = portrait ? 216 : 184, x = portrait ? (width - size) / 2 : 0;
    auto* title = style::label(content_,"2048",&lv_font_montserrat_20);
    lv_obj_set_pos(title,0,0);
    game_score_ = style::label(content_,"",&lv_font_montserrat_14,true);
    lv_obj_set_width(game_score_,width - 72); lv_obj_set_height(game_score_,18);
    lv_label_set_long_mode(game_score_,LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(game_score_,LV_TEXT_ALIGN_RIGHT,0);
    lv_obj_align(game_score_,LV_ALIGN_TOP_RIGHT,0,4);
    auto* board = style::card(content_); lv_obj_set_style_pad_all(board,0,0);
    lv_obj_set_style_bg_color(board,style::control,0);
    lv_obj_set_pos(board,x,portrait ? 30 : 32); lv_obj_set_size(board,size,size);
    const int cell = (size - 24) / 4;
    for (int i = 0; i < 16; ++i) {
        game_cells_[i] = style::card(board); lv_obj_set_style_pad_all(game_cells_[i],0,0);
        lv_obj_set_style_border_width(game_cells_[i],0,0); lv_obj_set_style_radius(game_cells_[i],8,0);
        lv_obj_set_pos(game_cells_[i],6 + i % 4 * (cell + 4),6 + i / 4 * (cell + 4));
        lv_obj_set_size(game_cells_[i],cell,cell);
        game_labels_[i] = style::label(game_cells_[i],"",&lv_font_montserrat_20);
        lv_obj_set_width(game_labels_[i],cell - 2); lv_label_set_long_mode(game_labels_[i],LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(game_labels_[i],LV_TEXT_ALIGN_CENTER,0);
        lv_obj_center(game_labels_[i]);
    }
    const int pad_x = portrait ? 0 : size + 12;
    const int pad_w = portrait ? width : width - pad_x, top = portrait ? 250 : 40;
    const int cell_w = (pad_w - 12) / 3, pitch = cell_w + 6;
    app_button(content_,LV_SYMBOL_UP,AppControl::GameUp,pad_x + pitch,top,cell_w,40);
    app_button(content_,LV_SYMBOL_LEFT,AppControl::GameLeft,pad_x,top + 46,cell_w,40);
    app_button(content_,LV_SYMBOL_RIGHT,AppControl::GameRight,pad_x + 2 * pitch,top + 46,cell_w,40);
    app_button(content_,LV_SYMBOL_DOWN,AppControl::GameDown,pad_x + pitch,top + 92,cell_w,40);
    game_note_ = style::label(content_,"Tap\narrows",&lv_font_montserrat_14,true);
    lv_obj_set_pos(game_note_,pad_x + pitch,top + 46);
    lv_obj_set_size(game_note_,cell_w,40); lv_label_set_long_mode(game_note_,LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(game_note_,LV_TEXT_ALIGN_CENTER,0);
    const int bottom = portrait ? top + 92 : 178;
    const int action_w = portrait ? cell_w : (pad_w - 6) / 2;
    app_button(content_,"New",AppControl::GameNew,pad_x,bottom,action_w,40);
    app_button(content_,"Undo",AppControl::GameUndo,portrait ? pad_x + 2 * pitch : pad_x + action_w + 6,bottom,action_w,40);
    update_2048();
}
void UiManager::update_2048() {
    if (!game_cells_[0]) return;
    static const uint32_t colors[] = {0xffffff,0xffefd3,0xffdfb4,0xffc37d,0xf39446,
        0xe86b42,0xdb4775,0x9b62da,0x397ac9,0x008b9a,0x168c64,0x9955c3};
    for (unsigned i = 0; i < 16; ++i) {
        const uint8_t exponent = game2048_.tiles()[i];
        const uint32_t color = exponent < 12 ? colors[exponent] : colors[5 + exponent % 7];
        lv_obj_set_style_bg_color(game_cells_[i],lv_color_hex(color),0);
        lv_obj_set_style_text_color(game_labels_[i],exponent >= 4 ? lv_color_white() : style::foreground,0);
        char text[16]{};
        if (exponent >= 14 && exponent <= 17)
            std::snprintf(text,sizeof(text),"%luK",static_cast<unsigned long>(uint32_t{1} << (exponent - 10)));
        else if (exponent > 17) std::snprintf(text,sizeof(text),"2^%u",exponent);
        else if (exponent) std::snprintf(text,sizeof(text),"%lu",static_cast<unsigned long>(uint32_t{1} << exponent));
        const lv_font_t* font = exponent >= 10 ? &lv_font_montserrat_14 : &lv_font_montserrat_20;
        lv_obj_set_style_text_font(game_labels_[i],font,0);
        style::set_text(game_labels_[i],text); lv_obj_center(game_labels_[i]);
    }
    char text[80]; std::snprintf(text,sizeof(text),"Score %lu  |  Best %lu",
        static_cast<unsigned long>(game2048_.score()),static_cast<unsigned long>(local_.best_2048));
    style::set_text(game_score_,text);
    style::set_text(game_note_,game2048_.can_move() ? "Tap\narrows" : "Game\nover");
}

void UiManager::build_snake() {
    const bool portrait = lcd_.width() < lcd_.height();
    const int width = lcd_.width() - 24;
    auto* title = style::label(content_,"Snake",&lv_font_montserrat_20); lv_obj_set_pos(title,0,0);
    game_score_ = style::label(content_,"",&lv_font_montserrat_14,true);
    lv_obj_set_width(game_score_,width - 82); lv_obj_set_height(game_score_,18);
    lv_label_set_long_mode(game_score_,LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(game_score_,LV_TEXT_ALIGN_RIGHT,0); lv_obj_align(game_score_,LV_ALIGN_TOP_RIGHT,0,4);
    const int board_w = portrait ? 250 : 210, board_h = portrait ? 202 : 170;
    auto* board = style::card(content_); lv_obj_set_style_pad_all(board,4,0);
    lv_obj_set_pos(board,portrait ? (width - board_w) / 2 : 0,portrait ? 46 : 48);
    lv_obj_set_size(board,board_w,board_h);
    snake_descriptor_.header.magic = LV_IMAGE_HEADER_MAGIC;
    snake_descriptor_.header.cf = LV_COLOR_FORMAT_RGB565;
    snake_descriptor_.header.w = 80; snake_descriptor_.header.h = 64;
    snake_descriptor_.header.stride = 160;
    snake_descriptor_.data_size = snake_pixels_.size() * sizeof(uint16_t);
    snake_descriptor_.data = reinterpret_cast<const uint8_t*>(snake_pixels_.data());
    snake_image_ = lv_image_create(board);
    lv_image_set_src(snake_image_,&snake_descriptor_); lv_image_set_pivot(snake_image_,0,0);
    lv_image_set_scale(snake_image_,portrait ? 768 : 640);
    lv_image_set_antialias(snake_image_,false); lv_obj_set_pos(snake_image_,0,0);
    lv_obj_remove_flag(snake_image_,LV_OBJ_FLAG_CLICKABLE);
    game_note_ = style::label(content_,"",&lv_font_montserrat_14,true);
    lv_obj_set_pos(game_note_,0,26); lv_obj_set_width(game_note_,width);
    lv_obj_set_height(game_note_,18); lv_label_set_long_mode(game_note_,LV_LABEL_LONG_DOT);
    const int pad_x = portrait ? 0 : board_w + 16;
    const int pad_w = width - pad_x, top = portrait ? 250 : 40;
    const int cell_w = (pad_w - 12) / 3, pitch = cell_w + 6;
    app_button(content_,LV_SYMBOL_UP,AppControl::SnakeUp,pad_x + pitch,top,cell_w,40);
    app_button(content_,LV_SYMBOL_LEFT,AppControl::SnakeLeft,pad_x,top + 46,cell_w,40);
    app_button(content_,LV_SYMBOL_RIGHT,AppControl::SnakeRight,pad_x + 2 * pitch,top + 46,cell_w,40);
    app_button(content_,LV_SYMBOL_DOWN,AppControl::SnakeDown,pad_x + pitch,top + 92,cell_w,40);
    snake_speed_ = app_button(content_,"Normal",AppControl::SnakeSpeed,pad_x + pitch,top + 46,cell_w,40);
    lv_obj_set_style_bg_color(snake_speed_,lv_color_hex(0xe0efff),0);
    const int bottom = portrait ? top + 92 : 178, action_w = portrait ? cell_w : (pad_w - 6) / 2;
    app_button(content_,"New",AppControl::SnakeNew,pad_x,bottom,action_w,40);
    snake_start_ = app_button(content_,"Start",AppControl::SnakeStart,
        portrait ? pad_x + 2 * pitch : pad_x + action_w + 6,bottom,action_w,40);
    update_snake();
}
void UiManager::update_snake() {
    if (!snake_image_) return;
    snake_pixels_.fill(rgb565(0xf3f7fc));
    auto cell = [&](uint16_t point, uint32_t color) {
        const int left = point % apps::Snake::width * 4, top = point / apps::Snake::width * 4;
        for (int y = top; y < top + 4; ++y) for (int x = left; x < left + 4; ++x)
            snake_pixels_[y * 80 + x] = rgb565(color);
    };
    for (uint16_t point = 0; point < apps::Snake::capacity; ++point)
        if (snake_.occupies(point)) cell(point,point == snake_.head() ? 0x0057b8 : 0x54aaff);
    if (!snake_.won()) cell(snake_.food(),0xef477b);
    const int head_x = snake_.head() % apps::Snake::width * 4, head_y = snake_.head() / apps::Snake::width * 4;
    snake_pixels_[(head_y + 1) * 80 + head_x + 1] = rgb565(0xffffff);
    lv_image_cache_drop(&snake_descriptor_); lv_obj_invalidate(snake_image_);
    char text[80]; std::snprintf(text,sizeof(text),"Score %lu  |  Best %lu",
        static_cast<unsigned long>(snake_.score()),static_cast<unsigned long>(local_.best_snake));
    style::set_text(game_score_,text);
    style::set_text(game_note_,snake_.won() ? "You filled the board!" : snake_.over() ? "Game over. Tap New or Start." :
                    snake_running_ ? "Tap arrows. Tap Pause to take a break." : "Paused. Tap Start when you're ready.");
    style::set_text(lv_obj_get_child(snake_start_,0),snake_running_ ? "Pause" : snake_.over() ? "Again" : "Start");
    const char* speeds[] = {"Slow","Normal","Fast"};
    style::set_text(lv_obj_get_child(snake_speed_,0),speeds[local_.snake_speed]);
}
}
