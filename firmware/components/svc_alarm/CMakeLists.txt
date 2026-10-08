# Alarm service (docs/03-firmware-features.md F6, docs/02 §7). alarm_sched.c and
# timer_set.c are pure C and also built by firmware/host_test and the simulator.
idf_component_register(SRCS "svc_alarm.c" "alarm_sched.c" "timer_set.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_event svc_time
                       PRIV_REQUIRES s3w_hal sys_core svc_power svc_sensors svc_settings svc_audio nvs_flash esp_timer log)
