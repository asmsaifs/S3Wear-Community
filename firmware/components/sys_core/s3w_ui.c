#include "s3w_ui.h"

#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "s3w_ui";

// 32 x ~140 B = 4.5 KB -> PSRAM (CLAUDE.md: > 4 KB goes to PSRAM).
#define MBOX_SLOTS 32

static s3w_mbox_t s_mbox;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_ui_task;

esp_err_t s3w_ui_init(void)
{
    if (s_mbox.slots) {
        return ESP_OK;
    }
    s3w_mbox_msg_t *slots = heap_caps_calloc(MBOX_SLOTS, sizeof *slots, MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(slots, ESP_ERR_NO_MEM, TAG, "mailbox");
    s3w_mbox_init(&s_mbox, slots, MBOX_SLOTS);
    return ESP_OK;
}

void s3w_ui_bind_task(TaskHandle_t ui_task)
{
    s_ui_task = ui_task;
}

esp_err_t s3w_ui_post_data(s3w_mbox_fn_t fn, void *ctx, int32_t arg, const void *data, size_t len)
{
    ESP_RETURN_ON_FALSE(s_mbox.slots, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    portENTER_CRITICAL(&s_lock);
    const bool ok = s3w_mbox_push(&s_mbox, fn, ctx, arg, data, len);
    portEXIT_CRITICAL(&s_lock);
    if (!ok) {
        ESP_LOGW(TAG, "mailbox full or bad post, dropped %lu", (unsigned long)s_mbox.dropped);
        return ESP_ERR_NO_MEM;
    }
    if (s_ui_task) {
        xTaskNotifyGiveIndexed(s_ui_task, S3W_UI_NOTIFY_INDEX);
    }
    return ESP_OK;
}

static void call_simple(void *ctx, int32_t arg, const void *data, size_t len)
{
    (void)arg;
    (void)len;
    void (*fn)(void *);
    memcpy(&fn, data, sizeof fn);
    fn(ctx);
}

esp_err_t s3w_ui_post(void (*fn)(void *ctx), void *ctx)
{
    ESP_RETURN_ON_FALSE(fn, ESP_ERR_INVALID_ARG, TAG, "fn");
    return s3w_ui_post_data(call_simple, ctx, 0, &fn, sizeof fn);
}

uint32_t s3w_ui_drain(void)
{
    uint32_t n = 0;
    s3w_mbox_msg_t msg; // on the UI task stack (~140 B)
    for (;;) {
        portENTER_CRITICAL(&s_lock);
        const bool got = s_mbox.slots && s3w_mbox_pop(&s_mbox, &msg);
        portEXIT_CRITICAL(&s_lock);
        if (!got) {
            return n;
        }
        msg.fn(msg.ctx, msg.arg, msg.data, msg.len);
        n++;
    }
}

bool s3w_ui_is_ui_task(void)
{
    return s_ui_task && xTaskGetCurrentTaskHandle() == s_ui_task;
}

void s3w_ui_stats(uint16_t *high_water, uint32_t *dropped)
{
    portENTER_CRITICAL(&s_lock);
    *high_water = s_mbox.high_water;
    *dropped = s_mbox.dropped;
    portEXIT_CRITICAL(&s_lock);
}
