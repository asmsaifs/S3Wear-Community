# Input service (docs/02-firmware-architecture.md §7). button_gesture.c and
# palm_detect.c are pure C and also built by firmware/host_test.
idf_component_register(SRCS "svc_input.c" "button_gesture.c" "palm_detect.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_event
                       PRIV_REQUIRES s3w_hal sys_core svc_power esp_timer log)
