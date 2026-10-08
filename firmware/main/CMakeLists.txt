# Pro-only glue (docs/10 §3): built when CONFIG_S3W_EDITION_PRO is on. REQUIRES cannot depend on
# the configuration (IDF), so the Pro services stay listed in both editions.
set(srcs "app_main.c" "link_hello.c" "link_status.c" "link_time.c" "connect_ui.c" "activity_ui.c")
if(CONFIG_S3W_EDITION_PRO)
    list(APPEND srcs "notif_ui.c" "find_ui.c" "memo_ui.c" "media_ui.c" "weather_ui.c" "calendar_ui.c" "call_ui.c"
                     "ha_ui.c" "apps_link.c" "apps_ui.c")
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES bsp_s3w s3w_hal ui_framework lvgl svc_diag svc_storage svc_worker sys_core esp_app_format esp_event svc_settings svc_power svc_input svc_sensors svc_activity svc_modes svc_time svc_alarm svc_audio svc_ble svc_link svc_notify svc_find svc_memo svc_screenshot svc_media svc_weather svc_wifi svc_ha svc_calendar svc_call svc_license app_runtime s3w_crypto watchfaces ui_apps esp_timer)
