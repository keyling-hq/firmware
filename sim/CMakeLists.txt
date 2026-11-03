cmake_minimum_required(VERSION 3.20)
project(keyling_ui_sim C)

# Host build of components/ui with the firmware's LVGL and LVGL settings (see README.md).
# The window uses LVGL's Win32 driver on Windows and its SDL2 driver elsewhere.

set(FW ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(LVGL ${FW}/managed_components/lvgl__lvgl)
if(NOT EXISTS ${LVGL}/lvgl.h)
    message(FATAL_ERROR "LVGL not found in managed_components: build the firmware once first")
endif()

# CONFIG_LV_* from the firmware's sdkconfig, with the OS and display driver of the simulator.
set(SDKCONFIG ${FW}/sdkconfig.hid CACHE FILEPATH "Firmware sdkconfig the simulator reads its LVGL and UI settings from")
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
if(NOT WIN32)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 IMPORTED_TARGET sdl2)
    if(NOT SDL2_FOUND)
        message(WARNING "SDL2 not found: building without the window (screenshots only)")
    endif()
endif()
if(WIN32)
    string(APPEND conf "#define CONFIG_LV_OS_WINDOWS 1\n#define CONFIG_LV_USE_WINDOWS 1\n")
elseif(NOT SDL2_FOUND)
    string(APPEND conf "#define CONFIG_LV_OS_NONE 1\n")
else()
    string(APPEND conf
        "#define CONFIG_LV_OS_NONE 1\n"
        "#define CONFIG_LV_USE_SDL 1\n"
        "#define CONFIG_LV_SDL_INCLUDE_PATH <SDL2/SDL.h>\n"
        "#define CONFIG_LV_SDL_RENDER_MODE_DIRECT 1\n"
        "#define CONFIG_LV_SDL_BUF_COUNT 1\n"
        "#define CONFIG_LV_SDL_ACCELERATED 1\n"
        "#define CONFIG_LV_SDL_DIRECT_EXIT 1\n")
endif()
file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/lv_sim_kconfig.h CONTENT "${conf}")

file(GLOB_RECURSE lvgl_srcs CONFIGURE_DEPENDS ${LVGL}/src/*.c)
add_library(lvgl STATIC ${lvgl_srcs})
target_include_directories(lvgl PUBLIC ${LVGL} ${LVGL}/src ${CMAKE_BINARY_DIR})
target_compile_definitions(lvgl PUBLIC
    LV_CONF_KCONFIG_EXTERNAL_INCLUDE="lv_sim_kconfig.h" LV_CONF_SKIP)
target_compile_options(lvgl PRIVATE -w)
if(WIN32)
    target_link_libraries(lvgl PUBLIC gdi32 user32 imm32)
elseif(SDL2_FOUND)
    target_link_libraries(lvgl PUBLIC PkgConfig::SDL2 m)
else()
    target_link_libraries(lvgl PUBLIC m)
endif()

add_executable(keyling_ui_sim
    ${FW}/components/ui/ui.c
    ${FW}/components/ui/ui_nav.c
    ${FW}/components/ui/ui_input.c
    ${FW}/components/ui/ui_settings.c
    ${FW}/components/ui/ui_sites.c
    ${FW}/components/ui/ui_site_table.c
    ${FW}/components/ui/ui_theme_candy.c
    ${FW}/components/ui/ui_theme_device.c
    ${FW}/components/status_led/status_led_pattern.c
    ${FW}/components/keymap/keymap.c
    ${FW}/components/keymap/keymap_layouts.c
    sim_main.c
    sim_button.c
    sim_platform.c
    sim_png.c
    sim_window.c
)

# The apps and themes the firmware's sdkconfig selects (components/ui/Kconfig, with its defaults
# where the sdkconfig doesn't have the option yet). SIM_EXAMPLES adds the template app and theme.
set(UI_APP_JIGGLER ON)
set(UI_APP_PROFILES ON)
set(UI_APP_DRIVE OFF)
set(UI_APP_EXAMPLE OFF)
set(UI_THEME_EXAMPLE OFF)
file(STRINGS ${SDKCONFIG} ui_lines REGEX "CONFIG_UI_[A-Z_]+")
foreach(line IN LISTS ui_lines)
    if(line MATCHES "^CONFIG_(UI_[A-Z_]+)=y$")
        set(${CMAKE_MATCH_1} ON)
    elseif(line MATCHES "^# CONFIG_(UI_[A-Z_]+) is not set$")
        set(${CMAKE_MATCH_1} OFF)
    endif()
endforeach()
option(SIM_EXAMPLES "Build the template app and the example theme" OFF)
if(SIM_EXAMPLES)
    set(UI_APP_EXAMPLE ON)
    set(UI_THEME_EXAMPLE ON)
endif()
set(ui_optional
    UI_APP_JIGGLER ui_jiggler.c
    UI_APP_PROFILES ui_profiles.c
    UI_APP_DRIVE ui_drive.c
    UI_APP_EXAMPLE ui_app_example.c
    UI_THEME_EXAMPLE ui_theme_example.c)
while(ui_optional)
    list(POP_FRONT ui_optional option file)
    if(${option})
        target_sources(keyling_ui_sim PRIVATE ${FW}/components/ui/${file})
        target_compile_definitions(keyling_ui_sim PRIVATE CONFIG_${option}=1)
    endif()
endwhile()
message(STATUS "apps: jiggler ${UI_APP_JIGGLER}, profiles ${UI_APP_PROFILES}, drive ${UI_APP_DRIVE}, "
               "template ${UI_APP_EXAMPLE}; "
               "example theme ${UI_THEME_EXAMPLE}")
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
    ${FW}/components/status_led/include
    ${FW}/components/usb_hid/include
)
target_compile_options(keyling_ui_sim PRIVATE -Wall -Wno-unused-function)
target_link_libraries(keyling_ui_sim PRIVATE lvgl)
