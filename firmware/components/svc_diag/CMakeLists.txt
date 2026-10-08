# Diagnostics: esp_console over USB-Serial-JTAG (docs/02-firmware-architecture.md §11).
# Commands for Pro-only services (docs/10 §3). REQUIRES cannot depend on the configuration (IDF).
set(srcs "svc_diag.c" "cmd_i2c.c" "cmd_lcd.c" "cmd_touch.c" "cmd_rtc.c" "cmd_time.c" "cmd_pmu.c" "cmd_imu.c" "cmd_audio.c" "cmd_fs.c" "cmd_factory.c" "cmd_ui.c" "cmd_face.c" "cmd_settings.c" "cmd_power.c" "cmd_input.c" "cmd_sensors.c" "cmd_activity.c" "cmd_sys.c" "cmd_crash.c" "cmd_metrics.c" "cmd_alarm.c" "cmd_modes.c" "cmd_ble.c" "metrics_ring.c" "svc_diag_metrics.c")
if(CONFIG_S3W_EDITION_PRO)
    list(APPEND srcs "cmd_notify.c" "cmd_find.c" "cmd_memo.c" "cmd_screenshot.c" "cmd_media.c" "cmd_weather.c" "cmd_wifi.c" "cmd_ha.c" "cmd_calendar.c" "cmd_call.c" "cmd_license.c" "cmd_app.c")
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES console app_runtime svc_alarm svc_notify svc_find svc_memo svc_screenshot svc_media svc_weather svc_wifi svc_ha svc_calendar svc_call svc_license svc_audio svc_ble svc_link svc_modes svc_settings svc_power svc_input svc_sensors svc_activity svc_time esp_pm bsp_s3w drv_audio svc_storage factory_test sys_core heap ui_framework watchfaces lvgl esp_lcd esp_timer esp_event log espcoredump esp_partition mbedtls nvs_flash svc_worker esp_system freertos)
