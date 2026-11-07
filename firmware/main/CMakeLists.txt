idf_component_register(
    SRCS "main.cpp" "app_model.cpp"
         "display/lcd_driver.cpp" "display/touch_driver.cpp"
         "assets/icon_data.cpp" "assets/clock_fonts.c" "ui/ui_icon.cpp"
         "bluetooth/ble_protocol.cpp" "bluetooth/ble_manager.cpp"
         "storage/storage.cpp" "update/ota_manager.cpp" "diagnostics/boot_info.cpp"
         "apps/local_apps.cpp" "ui/ui_apps.cpp" "ui/ui_calculator.cpp"
         "ui/ui_timer.cpp" "ui/ui_games.cpp" "ui/ui_pet.cpp"
         "ui/ui_manager.cpp" "ui/ui_styles.cpp" "ui/ui_home.cpp"
         "ui/ui_media.cpp" "ui/ui_monitor.cpp" "ui/ui_settings.cpp" "ui/ui_screensaver.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_ledc esp_timer nvs_flash bt json
             esp_hw_support esp_system app_update mbedtls lvgl
    EMBED_TXTFILES "../../profiles/device_defaults.json"
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror=return-type)
