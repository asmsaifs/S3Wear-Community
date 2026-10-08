# System sounds service (docs/03 F15, P3-11). audio_mix.c is pure C and also built by
# firmware/host_test.
idf_component_register(SRCS "svc_audio.c" "audio_mix.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_event drv_audio
                       PRIV_REQUIRES sys_core svc_settings svc_modes svc_power bsp_s3w esp_timer log)
