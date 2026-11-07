cmake_minimum_required(VERSION 3.24)
project(nexus_ui_preview LANGUAGES C CXX)
if(CMAKE_C_COMPILER MATCHES "zig(.exe)?$")
    set(CMAKE_ASM_COMPILER_ARG1 "cc" CACHE STRING "" FORCE)
    set(CMAKE_AR "${CMAKE_C_COMPILER}")
    set(CMAKE_C_ARCHIVE_CREATE "<CMAKE_AR> ar qc <TARGET> <LINK_FLAGS> <OBJECTS>")
    set(CMAKE_C_ARCHIVE_FINISH "<CMAKE_AR> ranlib <TARGET>")
    set(CMAKE_CXX_ARCHIVE_CREATE "<CMAKE_AR> ar qc <TARGET> <LINK_FLAGS> <OBJECTS>")
    set(CMAKE_CXX_ARCHIVE_FINISH "<CMAKE_AR> ranlib <TARGET>")
endif()
get_filename_component(NEXUS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
if(NOT IDF_PATH)
    set(IDF_PATH "$ENV{IDF_PATH}")
endif()
if(NOT EXISTS "${IDF_PATH}/components/json/cJSON/cJSON.c")
    message(FATAL_ERROR "Pass -DIDF_PATH pointing to the installed ESP-IDF source")
endif()
set(LV_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE STRING "" FORCE)
set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)
add_subdirectory("${NEXUS_ROOT}/firmware/managed_components/lvgl__lvgl" lvgl)
set(NEXUS_MAIN "${NEXUS_ROOT}/firmware/main")
add_executable(nexus_ui_preview preview.cpp hardware_fixtures.cpp
    "${NEXUS_MAIN}/app_model.cpp" "${NEXUS_MAIN}/assets/icon_data.cpp"
    "${NEXUS_MAIN}/ui/ui_manager.cpp" "${NEXUS_MAIN}/ui/ui_styles.cpp"
    "${NEXUS_MAIN}/ui/ui_icon.cpp" "${NEXUS_MAIN}/ui/ui_home.cpp"
    "${NEXUS_MAIN}/ui/ui_media.cpp" "${NEXUS_MAIN}/ui/ui_monitor.cpp"
    "${NEXUS_MAIN}/ui/ui_settings.cpp" "${NEXUS_MAIN}/ui/ui_screensaver.cpp"
    "${NEXUS_MAIN}/assets/clock_fonts.c"
    "${NEXUS_MAIN}/apps/local_apps.cpp" "${NEXUS_MAIN}/ui/ui_apps.cpp"
    "${NEXUS_MAIN}/ui/ui_calculator.cpp" "${NEXUS_MAIN}/ui/ui_timer.cpp"
    "${NEXUS_MAIN}/ui/ui_games.cpp" "${NEXUS_MAIN}/ui/ui_pet.cpp"
    "${IDF_PATH}/components/json/cJSON/cJSON.c"
    "${IDF_PATH}/components/mbedtls/mbedtls/library/base64.c"
    "${IDF_PATH}/components/mbedtls/mbedtls/library/constant_time.c")
target_include_directories(nexus_ui_preview PRIVATE include "${NEXUS_MAIN}"
    "${IDF_PATH}/components/json/cJSON"
    "${IDF_PATH}/components/mbedtls/mbedtls/include")
target_compile_definitions(nexus_ui_preview PRIVATE MBEDTLS_CONFIG_FILE="nexus_mbedtls.h")
target_compile_features(nexus_ui_preview PRIVATE cxx_std_17)
target_link_libraries(nexus_ui_preview PRIVATE lvgl)
add_executable(nexus_apps_tests "${NEXUS_ROOT}/tests/native/local_apps_test.cpp"
    "${NEXUS_MAIN}/apps/local_apps.cpp")
target_include_directories(nexus_apps_tests PRIVATE "${NEXUS_MAIN}")
target_compile_features(nexus_apps_tests PRIVATE cxx_std_17)
add_executable(nexus_ota_tests "${NEXUS_ROOT}/tests/native/ota_test.cpp"
    "${NEXUS_MAIN}/update/ota_manager.cpp" "${NEXUS_MAIN}/app_model.cpp"
    "${NEXUS_MAIN}/bluetooth/ble_protocol.cpp" "${NEXUS_MAIN}/assets/icon_data.cpp"
    "${IDF_PATH}/components/json/cJSON/cJSON.c"
    "${IDF_PATH}/components/mbedtls/mbedtls/library/base64.c"
    "${IDF_PATH}/components/mbedtls/mbedtls/library/constant_time.c"
    "${IDF_PATH}/components/mbedtls/mbedtls/library/sha256.c"
    "${IDF_PATH}/components/mbedtls/mbedtls/library/platform_util.c")
target_include_directories(nexus_ota_tests PRIVATE "${NEXUS_ROOT}/tests/native/ota_fixtures"
    include "${NEXUS_MAIN}" "${IDF_PATH}/components/json/cJSON"
    "${IDF_PATH}/components/mbedtls/mbedtls/include"
    "${IDF_PATH}/components/esp_common/include"
    "${IDF_PATH}/components/esp_app_format/include"
    "${IDF_PATH}/components/bootloader_support/include")
target_compile_definitions(nexus_ota_tests PRIVATE MBEDTLS_CONFIG_FILE="nexus_mbedtls.h")
target_compile_features(nexus_ota_tests PRIVATE cxx_std_17)
target_link_libraries(nexus_ota_tests PRIVATE lvgl)
add_executable(nexus_boot_tests "${NEXUS_ROOT}/tests/native/boot_info_test.cpp"
    "${NEXUS_MAIN}/diagnostics/boot_info.cpp")
target_include_directories(nexus_boot_tests PRIVATE "${NEXUS_ROOT}/tests/native/boot_fixtures" "${NEXUS_MAIN}")
target_compile_features(nexus_boot_tests PRIVATE cxx_std_17)
