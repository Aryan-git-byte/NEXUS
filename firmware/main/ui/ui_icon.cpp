#include "ui_icon.hpp"
#include "assets/icon_data.hpp"
#include "assets/generated_icons.hpp"
#include <cstdlib>
#include <cstring>
#include <new>

namespace nexus {
namespace {
struct IconView {
    icons::IconData asset{};
    lv_image_dsc_t image{};
    lv_obj_t* object = nullptr;
    lv_timer_t* timer = nullptr;
    uint8_t* owned = nullptr;
    unsigned frame = 0;
    alignas(4) uint8_t pixels[icons::kPixels * 3]{};
};
void advance_icon(lv_timer_t* timer) {
    auto* view = static_cast<IconView*>(lv_timer_get_user_data(timer));
    view->frame = (view->frame + 1) % view->asset.frames;
    view->asset.decode(view->frame,view->pixels);
    lv_image_cache_drop(&view->image);
    lv_obj_invalidate(view->object);
    lv_timer_set_period(timer,view->asset.duration(view->frame));
}
void delete_icon(lv_event_t* event) {
    auto* view = static_cast<IconView*>(lv_event_get_user_data(event));
    if (view->timer) lv_timer_delete(view->timer);
    lv_image_cache_drop(&view->image);
    std::free(view->owned); delete view;
}
}
lv_obj_t* color_icon(lv_obj_t* parent, const Button& button, Storage& storage, unsigned size) {
    auto* view = new(std::nothrow) IconView;
    if (!view) return nullptr;
    bool loaded = false;
    if (button.icon_asset[0]) {
        size_t bytes = 0;
        if (storage.load_icon(button.icon_asset,view->owned,bytes) == ESP_OK)
            loaded = icons::IconData::parse(view->owned,bytes,view->asset);
    }
    if (!loaded) for (const auto& builtin : icons::builtins) {
        if (std::strcmp(button.icon,builtin.name) == 0) {
            loaded = icons::IconData::parse(builtin.data,builtin.size,view->asset); break;
        }
    }
    if (!loaded) { std::free(view->owned); delete view; return nullptr; }
    view->asset.decode(0,view->pixels);
    view->image.header.magic = LV_IMAGE_HEADER_MAGIC;
    view->image.header.cf = LV_COLOR_FORMAT_RGB565A8;
    view->image.header.w = view->image.header.h = icons::kSize;
    view->image.header.stride = icons::kSize * 2;
    view->image.data_size = sizeof(view->pixels); view->image.data = view->pixels;
    view->object = lv_image_create(parent);
    lv_image_set_src(view->object,&view->image);
    lv_obj_set_style_image_recolor_opa(view->object,LV_OPA_TRANSP,0);
    lv_image_set_scale(view->object,size * 256 / icons::kSize);
    lv_obj_add_event_cb(view->object,delete_icon,LV_EVENT_DELETE,view);
    lv_obj_remove_flag(view->object,LV_OBJ_FLAG_CLICKABLE);
    if (view->asset.frames > 1) view->timer = lv_timer_create(advance_icon,view->asset.duration(0),view);
    return view->object;
}
}
