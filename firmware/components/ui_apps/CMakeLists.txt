# System shell around the watch face (P3-06), the clock apps (P3-07) and the battery
# screens (P3-09): quick settings, notifications, tiles, launcher, app registry,
# alarms, timer, stopwatch, world clock, battery, charging. Portable: the simulator
# builds the same list. stopwatch.c and world_clock.c are pure C and also built by
# firmware/host_test, and so is settings_tree.c. svc_power: only its pure battery_hist.h.
include(${CMAKE_CURRENT_LIST_DIR}/sources.cmake)

idf_component_register(SRCS ${S3W_SHELL_SRCS}
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "."
                       REQUIRES lvgl ui_framework svc_alarm svc_power sys_core
                       PRIV_REQUIRES watchfaces svc_time)
