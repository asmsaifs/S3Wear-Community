# Activity: steps, distance, kcal, active minutes, goals (docs/03 F12, docs/02 §7 "Activity",
# P5-01). step_detect.c and activity_day.c are pure C and also built by firmware/host_test.
idf_component_register(SRCS "svc_activity.c" "step_detect.c" "activity_day.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_event
                       PRIV_REQUIRES svc_sensors svc_settings svc_power svc_time svc_worker sys_core nvs_flash
                                     esp_timer esp_rom heap log)
