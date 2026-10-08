idf_component_register(SRCS "drv_co5300.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_lcd
                       PRIV_REQUIRES esp_driver_gpio log)
