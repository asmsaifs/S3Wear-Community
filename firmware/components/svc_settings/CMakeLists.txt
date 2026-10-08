# settings_store.c is pure C and also built by firmware/host_test.
idf_component_register(SRCS "svc_settings.c" "settings_store.c"
                       INCLUDE_DIRS "include"
                       REQUIRES sys_core esp_event
                       PRIV_REQUIRES nvs_flash svc_worker log)
