#include "svc_diag.h"

#include "esp_check.h"
#include "esp_console.h"
#include "esp_log.h"
#include "svc_diag_priv.h"

static const char *TAG = "svc_diag";

esp_err_t svc_diag_console_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "s3w>";
    repl_cfg.max_cmdline_length = 256;
    repl_cfg.task_stack_size = 6144;

    const esp_console_dev_usb_serial_jtag_config_t dev_cfg = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_console_new_repl_usb_serial_jtag(&dev_cfg, &repl_cfg, &repl), TAG, "repl");

    ESP_RETURN_ON_ERROR(esp_console_register_help_command(), TAG, "help");
    ESP_RETURN_ON_ERROR(diag_register_i2c(), TAG, "i2c");
    ESP_RETURN_ON_ERROR(diag_register_lcd(), TAG, "lcd");
    ESP_RETURN_ON_ERROR(diag_register_touch(), TAG, "touch");
    ESP_RETURN_ON_ERROR(diag_register_rtc(), TAG, "rtc");
    ESP_RETURN_ON_ERROR(diag_register_time(), TAG, "time");
    ESP_RETURN_ON_ERROR(diag_register_pmu(), TAG, "pmu");
    ESP_RETURN_ON_ERROR(diag_register_imu(), TAG, "imu");
    ESP_RETURN_ON_ERROR(diag_register_audio(), TAG, "audio");
    ESP_RETURN_ON_ERROR(diag_register_fs(), TAG, "fs");
    ESP_RETURN_ON_ERROR(diag_register_factory(), TAG, "factory");
    ESP_RETURN_ON_ERROR(diag_register_ui(), TAG, "ui");
    ESP_RETURN_ON_ERROR(diag_register_face(), TAG, "face");
    ESP_RETURN_ON_ERROR(diag_register_settings(), TAG, "settings");
    ESP_RETURN_ON_ERROR(diag_register_power(), TAG, "power");
    ESP_RETURN_ON_ERROR(diag_register_input(), TAG, "input");
    ESP_RETURN_ON_ERROR(diag_register_sensors(), TAG, "sensors");
    ESP_RETURN_ON_ERROR(diag_register_sys(), TAG, "sys");
    ESP_RETURN_ON_ERROR(diag_register_crash(), TAG, "crash");
    ESP_RETURN_ON_ERROR(diag_register_metrics(), TAG, "metrics");
    ESP_RETURN_ON_ERROR(diag_register_alarm(), TAG, "alarm");
    ESP_RETURN_ON_ERROR(diag_register_modes(), TAG, "modes");

    return esp_console_start_repl(repl);
}
