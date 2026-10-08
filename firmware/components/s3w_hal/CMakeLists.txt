# HAL interfaces (include/) and their watch implementation over bsp_s3w (s3w/).
# The simulator builds the same headers with simulator/hal_sim/ instead.
idf_component_register(SRCS "s3w/hal_display_s3w.c" "s3w/hal_input_s3w.c" "s3w/hal_rtc_s3w.c"
                            "s3w/hal_pmu_s3w.c" "s3w/hal_storage_s3w.c" "s3w/hal_power_s3w.c"
                            "s3w/hal_imu_s3w.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES bsp_s3w esp_event log)
