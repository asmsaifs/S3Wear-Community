# UI framework. lvgl_port/ is the watch-only LVGL port (P1-02); ui/ is portable
# (LVGL + HAL only) and also built by the simulator: theme, navigation, overlays,
# widgets, fonts and the built-in screens (P2-05).
include(${CMAKE_CURRENT_LIST_DIR}/ui/sources.cmake)

idf_component_register(SRCS "lvgl_port/s3w_lvgl_port.c" ${S3W_UI_PORTABLE_SRCS}
                       INCLUDE_DIRS "include"
                       REQUIRES lvgl esp_lcd
                       PRIV_REQUIRES s3w_hal sys_core esp_timer esp_pm heap log)
