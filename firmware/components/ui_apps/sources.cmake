# System shell (P3-06), clock app (P3-07), battery screen (P3-09), Settings app (P3-10), pairing alert (P4-01), find phone / watch (P4-08), Flashlight (P8-15), Connections (P4-09), Media (P6-01), Weather (P6-02), Calendar (P6-03), Calls (P6-04), Voice memos (P7-01), Home (P9-05) and mini apps (P8-05) sources, shared by the watch build
# (CMakeLists.txt) and the simulator so the two lists cannot drift. Portable: LVGL +
# ui_framework + watchfaces + the pure svc_alarm/svc_time/svc_power logic.
# S3W_SHELL_PRO_SRCS: Pro edition only (docs/10 §3); the build adds them when
# CONFIG_S3W_EDITION_PRO (watch) or S3W_EDITION_PRO (simulator) is on.
set(S3W_SHELL_SRCS
    ${CMAKE_CURRENT_LIST_DIR}/shell.c
    ${CMAKE_CURRENT_LIST_DIR}/shell_qs.c
    ${CMAKE_CURRENT_LIST_DIR}/shell_tiles.c
    ${CMAKE_CURRENT_LIST_DIR}/shell_launcher.c
    ${CMAKE_CURRENT_LIST_DIR}/clock_apps.c
    ${CMAKE_CURRENT_LIST_DIR}/clock_alarms.c
    ${CMAKE_CURRENT_LIST_DIR}/clock_ring.c
    ${CMAKE_CURRENT_LIST_DIR}/clock_timer.c
    ${CMAKE_CURRENT_LIST_DIR}/clock_stopwatch.c
    ${CMAKE_CURRENT_LIST_DIR}/clock_world.c
    ${CMAKE_CURRENT_LIST_DIR}/battery_apps.c
    ${CMAKE_CURRENT_LIST_DIR}/battery_screens.c
    ${CMAKE_CURRENT_LIST_DIR}/settings_apps.c
    ${CMAKE_CURRENT_LIST_DIR}/settings_tree.c
    ${CMAKE_CURRENT_LIST_DIR}/phone_apps.c
    ${CMAKE_CURRENT_LIST_DIR}/connect_apps.c
    ${CMAKE_CURRENT_LIST_DIR}/stopwatch.c
    ${CMAKE_CURRENT_LIST_DIR}/world_clock.c)

set(S3W_SHELL_PRO_SRCS
    ${CMAKE_CURRENT_LIST_DIR}/shell_notif.c
    ${CMAKE_CURRENT_LIST_DIR}/find_apps.c
    ${CMAKE_CURRENT_LIST_DIR}/flashlight_apps.c
    ${CMAKE_CURRENT_LIST_DIR}/media_apps.c
    ${CMAKE_CURRENT_LIST_DIR}/weather_apps.c
    ${CMAKE_CURRENT_LIST_DIR}/calendar_apps.c
    ${CMAKE_CURRENT_LIST_DIR}/call_apps.c
    ${CMAKE_CURRENT_LIST_DIR}/ha_apps.c
    ${CMAKE_CURRENT_LIST_DIR}/memo_apps.c
    ${CMAKE_CURRENT_LIST_DIR}/mini_apps.c)
