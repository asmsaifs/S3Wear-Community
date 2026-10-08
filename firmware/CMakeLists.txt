# S3Wear firmware (ESP-IDF v5.5.x)
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

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
