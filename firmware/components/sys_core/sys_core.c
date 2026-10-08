#include "sys_core.h"

#include "esp_check.h"

static const char *TAG = "sys_core";

esp_err_t sys_core_init(void)
{
    s3w_log_init();
    ESP_RETURN_ON_ERROR(s3w_ui_init(), TAG, "ui mailbox");
    return s3w_event_init();
}
