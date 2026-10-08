idf_component_register(SRCS "app_main.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES bsp_s3w s3w_hal ui_framework lvgl svc_diag svc_storage svc_worker sys_core esp_app_format esp_event svc_settings svc_power svc_input svc_sensors svc_modes svc_time svc_alarm svc_audio watchfaces ui_apps)
