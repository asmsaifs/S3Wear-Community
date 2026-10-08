# audio_dsp.c is pure C and also built by firmware/host_test.
idf_component_register(SRCS "drv_audio.c" "audio_dsp.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_i2c esp_driver_gpio
                       PRIV_REQUIRES esp_driver_i2s espressif__esp_codec_dev heap log)
