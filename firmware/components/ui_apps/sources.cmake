# System shell (P3-06), clock app (P3-07), battery screen (P3-09) and Settings app (P3-10) sources, shared by the watch build
# (CMakeLists.txt) and the simulator so the two lists cannot drift. Portable: LVGL +
# ui_framework + watchfaces + the pure svc_alarm/svc_time/svc_power logic.
set(S3W_SHELL_SRCS
    ${CMAKE_CURRENT_LIST_DIR}/shell.c
    ${CMAKE_CURRENT_LIST_DIR}/shell_qs.c
    ${CMAKE_CURRENT_LIST_DIR}/shell_notif.c
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
    ${CMAKE_CURRENT_LIST_DIR}/stopwatch.c
    ${CMAKE_CURRENT_LIST_DIR}/world_clock.c)
