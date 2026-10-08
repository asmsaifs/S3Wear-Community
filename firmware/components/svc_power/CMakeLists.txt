# Power service (docs/02-firmware-architecture.md §7). power_fsm.c, battery_hist.c
# and watch_only.c are pure C and also built by firmware/host_test (battery_hist.c
# by the simulator too).
idf_component_register(SRCS "svc_power.c" "power_fsm.c" "battery_hist.c" "watch_only.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_event
                       PRIV_REQUIRES s3w_hal sys_core svc_modes svc_settings esp_pm esp_timer log)
