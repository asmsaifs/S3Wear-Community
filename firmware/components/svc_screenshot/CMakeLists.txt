# Screenshots of the watch's own screen (P8-17). png_enc.c is pure C and also built by
# firmware/host_test.
idf_component_register(SRCS "svc_screenshot.c" "png_enc.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES ui_framework svc_storage svc_time svc_link svc_worker s3w_crypto proto lvgl heap log)
