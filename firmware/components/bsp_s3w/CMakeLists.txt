idf_component_register(SRCS "bsp_s3w.c" "bsp_buttons.c" "bsp_display.c" "bsp_touch.c" "bsp_rtc.c" "bsp_pmu.c" "bsp_imu.c" "bsp_audio.c" "bsp_sdcard.c" "bsp_sleep.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_i2c esp_driver_gpio esp_lcd drv_ft3168 drv_pcf85063 drv_axp2101 drv_qmi8658 esp_event sdmmc
                       PRIV_REQUIRES log esp_timer esp_driver_spi drv_co5300 drv_audio fatfs esp_driver_sdspi)
