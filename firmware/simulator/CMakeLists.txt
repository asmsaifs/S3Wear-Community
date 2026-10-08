# Desktop simulator (SDL2 + LVGL). No ESP-IDF.
#
#   cmake -S firmware/simulator -B build/sim
#   cmake --build build/sim
#   ./build/sim/s3w_sim                          # window
#   ./build/sim/s3w_sim --screenshot out.png     # headless PNG
#   ctest --test-dir build/sim                   # UI snapshot tests (firmware/test/ui)
#
# Community edition (docs/10 §3), tests from firmware/test/ui/community:
#   cmake -S firmware/simulator -B build/sim-community -DS3W_EDITION_PRO=OFF
cmake_minimum_required(VERSION 3.16)
project(s3w_sim LANGUAGES C CXX)
enable_testing()

option(S3W_EDITION_PRO "Pro edition (OFF: Community, docs/10 §3)" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(SDL2 CONFIG QUIET)
if(NOT TARGET SDL2::SDL2)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    add_library(SDL2::SDL2 ALIAS PkgConfig::SDL2)
endif()

include(FetchContent)

# Same LVGL version as firmware/main/idf_component.yml.
FetchContent_Declare(
    lvgl
    URL https://github.com/lvgl/lvgl/archive/refs/tags/v9.3.0.tar.gz
    URL_HASH SHA256=4933becfd3603b29158a5d04138139582836ef2bc17beb6c39dccda9cb0d32e7
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(LV_BUILD_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(lvgl)
# LVGL's SDL driver includes <SDL2/SDL.h>.
target_link_libraries(lvgl PUBLIC SDL2::SDL2)

set(S3W_FW_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Mini app runtime: WAMR and the portable app_runtime parts, as in firmware/host_test. Pro only:
# the Community edition has no installable mini apps (docs/10 §3).
if(S3W_EDITION_PRO)
    add_subdirectory(${S3W_FW_DIR}/components/app_runtime/host ${CMAKE_CURRENT_BINARY_DIR}/app_runtime)
endif()

# Same portable sources as the watch build; hal_sim replaces s3w_hal/s3w + bsp_s3w.
include(${S3W_FW_DIR}/components/ui_framework/ui/sources.cmake)
include(${S3W_FW_DIR}/components/watchfaces/sources.cmake)
include(${S3W_FW_DIR}/components/ui_apps/sources.cmake)
# Pro only: the screens' simulated services and their pure state.
set(S3W_SIM_PRO_SRCS
    sim_app.c
    ${S3W_FW_DIR}/components/app_runtime/app_ui.c
    ${S3W_FW_DIR}/components/app_runtime/app_canvas.c
    ${S3W_FW_DIR}/components/app_runtime/app_image.c
    sim_notify.c
    sim_find.c
    sim_flashlight.c
    sim_media.c
    sim_weather.c
    sim_calendar.c
    sim_call.c
    sim_ha.c
    sim_memo.c
    ${S3W_FW_DIR}/components/svc_notify/notify_store.c
    ${S3W_FW_DIR}/components/svc_media/media_state.c
    ${S3W_FW_DIR}/components/svc_media/media_text.c
    ${S3W_FW_DIR}/components/svc_media/media_art.c
    ${S3W_FW_DIR}/components/svc_weather/weather_state.c
    ${S3W_FW_DIR}/components/svc_calendar/calendar_state.c
    ${S3W_FW_DIR}/components/svc_call/call_state.c
    ${S3W_FW_DIR}/components/svc_ha/ha_model.c
    ${S3W_FW_DIR}/components/svc_memo/memo_store.c
    ${S3W_SHELL_PRO_SRCS})
if(NOT S3W_EDITION_PRO)
    set(S3W_SIM_PRO_SRCS "")
endif()
add_executable(s3w_sim
    sim_main.c
    sim_screenshot.c
    sim_script.c
    sim_data.c
    sim_clock.c
    sim_modes.c
    sim_battery.c
    sim_settings.c
    sim_connect.c
    hal_sim/hal_sim.c
    ${S3W_FW_DIR}/components/svc_alarm/alarm_sched.c
    ${S3W_FW_DIR}/components/svc_alarm/timer_set.c
    ${S3W_FW_DIR}/components/svc_time/tz_posix.c
    ${S3W_FW_DIR}/components/svc_modes/modes.c
    ${S3W_FW_DIR}/components/svc_power/battery_hist.c
    ${S3W_FW_DIR}/components/svc_settings/settings_store.c
    ${S3W_FW_DIR}/components/svc_power/watch_only.c
    ${S3W_UI_PORTABLE_SRCS}
    ${S3W_WF_SRCS}
    ${S3W_SHELL_SRCS}
    ${S3W_SIM_PRO_SRCS})
target_include_directories(s3w_sim PRIVATE
    hal_sim
    ${S3W_FW_DIR}/components/s3w_hal/include
    ${S3W_FW_DIR}/components/ui_framework/include
    ${S3W_FW_DIR}/components/watchfaces/include
    ${S3W_FW_DIR}/components/watchfaces
    ${S3W_FW_DIR}/components/ui_apps/include
    ${S3W_FW_DIR}/components/svc_alarm/include
    ${S3W_FW_DIR}/components/svc_time/include
    ${S3W_FW_DIR}/components/svc_modes/include
    ${S3W_FW_DIR}/components/svc_power/include
    ${S3W_FW_DIR}/components/svc_settings/include
    ${S3W_FW_DIR}/components/svc_notify/include
    ${S3W_FW_DIR}/components/svc_media/include
    ${S3W_FW_DIR}/components/svc_weather/include
    ${S3W_FW_DIR}/components/svc_calendar/include
    ${S3W_FW_DIR}/components/svc_call/include
    ${S3W_FW_DIR}/components/svc_ha/include
    ${S3W_FW_DIR}/components/svc_memo/include
    ${S3W_FW_DIR}/components/sys_core/include
    ${S3W_FW_DIR}/host_stubs)
target_compile_options(s3w_sim PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Werror>)
target_compile_definitions(s3w_sim PRIVATE
    S3W_EDITION_PRO=$<BOOL:${S3W_EDITION_PRO}>
    S3W_TEST_APPS_DIR="${S3W_FW_DIR}/components/app_runtime/test_apps"
    S3W_SIM_FILES_DIR="${S3W_FW_DIR}/test/ui/files"
    S3W_SDK_EXAMPLES_DIR="${S3W_FW_DIR}/../sdk/examples")
target_link_libraries(s3w_sim PRIVATE lvgl::lvgl SDL2::SDL2)
if(S3W_EDITION_PRO)
    target_link_libraries(s3w_sim PRIVATE s3w_app_rt)
endif()
if(NOT WIN32)
    target_link_libraries(s3w_sim PRIVATE m)
endif()
# UI snapshot tests: every firmware/test/ui/<name>.txt renders and must match
# firmware/test/ui_snapshots/<name>.png (Community: test/ui/community/<name>.txt and
# ui_snapshots/community/<name>.png). Update goldens: firmware/test/ui/update_snapshots.sh
if(S3W_EDITION_PRO)
    set(S3W_UI_EDITION_DIR "")
else()
    set(S3W_UI_EDITION_DIR "/community")
endif()
file(GLOB S3W_UI_SCENARIOS ${S3W_FW_DIR}/test/ui${S3W_UI_EDITION_DIR}/*.txt)
foreach(scenario ${S3W_UI_SCENARIOS})
    get_filename_component(name ${scenario} NAME_WE)
    add_test(NAME ui_${name}
             COMMAND s3w_sim --script ${scenario}
                     --screenshot ${CMAKE_CURRENT_BINARY_DIR}/ui_snapshots/${name}.png
                     --expect ${S3W_FW_DIR}/test/ui_snapshots${S3W_UI_EDITION_DIR}/${name}.png)
endforeach()
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/ui_snapshots)

# The s3w CLI (P8-06): a new C / Rust app from the templates builds and runs in this simulator;
# Python only, its toolchain tests skip without clang (wasm32) or cargo + wasm32-unknown-unknown.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND AND S3W_EDITION_PRO)
    add_test(NAME sdk_cli
             COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${S3W_FW_DIR}/../sdk/tools/s3w-cli/tests)
    set_tests_properties(sdk_cli PROPERTIES ENVIRONMENT "S3W_SIM=$<TARGET_FILE:s3w_sim>")
endif()
