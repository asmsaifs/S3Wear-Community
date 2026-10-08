# Time service (docs/03 F1, docs/02 §5). tz_posix.c and time_drift.c are pure C and
# also built by firmware/host_test.
idf_component_register(SRCS "svc_time.c" "tz_posix.c" "time_drift.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_event
                       PRIV_REQUIRES s3w_hal sys_core svc_settings svc_power svc_worker nvs_flash esp_timer log)
