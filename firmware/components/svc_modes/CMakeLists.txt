# Modes service: DND, sleep and theater modes with schedules (docs/03 F2/F4/F7).
# modes.c is pure C and also built by firmware/host_test and the simulator.
idf_component_register(SRCS "svc_modes.c" "modes.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_event
                       PRIV_REQUIRES sys_core svc_settings svc_time esp_timer log)
