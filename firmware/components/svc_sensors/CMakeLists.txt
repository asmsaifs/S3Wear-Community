# Sensor service (docs/02-firmware-architecture.md §7, docs/03 F2). raise_detect.c and
# flip_detect.c are pure C and also built by firmware/host_test.
idf_component_register(SRCS "svc_sensors.c" "raise_detect.c" "flip_detect.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_event
                       PRIV_REQUIRES s3w_hal sys_core svc_modes svc_power svc_settings esp_timer log)
