# Host unit tests for pure-logic firmware code (no ESP-IDF, no hardware).
#
#   cmake -S firmware/host_test -B build/host_test
#   cmake --build build/host_test
#   ctest --test-dir build/host_test --output-on-failure
cmake_minimum_required(VERSION 3.16)
project(s3w_host_test LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# GoogleTest pinned by release tarball + hash so builds are reproducible.
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/releases/download/v1.17.0/googletest-1.17.0.tar.gz
    URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)

# s3w_host_test(<name> SOURCES <files...> [LIBS <targets...>])
# Adds one test executable and registers each TEST() with ctest.
function(s3w_host_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    target_compile_options(${name} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Werror>)
    target_link_libraries(${name} PRIVATE GTest::gmock_main ${ARG_LIBS})
    gtest_discover_tests(${name})
endfunction()

set(S3W_FW_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(S3W_REPO_DIR ${S3W_FW_DIR}/..)

# nlohmann/json reads the golden vector .json files.
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# Vendored nanopb runtime and the generated protocol code, built for the host.
add_library(s3w_nanopb STATIC
    ${S3W_FW_DIR}/components/nanopb/src/pb_common.c
    ${S3W_FW_DIR}/components/nanopb/src/pb_encode.c
    ${S3W_FW_DIR}/components/nanopb/src/pb_decode.c)
target_include_directories(s3w_nanopb PUBLIC ${S3W_FW_DIR}/components/nanopb/include)

file(GLOB S3W_PROTO_SRCS CONFIGURE_DEPENDS ${S3W_FW_DIR}/components/proto/*.pb.c)
add_library(s3w_proto STATIC ${S3W_PROTO_SRCS})
target_include_directories(s3w_proto PUBLIC ${S3W_FW_DIR}/components/proto)
target_link_libraries(s3w_proto PUBLIC s3w_nanopb)

s3w_host_test(test_sample SOURCES test_sample.cpp)

s3w_host_test(test_proto_vectors
    SOURCES test_proto_vectors.cpp
    LIBS s3w_proto nlohmann_json::nlohmann_json)
target_compile_definitions(test_proto_vectors PRIVATE
    S3W_TESTVECTORS_DIR="${S3W_REPO_DIR}/protocol/testvectors")

# drv_pcf85063 register codec (BCD, time/alarm/offset encoding).
add_library(s3w_pcf85063_codec STATIC ${S3W_FW_DIR}/components/drv_pcf85063/pcf85063_codec.c)
target_include_directories(s3w_pcf85063_codec PUBLIC ${S3W_FW_DIR}/components/drv_pcf85063/include)
s3w_host_test(test_pcf85063_codec SOURCES test_pcf85063_codec.cpp LIBS s3w_pcf85063_codec)

# drv_axp2101 register codec (rails, charger, ADC conversions).
add_library(s3w_axp2101_codec STATIC ${S3W_FW_DIR}/components/drv_axp2101/axp2101_codec.c)
target_include_directories(s3w_axp2101_codec PUBLIC ${S3W_FW_DIR}/components/drv_axp2101/include)
s3w_host_test(test_axp2101_codec SOURCES test_axp2101_codec.cpp LIBS s3w_axp2101_codec)

# drv_qmi8658 conversions (scales, FIFO parsing, tap thresholds, self-test).
add_library(s3w_qmi8658_codec STATIC ${S3W_FW_DIR}/components/drv_qmi8658/qmi8658_codec.c)
target_include_directories(s3w_qmi8658_codec PUBLIC ${S3W_FW_DIR}/components/drv_qmi8658/include)
s3w_host_test(test_qmi8658_codec SOURCES test_qmi8658_codec.cpp LIBS s3w_qmi8658_codec)

# drv_audio DSP helpers (tone generator, Goertzel tone detection).
add_library(s3w_audio_dsp STATIC ${S3W_FW_DIR}/components/drv_audio/audio_dsp.c)
target_include_directories(s3w_audio_dsp PUBLIC ${S3W_FW_DIR}/components/drv_audio/include)
if(NOT WIN32)
    target_link_libraries(s3w_audio_dsp PUBLIC m)
endif()
s3w_host_test(test_audio_dsp SOURCES test_audio_dsp.cpp LIBS s3w_audio_dsp)

# svc_audio sounds, policy and mixer.
add_library(s3w_audio_mix STATIC ${S3W_FW_DIR}/components/svc_audio/audio_mix.c)
target_include_directories(s3w_audio_mix PUBLIC ${S3W_FW_DIR}/components/svc_audio/include)
target_link_libraries(s3w_audio_mix PUBLIC s3w_audio_dsp)
s3w_host_test(test_audio_mix SOURCES test_audio_mix.cpp LIBS s3w_audio_mix)

# factory_test result model + JSON output.
add_library(s3w_ft_result STATIC ${S3W_FW_DIR}/components/factory_test/ft_result.c)
target_include_directories(s3w_ft_result PUBLIC ${S3W_FW_DIR}/components/factory_test/include)
s3w_host_test(test_ft_result SOURCES test_ft_result.cpp LIBS s3w_ft_result nlohmann_json::nlohmann_json)

# sys_core UI mailbox (ordering, wrap-around, overflow).
add_library(s3w_mailbox STATIC ${S3W_FW_DIR}/components/sys_core/s3w_mailbox.c)
target_include_directories(s3w_mailbox PUBLIC ${S3W_FW_DIR}/components/sys_core/include)
s3w_host_test(test_s3w_mailbox SOURCES test_s3w_mailbox.cpp LIBS s3w_mailbox)

# svc_settings store (schema, validation, load/flush) against a fake NVS.
add_library(s3w_settings_store STATIC ${S3W_FW_DIR}/components/svc_settings/settings_store.c)
target_include_directories(s3w_settings_store PUBLIC
    ${S3W_FW_DIR}/components/svc_settings/include
    ${S3W_FW_DIR}/components/sys_core/include
    ${S3W_FW_DIR}/host_stubs)
s3w_host_test(test_settings_store SOURCES test_settings_store.cpp LIBS s3w_settings_store)

# svc_power state machine (ACTIVE/DIM/AOD/SLEEP/SAVER/WATCH-ONLY/OFF).
add_library(s3w_power_fsm STATIC
    ${S3W_FW_DIR}/components/svc_power/power_fsm.c
    ${S3W_FW_DIR}/components/svc_power/battery_hist.c
    ${S3W_FW_DIR}/components/svc_power/watch_only.c)
target_include_directories(s3w_power_fsm PUBLIC ${S3W_FW_DIR}/components/svc_power/include)
s3w_host_test(test_power_fsm SOURCES test_power_fsm.cpp LIBS s3w_power_fsm)
s3w_host_test(test_battery_hist SOURCES test_battery_hist.cpp LIBS s3w_power_fsm)
s3w_host_test(test_watch_only SOURCES test_watch_only.cpp LIBS s3w_power_fsm)

# svc_input button presses (short/long/double/triple) and palm-cover detection.
add_library(s3w_input_logic STATIC
    ${S3W_FW_DIR}/components/svc_input/button_gesture.c
    ${S3W_FW_DIR}/components/svc_input/palm_detect.c)
target_include_directories(s3w_input_logic PUBLIC ${S3W_FW_DIR}/components/svc_input/include)
s3w_host_test(test_button_gesture SOURCES test_button_gesture.cpp LIBS s3w_input_logic)
s3w_host_test(test_palm_detect SOURCES test_palm_detect.cpp LIBS s3w_input_logic)

# svc_diag persistent metrics ring (boot count, reset reasons, min heap, CRC'd NVS blob).
add_library(s3w_metrics_ring STATIC ${S3W_FW_DIR}/components/svc_diag/metrics_ring.c)
target_include_directories(s3w_metrics_ring PUBLIC ${S3W_FW_DIR}/components/svc_diag/include)
s3w_host_test(test_metrics_ring SOURCES test_metrics_ring.cpp LIBS s3w_metrics_ring)

# svc_time: POSIX TZ engine (DST transitions) and RTC drift calibration.
add_library(s3w_time_logic STATIC
    ${S3W_FW_DIR}/components/svc_time/tz_posix.c
    ${S3W_FW_DIR}/components/svc_time/time_drift.c)
target_include_directories(s3w_time_logic PUBLIC ${S3W_FW_DIR}/components/svc_time/include)
s3w_host_test(test_tz_posix SOURCES test_tz_posix.cpp LIBS s3w_time_logic)
s3w_host_test(test_time_drift SOURCES test_time_drift.cpp LIBS s3w_time_logic)

# watchfaces: binding registry, date/number formatting, moon phase, complication views,
# JSON pull parser, the face.json parser, saved configuration and the AOD burn-in shift.
add_library(s3w_wf_logic STATIC
    ${S3W_FW_DIR}/components/watchfaces/wf_bind.c
    ${S3W_FW_DIR}/components/watchfaces/wf_comp.c
    ${S3W_FW_DIR}/components/watchfaces/wf_json.c
    ${S3W_FW_DIR}/components/watchfaces/wf_decl.c
    ${S3W_FW_DIR}/components/watchfaces/wf_cfg.c
    ${S3W_FW_DIR}/components/watchfaces/wf_shift.c)
target_include_directories(s3w_wf_logic PUBLIC ${S3W_FW_DIR}/components/watchfaces/include)
if(NOT WIN32)
    target_link_libraries(s3w_wf_logic PUBLIC m)
endif()
s3w_host_test(test_wf_bind SOURCES test_wf_bind.cpp LIBS s3w_wf_logic)
s3w_host_test(test_wf_decl SOURCES test_wf_decl.cpp LIBS s3w_wf_logic)
s3w_host_test(test_wf_cfg SOURCES test_wf_cfg.cpp LIBS s3w_wf_logic)
s3w_host_test(test_wf_shift SOURCES test_wf_shift.cpp LIBS s3w_wf_logic)
target_compile_definitions(test_wf_decl PRIVATE
    S3W_WF_SAMPLES_DIR="${S3W_FW_DIR}/components/watchfaces/samples")

# svc_sensors raise-to-wake gesture detector.
add_library(s3w_raise_detect STATIC ${S3W_FW_DIR}/components/svc_sensors/raise_detect.c)
target_include_directories(s3w_raise_detect PUBLIC ${S3W_FW_DIR}/components/svc_sensors/include)
if(NOT WIN32)
    target_link_libraries(s3w_raise_detect PUBLIC m)
endif()
s3w_host_test(test_raise_detect SOURCES test_raise_detect.cpp LIBS s3w_raise_detect)

# svc_sensors flip detector (flip to snooze).
add_library(s3w_flip_detect STATIC ${S3W_FW_DIR}/components/svc_sensors/flip_detect.c)
target_include_directories(s3w_flip_detect PUBLIC ${S3W_FW_DIR}/components/svc_sensors/include)
if(NOT WIN32)
    target_link_libraries(s3w_flip_detect PUBLIC m)
endif()
s3w_host_test(test_flip_detect SOURCES test_flip_detect.cpp LIBS s3w_flip_detect)

# svc_alarm scheduler (alarms, snooze, DST, NVS blob) and countdown timers.
add_library(s3w_alarm_logic STATIC
    ${S3W_FW_DIR}/components/svc_alarm/alarm_sched.c
    ${S3W_FW_DIR}/components/svc_alarm/timer_set.c)
target_include_directories(s3w_alarm_logic PUBLIC ${S3W_FW_DIR}/components/svc_alarm/include)
target_link_libraries(s3w_alarm_logic PUBLIC s3w_time_logic)
s3w_host_test(test_alarm_sched SOURCES test_alarm_sched.cpp LIBS s3w_alarm_logic)
s3w_host_test(test_timer_set SOURCES test_timer_set.cpp LIBS s3w_alarm_logic)

# ui_apps pure helpers: stopwatch and the world clock city table.
add_library(s3w_clock_apps_logic STATIC
    ${S3W_FW_DIR}/components/ui_apps/stopwatch.c
    ${S3W_FW_DIR}/components/ui_apps/world_clock.c)
target_include_directories(s3w_clock_apps_logic PUBLIC ${S3W_FW_DIR}/components/ui_apps/include)
target_link_libraries(s3w_clock_apps_logic PUBLIC s3w_time_logic)
s3w_host_test(test_stopwatch SOURCES test_stopwatch.cpp LIBS s3w_clock_apps_logic)

# Settings app tree (P3-10) against the settings schema.
add_library(s3w_settings_tree STATIC ${S3W_FW_DIR}/components/ui_apps/settings_tree.c)
target_include_directories(s3w_settings_tree PUBLIC ${S3W_FW_DIR}/components/ui_apps/include)
target_link_libraries(s3w_settings_tree PUBLIC s3w_settings_store)
s3w_host_test(test_settings_tree SOURCES test_settings_tree.cpp LIBS s3w_settings_tree)

# svc_modes: DND / sleep / theater modes and their weekly schedules.
add_library(s3w_modes_logic STATIC ${S3W_FW_DIR}/components/svc_modes/modes.c)
target_include_directories(s3w_modes_logic PUBLIC ${S3W_FW_DIR}/components/svc_modes/include)
s3w_host_test(test_modes SOURCES test_modes.cpp LIBS s3w_modes_logic)
