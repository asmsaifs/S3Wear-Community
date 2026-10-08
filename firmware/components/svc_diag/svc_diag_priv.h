#pragma once

#include <stdbool.h>

#include "esp_err.h"

// One registration function per command group (cmd_<group>.c).
esp_err_t diag_register_i2c(void);
esp_err_t diag_register_lcd(void);
esp_err_t diag_register_touch(void);
esp_err_t diag_register_rtc(void);
esp_err_t diag_register_time(void);
esp_err_t diag_register_pmu(void);
esp_err_t diag_register_imu(void);
esp_err_t diag_register_audio(void);
esp_err_t diag_register_fs(void);
esp_err_t diag_register_factory(void);
esp_err_t diag_register_ui(void);
esp_err_t diag_register_face(void);
esp_err_t diag_register_settings(void);
esp_err_t diag_register_power(void);
esp_err_t diag_register_input(void);
esp_err_t diag_register_sensors(void);
esp_err_t diag_register_sys(void);
esp_err_t diag_register_crash(void);
esp_err_t diag_register_metrics(void);
esp_err_t diag_register_alarm(void);
esp_err_t diag_register_modes(void);

// Panel sleep-out + display-on (true) or display-off + sleep-in (false).
esp_err_t diag_panel_power(bool on);
