# qmi8658_codec.c is pure C and also built by firmware/host_test.
idf_component_register(SRCS "drv_qmi8658.c" "qmi8658_codec.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_i2c
                       PRIV_REQUIRES esp_timer log)
