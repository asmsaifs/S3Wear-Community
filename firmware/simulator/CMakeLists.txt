# Desktop simulator (SDL2 + LVGL). No ESP-IDF.
#
#   cmake -S firmware/simulator -B build/sim
#   cmake --build build/sim
#   ./build/sim/s3w_sim                          # window
#   ./build/sim/s3w_sim --screenshot out.png     # headless PNG
#   ctest --test-dir build/sim                   # UI snapshot tests (firmware/test/ui)
cmake_minimum_required(VERSION 3.16)
project(s3w_sim LANGUAGES C CXX)
enable_testing()

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

# Same portable sources as the watch build; hal_sim replaces s3w_hal/s3w + bsp_s3w.
include(${S3W_FW_DIR}/components/ui_framework/ui/sources.cmake)
include(${S3W_FW_DIR}/components/watchfaces/sources.cmake)
include(${S3W_FW_DIR}/components/ui_apps/sources.cmake)
add_executable(s3w_sim
    sim_main.c
    sim_screenshot.c
    sim_script.c
    sim_data.c
    sim_clock.c
    sim_modes.c
    sim_battery.c
    sim_settings.c
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
    ${S3W_SHELL_SRCS})
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
    ${S3W_FW_DIR}/components/sys_core/include
    ${S3W_FW_DIR}/host_stubs)
target_compile_options(s3w_sim PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Werror>)
target_link_libraries(s3w_sim PRIVATE lvgl::lvgl SDL2::SDL2)
if(NOT WIN32)
    target_link_libraries(s3w_sim PRIVATE m)
endif()
# UI snapshot tests: every firmware/test/ui/<name>.txt renders and must match
# firmware/test/ui_snapshots/<name>.png. Update goldens: firmware/test/ui/update_snapshots.sh
file(GLOB S3W_UI_SCENARIOS ${S3W_FW_DIR}/test/ui/*.txt)
foreach(scenario ${S3W_UI_SCENARIOS})
    get_filename_component(name ${scenario} NAME_WE)
    add_test(NAME ui_${name}
             COMMAND s3w_sim --script ${scenario}
                     --screenshot ${CMAKE_CURRENT_BINARY_DIR}/ui_snapshots/${name}.png
                     --expect ${S3W_FW_DIR}/test/ui_snapshots/${name}.png)
endforeach()
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/ui_snapshots)
