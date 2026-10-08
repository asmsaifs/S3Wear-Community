# s3w_mailbox.c is pure C and also built by firmware/host_test.
idf_component_register(SRCS "sys_core.c" "s3w_event.c" "s3w_ui.c" "s3w_mailbox.c" "s3w_task.c" "s3w_log.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_event freertos
                       PRIV_REQUIRES heap log)
