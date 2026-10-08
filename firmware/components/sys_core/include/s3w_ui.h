// UI mailbox: the only way for non-UI tasks to get work done on the UI (LVGL) task
// besides lv_lock()/lv_unlock() (CLAUDE.md "LVGL is single-threaded").
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "s3w_mailbox.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Notification index the UI task waits on (index 0 belongs to LVGL's thread sync). */
#define S3W_UI_NOTIFY_INDEX 1

esp_err_t s3w_ui_init(void);

/** Called once by the UI task itself; posts then wake it via S3W_UI_NOTIFY_INDEX. */
void s3w_ui_bind_task(TaskHandle_t ui_task);

/** Run fn(ctx) on the UI task. Any task, not ISR. ESP_ERR_NO_MEM if the mailbox is full. */
esp_err_t s3w_ui_post(void (*fn)(void *ctx), void *ctx);

/** Run fn(ctx, arg, copy-of-data, len) on the UI task; len <= S3W_MBOX_PAYLOAD_MAX. */
esp_err_t s3w_ui_post_data(s3w_mbox_fn_t fn, void *ctx, int32_t arg, const void *data, size_t len);

/** UI task only: run everything queued so far, in order. Returns how many ran. */
uint32_t s3w_ui_drain(void);

bool s3w_ui_is_ui_task(void);

void s3w_ui_stats(uint16_t *high_water, uint32_t *dropped);

#ifdef __cplusplus
}
#endif
