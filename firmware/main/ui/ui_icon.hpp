#pragma once
#include "app_model.hpp"
#include "storage/storage.hpp"
#include "lvgl.h"

namespace nexus {
lv_obj_t* color_icon(lv_obj_t* parent, const Button& button, Storage& storage, unsigned size);
}
