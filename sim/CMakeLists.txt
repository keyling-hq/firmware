cmake_minimum_required(VERSION 3.20)
project(keyling_ui_sim C)

# Host build of components/ui with the firmware's LVGL and LVGL settings.
#   cmake -S sim -B sim/build -G Ninja && cmake --build sim/build

set(FW ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(LVGL ${FW}/managed_components/lvgl__lvgl)
if(NOT EXISTS ${LVGL}/lvgl.h)
    message(FATAL_ERROR "LVGL not found in managed_components: build the firmware once first")
endif()

# CONFIG_LV_* from the firmware's sdkconfig, with the OS and display driver of the simulator.
set(SDKCONFIG ${FW}/sdkconfig.hid)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${SDKCONFIG})
file(STRINGS ${SDKCONFIG} lines REGEX "^CONFIG_LV_[A-Z0-9_]+=")
set(conf "#pragma once\n")
foreach(line IN LISTS lines)
    string(REGEX MATCH "^(CONFIG_LV_[A-Z0-9_]+)=(.*)$" _ "${line}")
    set(name ${CMAKE_MATCH_1})
    set(value "${CMAKE_MATCH_2}")
    if(name MATCHES "^CONFIG_LV_OS_")
        continue()
    endif()
    if(value STREQUAL "y")
        set(value 1)
    endif()
    string(APPEND conf "#define ${name} ${value}\n")
endforeach()
string(APPEND conf "#define CONFIG_LV_OS_WINDOWS 1\n#define CONFIG_LV_USE_WINDOWS 1\n")
file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/lv_sim_kconfig.h CONTENT "${conf}")

file(GLOB_RECURSE lvgl_srcs CONFIGURE_DEPENDS ${LVGL}/src/*.c)
add_library(lvgl STATIC ${lvgl_srcs})
target_include_directories(lvgl PUBLIC ${LVGL} ${LVGL}/src ${CMAKE_BINARY_DIR})
target_compile_definitions(lvgl PUBLIC
    LV_CONF_KCONFIG_EXTERNAL_INCLUDE="lv_sim_kconfig.h" LV_CONF_SKIP)
target_compile_options(lvgl PRIVATE -w)

add_executable(keyling_ui_sim
    ${FW}/components/ui/ui.c
    ${FW}/components/ui/ui_input.c
    ${FW}/components/ui/ui_settings.c
    ${FW}/components/ui/ui_jiggler.c
    ${FW}/components/ui/ui_profiles.c
    ${FW}/components/keymap/keymap.c
    ${FW}/components/keymap/keymap_layouts.c
    sim_main.c
    sim_button.c
    sim_platform.c
    sim_png.c
)
target_include_directories(keyling_ui_sim PRIVATE
    include
    ${FW}/components/ui
    ${FW}/components/ui/include
    ${FW}/components/bsp/include
    ${FW}/components/fw_info/include
    ${FW}/components/keymap
    ${FW}/components/keymap/include
    ${FW}/components/keystore/include
    ${FW}/components/settings/include
    ${FW}/components/usb_hid/include
)
target_compile_options(keyling_ui_sim PRIVATE -Wall -Wno-unused-function)
target_link_libraries(keyling_ui_sim PRIVATE lvgl)
if(WIN32)
    target_link_libraries(keyling_ui_sim PRIVATE gdi32 user32 imm32)
endif()
