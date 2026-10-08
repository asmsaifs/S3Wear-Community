# BLE service: NimBLE GAP/GATT, pairing, bonds, BAS + DIS (docs/06 §1, §8; P4-01).
idf_component_register(SRCS "svc_ble.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_event
                       PRIV_REQUIRES bt sys_core svc_power svc_settings esp_app_format esp_timer log)
