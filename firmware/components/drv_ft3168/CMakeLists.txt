idf_component_register(SRCS "drv_ft3168.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_i2c esp_lcd
                       PRIV_REQUIRES espressif__esp_lcd_touch_ft5x06 log)
