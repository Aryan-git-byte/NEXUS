# Optional Windows launcher workaround for Espressif's universal Xtensa GCC.
# Uses the identical compiler, CPU configuration and libraries as the S3 launcher.
set(_CMAKE_TOOLCHAIN_PREFIX xtensa-esp-elf-)
get_filename_component(_nexus_toolchain_file "${CMAKE_CURRENT_LIST_FILE}" ABSOLUTE)
get_filename_component(_nexus_toolchain_dir "${_nexus_toolchain_file}" DIRECTORY)
if(NOT EXISTS "${_nexus_toolchain_dir}/cflags")
    set(_nexus_toolchain_dir "${CMAKE_BINARY_DIR}/toolchain")
    file(MAKE_DIRECTORY "${_nexus_toolchain_dir}")
    configure_file("${_nexus_toolchain_file}" "${_nexus_toolchain_dir}/toolchain-esp32s3.cmake" COPYONLY)
    foreach(_nexus_flags cflags cxxflags asmflags ldflags)
        if(NOT EXISTS "${_nexus_toolchain_dir}/${_nexus_flags}")
            file(WRITE "${_nexus_toolchain_dir}/${_nexus_flags}" "")
        endif()
    endforeach()
endif()
set(CMAKE_TOOLCHAIN_FILE "${_nexus_toolchain_dir}/toolchain-esp32s3.cmake")
include($ENV{IDF_PATH}/tools/cmake/toolchain.cmake)
find_program(_nexus_gcc xtensa-esp-elf-gcc REQUIRED)
get_filename_component(_nexus_bin "${_nexus_gcc}" DIRECTORY)
get_filename_component(_nexus_cpu_config "${_nexus_bin}/../lib/xtensa_esp32s3.so" ABSOLUTE)
if(NOT EXISTS "${_nexus_cpu_config}")
    message(FATAL_ERROR "Universal Espressif GCC is missing the ESP32-S3 CPU configuration")
endif()
# The exact basename selects GCC's ESP32-S3 multilib; an absolute option selects
# the ISA but misses the multilib match. The helper adds this library to PATH.
idf_toolchain_remove_flags(COMPILE_OPTIONS "^-mdynconfig=" LINK_OPTIONS "^-mdynconfig=")
idf_toolchain_add_flags(COMPILE_OPTIONS "-mdynconfig=xtensa_esp32s3.so"
                        LINK_OPTIONS "-mdynconfig=xtensa_esp32s3.so")
