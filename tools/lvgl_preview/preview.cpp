// Render the product's real LVGL pages; fixture values stress text and geometry.
// This tool does not access an ESP32, touch hardware, a radio, or Windows input.
#include "ui/ui_manager.hpp"
#include "ui/ui_styles.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

extern std::vector<uint16_t> preview_pixels;
extern std::filesystem::path preview_icon_directory;

namespace nexus {
class UiPreview {
public:
    static void draw() {
        for (int tick = 0; tick < 8; ++tick) { lv_tick_inc(10); lv_timer_handler(); }
        lv_refr_now(nullptr);
    }
    static void save(const std::filesystem::path& path,LcdDriver& lcd) {
        draw();
        std::ofstream file(path,std::ios::binary);
        file << "P6\n" << lcd.width() << ' ' << lcd.height() << "\n255\n";
        for (const auto value : preview_pixels) {
            const char rgb[3] = {static_cast<char>(((value >> 11) & 31) * 255 / 31),
                static_cast<char>(((value >> 5) & 63) * 255 / 63),static_cast<char>((value & 31) * 255 / 31)};
            file.write(rgb,3);
        }
        if (!file) throw std::runtime_error("Could not write UI image");
    }
    static bool overlap(lv_obj_t* a,lv_obj_t* b) {
        lv_area_t x,y; lv_obj_get_coords(a,&x); lv_obj_get_coords(b,&y);
        return x.x1 <= y.x2 && y.x1 <= x.x2 && x.y1 <= y.y2 && y.y1 <= x.y2;
    }
    static void check_buttons(UiManager& ui) {
        for (unsigned index = 0; index < ui.button_count_; ++index) {
            auto* button = ui.buttons_[index].object;
            if (lv_obj_get_child_count(button) != 2 || overlap(lv_obj_get_child(button,0),lv_obj_get_child(button,1))) {
                std::fprintf(stderr,"Page %u, button %u: %u children\n",static_cast<unsigned>(ui.page_),index,
                             static_cast<unsigned>(lv_obj_get_child_count(button)));
                for (unsigned child = 0; child < lv_obj_get_child_count(button); ++child) {
                    lv_area_t bounds; lv_obj_get_coords(lv_obj_get_child(button,child),&bounds);
                    std::fprintf(stderr,"  child %u: (%d,%d)-(%d,%d)\n",child,bounds.x1,bounds.y1,bounds.x2,bounds.y2);
                }
                throw std::runtime_error("Macro icon/label overlap");
            }
            if (lv_obj_get_y(button) < 0 || lv_obj_get_y(button) + lv_obj_get_height(button) > lv_obj_get_height(ui.content_))
                throw std::runtime_error("Macro extends outside the content viewport");
        }
        if (ui.page_ == Page::Media && overlap(ui.media_title_,ui.media_artist_))
            throw std::runtime_error("Media title/artist overlap");
        if (ui.page_ == Page::Screensaver) {
            if (!lv_color_eq(lv_obj_get_style_bg_color(ui.root_,0),lv_color_black()))
                throw std::runtime_error("Sleep screen must remain black");
            for (auto* label : {ui.day_,ui.clock_,ui.date_,ui.pc_name_}) {
                if (!label || !lv_color_eq(lv_obj_get_style_text_color(label,0),lv_color_white()))
                    throw std::runtime_error("Sleep screen lettering must remain white");
            }
            if (overlap(ui.day_,ui.clock_) || overlap(ui.clock_,ui.date_) || overlap(ui.date_,ui.pc_name_))
                throw std::runtime_error("Sleep screen labels overlap");
            const auto* font = lv_obj_get_style_text_font(ui.clock_,0);
            lv_point_t size;
            lv_text_get_size(&size,"88:88",font,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
            if (font->line_height < 90 || size.x > lv_obj_get_width(ui.clock_))
                throw std::runtime_error("Clock digits are too small or clipped");
            if (std::strcmp(lv_label_get_text(ui.pc_name_),"Layout test PC") != 0)
                throw std::runtime_error("Sleep screen lost the computer name");
        }
    }
    static void find_dropdowns(lv_obj_t* object,std::vector<lv_obj_t*>& result) {
        if (lv_obj_check_type(object,&lv_dropdown_class)) result.push_back(object);
        for (unsigned i = 0; i < lv_obj_get_child_count(object); ++i) find_dropdowns(lv_obj_get_child(object,i),result);
    }
    static void render(UiManager& ui,const std::filesystem::path& directory) {
        const char* pages[] = {"home","development","media","monitor","macros","settings","clock","calibration"};
        std::snprintf(ui.telemetry_.pc_name,sizeof(ui.telemetry_.pc_name),"Layout test PC");
        std::snprintf(ui.telemetry_.media_title,sizeof(ui.telemetry_.media_title),
                      "A deliberately long media title to verify that text stays inside the narrow display");
        std::snprintf(ui.telemetry_.media_artist,sizeof(ui.telemetry_.media_artist),
                      "An artist with a long display name and extra words");
        ui.telemetry_.cpu = ui.telemetry_.ram = ui.telemetry_.disk = ui.telemetry_.battery = 100;
        ui.telemetry_.volume = 100; ui.telemetry_.media_available = true;
        for (const auto orientation : {Orientation::Landscape,Orientation::Portrait}) {
            ui.lcd_.set_orientation(orientation);
            const std::string suffix = orientation == Orientation::Landscape ? "landscape" : "portrait";
            for (unsigned p = 0; p < 8; ++p) {
                ui.telemetry_.received_ms = now_ms();
                ui.build(static_cast<Page>(p),false); lv_obj_update_layout(ui.root_);
                check_buttons(ui);
                save(directory / (std::string(pages[p]) + "-" + suffix + ".ppm"),ui.lcd_);
                if (p != static_cast<unsigned>(Page::Settings)) continue;
                std::vector<lv_obj_t*> dropdowns; find_dropdowns(ui.content_,dropdowns);
                if (dropdowns.size() != 4) throw std::runtime_error("Expected four settings dropdowns");
                for (unsigned d = 0; d < dropdowns.size(); ++d) {
                    if (d && overlap(dropdowns[d - 1],dropdowns[d])) throw std::runtime_error("Settings dropdowns overlap");
                    if (lv_obj_get_width(dropdowns[d]) < 100 || lv_obj_get_height(dropdowns[d]) != 40)
                        throw std::runtime_error("Settings control has invalid dimensions");
                    lv_obj_scroll_to_view(dropdowns[d],LV_ANIM_OFF); draw();
                    save(directory / ("setup-closed-" + std::to_string(d) + "-" + suffix + ".ppm"),ui.lcd_);
                    lv_dropdown_open(dropdowns[d]); draw();
                    save(directory / ("setup-open-" + std::to_string(d) + "-" + suffix + ".ppm"),ui.lcd_);
                    lv_dropdown_close(dropdowns[d]);
                }
                std::printf("%s: four separate settings controls and expanded lists rendered\n",suffix.c_str());
            }
            ui.telemetry_.received_ms = 0; ui.ble_.restart(false);
            ui.build(Page::Home,false);
            save(directory / ("offline-" + suffix + ".ppm"),ui.lcd_);
            ui.ble_.initialize();
        }
        // Delete objects while their ButtonView callbacks and icon storage are alive.
        lv_indev_delete(ui.input_); ui.input_ = nullptr;
        lv_obj_delete(ui.toast_); lv_obj_delete(ui.dim_);
        std::puts("PASS: actual LVGL pages in both orientations; macro and media text do not overlap");
    }
};
}

int main(int argc,char** argv) {
    if (argc < 3) { std::fputs("Usage: nexus_ui_preview DEVICE_PROFILE_JSON OUTPUT_DIRECTORY [ICON_DIRECTORY]\n",stderr); return 2; }
    try {
        const auto directory = std::filesystem::path(argv[2]); std::filesystem::create_directories(directory);
        preview_icon_directory = argc > 3 ? argv[3] : argv[2];
        std::ifstream source(argv[1],std::ios::binary);
        std::string json((std::istreambuf_iterator<char>(source)),{});
        auto profiles = std::make_unique<nexus::ProfileSet>();
        auto* root = nexus::parse_json(reinterpret_cast<const uint8_t*>(json.c_str()),json.size());
        char error[128]{};
        const bool valid = root && nexus::parse_profiles(root,*profiles,error,sizeof(error));
        cJSON_Delete(root);
        if (!valid) throw std::runtime_error(std::string("Profile fixture: ") + error);
        lv_init();
        {
            nexus::Storage storage; nexus::BleManager ble(storage); nexus::TouchDriver touch;
            nexus::LcdDriver lcd;
            if (lcd.attach_lvgl(nexus::Orientation::Landscape) != ESP_OK) throw std::runtime_error("Display allocation failed");
            ble.initialize();
            const int points[3] = {0,1,2}; touch.calibrate(points,points,points,points);
            nexus::UiManager ui(lcd,touch,ble,storage,std::move(profiles),nexus::DeviceSettings{});
            if (ui.initialize() != ESP_OK) throw std::runtime_error("UI initialization failed");
            nexus::UiPreview::render(ui,directory);
        }
        lv_deinit();
    } catch (const std::exception& error) { std::fprintf(stderr,"FAIL: %s\n",error.what()); return 1; }
}
