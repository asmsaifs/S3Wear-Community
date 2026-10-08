# Wi-Fi station, saved networks, SNTP (docs/02 §7 "Wi-Fi", P9-01). wifi_nets.c is pure C and also
# built by firmware/host_test.
idf_component_register(SRCS "svc_wifi.c" "wifi_link.c" "wifi_nets.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_event
                       PRIV_REQUIRES esp_wifi esp_netif lwip esp_timer nvs_flash svc_link svc_settings svc_time
                                     svc_worker proto nanopb sys_core heap mbedtls log)
