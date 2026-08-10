cmake_minimum_required(VERSION 3.16)
project(touchbar-fx VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TOUCHBAR_FX_SCREEN "SDL2 on-screen window backend (requires sdl2)" OFF)

find_package(PkgConfig REQUIRED)

pkg_check_modules(DRM REQUIRED IMPORTED_TARGET libdrm)
pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse libpulse-simple)
pkg_check_modules(DBUS REQUIRED IMPORTED_TARGET dbus-1)
pkg_check_modules(FFTW3 REQUIRED IMPORTED_TARGET fftw3)
pkg_check_modules(EVDEV REQUIRED IMPORTED_TARGET libevdev)
pkg_check_modules(ZLIB REQUIRED IMPORTED_TARGET zlib)

# Visualizer presets self-register; adding one is a single new file in
# src/viz/presets/ (picked up here) with a REGISTER_PRESET line in it.
file(GLOB PRESET_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viz/presets/*.cpp)

# OBJECT (not STATIC) so the self-registering preset/mode translation
# units are always linked in whole, never dropped by the linker.
add_library(tbfx_core OBJECT
    src/engine/event_loop.cpp
    src/engine/scheduler.cpp
    src/drm/drm_device.cpp
    src/drm/drm_output.cpp
    src/gfx/framebuffer.cpp
    src/gfx/font.cpp
    src/gfx/theme.cpp
    src/config/config.cpp
    src/audio/pulse_monitor_source.cpp
    src/audio/null_audio_source.cpp
    src/viz/viz_renderer.cpp
    src/viz/preset_registry.cpp
    src/viz/lkd.cpp
    ${PRESET_SOURCES}
    src/media/mpris_dbus_client.cpp
    src/media/art_fetcher.cpp
    src/output/dump_output.cpp
    src/mode/mode_registry.cpp
    src/mode/mode_manager.cpp
    src/mode/mediacenter_mode.cpp
    src/mode/sliders_mode.cpp
    src/mode/colors_mode.cpp
    src/ipc/ipc_server.cpp
    src/ipc/http_bridge.cpp
    src/widget/widget.cpp
    src/widget/layout_engine.cpp
    src/widget/widget_registry.cpp
    src/widget/builtin/clock_widget.cpp
    src/widget/builtin/battery_widget.cpp
    src/widget/builtin/sysmon_widget.cpp
    src/widget/builtin/separator_widget.cpp
    src/widget/builtin/spacer_widget.cpp
    src/widget/builtin/transport_widget.cpp
    src/widget/builtin/volume_slider_widget.cpp
    src/widget/builtin/mode_button_widget.cpp
    src/widget/builtin/preset_button_widget.cpp
    src/widget/slider_widget.cpp
    src/widget/builtin/brightness_widget.cpp
    src/widget/builtin/hue_slider_widget.cpp
    src/widget/builtin/viz_widget.cpp
    src/input/touch_input.cpp
    src/util/log.cpp
    src/util/timer_fd.cpp
)

target_include_directories(tbfx_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_link_libraries(tbfx_core PUBLIC
    PkgConfig::DRM
    PkgConfig::PULSE
    PkgConfig::DBUS
    PkgConfig::FFTW3
    PkgConfig::EVDEV
    PkgConfig::ZLIB
    pthread
)

target_compile_options(tbfx_core PRIVATE -Wall -Wextra -O2)

# Optional SDL2 on-screen backend: cmake -B build -DTOUCHBAR_FX_SCREEN=ON
if(TOUCHBAR_FX_SCREEN)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    target_sources(tbfx_core PRIVATE src/output/sdl_output.cpp)
    target_link_libraries(tbfx_core PUBLIC PkgConfig::SDL2)
    target_compile_definitions(tbfx_core PUBLIC TOUCHBAR_FX_SCREEN)
endif()

add_executable(touchbar-fx src/main.cpp)
target_link_libraries(touchbar-fx PRIVATE tbfx_core)
target_compile_options(touchbar-fx PRIVATE -Wall -Wextra -O2)

add_executable(touchbar-fxctl src/ipc/touchbar-fxctl.cpp)

# Headless unit tests: gesture classification, drag math, preset
# registry/frames, config parsing, mode registry. No hardware needed.
add_executable(touchbar-fx-test src/tests/test_main.cpp)
target_link_libraries(touchbar-fx-test PRIVATE tbfx_core)
target_compile_options(touchbar-fx-test PRIVATE -Wall -Wextra -O2)

enable_testing()
add_test(NAME unit COMMAND touchbar-fx-test)

install(TARGETS touchbar-fx touchbar-fxctl DESTINATION bin)

# Pioneer OEL clips for the "pioneer" preset (decoded at runtime by
# src/viz/lkd.cpp; override location with $TOUCHBAR_FX_CLIP_DIR).
install(DIRECTORY assets/clips DESTINATION share/touchbar-fx)
