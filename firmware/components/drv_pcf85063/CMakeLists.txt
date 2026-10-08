# pcf85063_codec.c is pure C and also built by firmware/host_test.
idf_component_register(SRCS "drv_pcf85063.c" "pcf85063_codec.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_i2c
                       PRIV_REQUIRES esp_driver_gpio log)
