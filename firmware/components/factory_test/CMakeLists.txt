# ft_result.c is pure C and also built by firmware/host_test.
idf_component_register(SRCS "factory_test.c" "ft_radio.c" "ft_result.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES bsp_s3w svc_sensors drv_audio svc_storage ui_framework lvgl bt esp_wifi esp_netif
                                     esp_event esp_timer esp_app_format heap log)
