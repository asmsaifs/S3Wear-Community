# System shell around the watch face (P3-06), the clock apps (P3-07) and the battery
# screens (P3-09): quick settings, notifications, tiles, launcher, app registry,
# alarms, timer, stopwatch, world clock, battery, charging. Portable: the simulator
# builds the same list. stopwatch.c and world_clock.c are pure C and also built by
# firmware/host_test, and so is settings_tree.c. svc_power: only its pure battery_hist.h;
# svc_notify: only its pure notify_store.h (notifications, P4-07); svc_media: only its pure
# media_state.h (Media, P6-01); svc_weather: only its pure weather_state.h (Weather, P6-02); svc_calendar: only its pure calendar_state.h (Calendar,
# P6-03); svc_call: only its pure call_state.h (Calls, P6-04); svc_ha: only its pure ha_model.h
# (Home, P9-05); svc_memo: only its pure memo_store.h (Voice memos, P7-01).
include(${CMAKE_CURRENT_LIST_DIR}/sources.cmake)
# Pro-only screens (docs/10 §3). REQUIRES cannot depend on the configuration (IDF), so the
# svc_* below stay listed in both editions; only their pure headers are used.
if(CONFIG_S3W_EDITION_PRO)
    list(APPEND S3W_SHELL_SRCS ${S3W_SHELL_PRO_SRCS})
endif()

idf_component_register(SRCS ${S3W_SHELL_SRCS}
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "."
                       REQUIRES lvgl ui_framework svc_alarm svc_power svc_notify svc_media svc_weather svc_calendar svc_call svc_ha svc_memo sys_core
                       PRIV_REQUIRES watchfaces svc_time)
