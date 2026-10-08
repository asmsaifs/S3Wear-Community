# S3Wear firmware (ESP-IDF v5.5.x)
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# WAMR (espressif/wasm-micro-runtime) reads these when its component is configured. The thread
# manager lets the app watchdog stop a spinning app with wasm_runtime_terminate() (P8-01); the
# component's Kconfig only offers it bundled with lib-pthread, which apps must not get.
set(WAMR_BUILD_THREAD_MGR 1)

# C++20 for all components (services/UI); no exceptions/RTTI (see sdkconfig.defaults).
idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++20" APPEND)

project(s3wear)

# Third-party: LVGL's lv_freertos.c includes FreeRTOS atomic.h, whose static
# inline helpers trigger -Wunused-function. Not our code; keep our build warning-free.
idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_options(${lvgl_lib} PRIVATE -Wno-unused-function)

# LVGL is always built with -O2, also in debug (-Og) builds, so the dev firmware
# scrolls like release: at -Og the list scroll bench drops from 29.9 to 25 fps (P2-04).
target_compile_options(${lvgl_lib} PRIVATE -O2)

# Mini apps: the WAMR interpreter and app_runtime's canvas raster are always -O2 for the same
# reason; at -Og the canvas benchmark (`app run bench`) could not hold 30 fps (P8-03).
# The Community export has no mini apps (docs/10 §3): WAMR is not built and app_runtime is an
# empty (interface) component there.
idf_build_get_property(s3w_build_components BUILD_COMPONENTS)
if("espressif__wasm-micro-runtime" IN_LIST s3w_build_components)
    idf_component_get_property(wamr_lib espressif__wasm-micro-runtime COMPONENT_LIB)
    target_compile_options(${wamr_lib} PRIVATE -O2)
endif()
idf_component_get_property(app_rt_lib app_runtime COMPONENT_LIB)
get_target_property(app_rt_type ${app_rt_lib} TYPE)
if(NOT app_rt_type STREQUAL "INTERFACE_LIBRARY")
    target_compile_options(${app_rt_lib} PRIVATE -O2)
endif()
