idf_component_register(SRCS "svc_storage.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_event
                       PRIV_REQUIRES s3w_hal sys_core svc_worker joltwallet__littlefs fatfs log)
