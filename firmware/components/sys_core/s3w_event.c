#include "s3w_event.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "s3w_task.h"
#include "s3w_ui.h"

static const char *TAG = "s3w_event";

#define QUEUE_LEN       32
#define POST_TIMEOUT_MS 10

typedef struct {
    uint16_t len;
    uint8_t data[S3W_EVENT_PAYLOAD_MAX];
} envelope_t;

struct s3w_event_sub {
    esp_event_base_t base;
    int32_t id;
    s3w_event_cb_t cb;
    void *ctx;
    bool ui;
    esp_event_handler_instance_t instance;
};

static esp_event_loop_handle_t s_loop;

esp_err_t s3w_event_init(void)
{
    if (s_loop) {
        return ESP_OK;
    }
    const esp_event_loop_args_t args = {
        .queue_size = QUEUE_LEN,
        .task_name = "s3w_evt",
        .task_priority = S3W_PRIO_EVENTS,
        .task_stack_size = S3W_STACK_EVENTS,
        .task_core_id = S3W_CORE_SERVICES,
    };
    return esp_event_loop_create(&args, &s_loop);
}

esp_err_t s3w_event_post(esp_event_base_t base, int32_t id, const void *data, size_t len)
{
    ESP_RETURN_ON_FALSE(s_loop, ESP_ERR_INVALID_STATE, TAG, "bus not initialised");
    ESP_RETURN_ON_FALSE(len <= S3W_EVENT_PAYLOAD_MAX && (data || !len), ESP_ERR_INVALID_SIZE, TAG,
                        "%s:%ld payload %u B", base, (long)id, (unsigned)len);
    envelope_t env;
    env.len = (uint16_t)len;
    if (len) {
        memcpy(env.data, data, len);
    }
    return esp_event_post_to(s_loop, base, id, &env, offsetof(envelope_t, data) + len,
                             pdMS_TO_TICKS(POST_TIMEOUT_MS));
}

static void ui_deliver(void *ctx, int32_t id, const void *data, size_t len)
{
    s3w_event_sub_t sub = ctx;
    sub->cb(sub->ctx, sub->base, id, data, len);
}

static void ui_free_sub(void *ctx)
{
    free(ctx); // queued behind every delivery that was already in the mailbox
}

static void bus_handler(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    s3w_event_sub_t sub = arg;
    const envelope_t *env = event_data;
    if (sub->ui) {
        s3w_ui_post_data(ui_deliver, sub, id, env->data, env->len);
    } else {
        sub->cb(sub->ctx, base, id, env->data, env->len);
    }
}

static esp_err_t subscribe(esp_event_base_t base, int32_t id, s3w_event_cb_t cb, void *ctx, bool ui,
                           s3w_event_sub_t *out)
{
    ESP_RETURN_ON_FALSE(s_loop, ESP_ERR_INVALID_STATE, TAG, "bus not initialised");
    ESP_RETURN_ON_FALSE(cb, ESP_ERR_INVALID_ARG, TAG, "cb");
    s3w_event_sub_t sub = calloc(1, sizeof *sub);
    ESP_RETURN_ON_FALSE(sub, ESP_ERR_NO_MEM, TAG, "sub");
    *sub = (struct s3w_event_sub){.base = base, .id = id, .cb = cb, .ctx = ctx, .ui = ui};
    esp_err_t err = esp_event_handler_instance_register_with(s_loop, base, id, bus_handler, sub, &sub->instance);
    if (err != ESP_OK) {
        free(sub);
        return err;
    }
    if (out) {
        *out = sub;
    }
    return ESP_OK;
}

esp_err_t s3w_event_subscribe(esp_event_base_t base, int32_t id, s3w_event_cb_t cb, void *ctx,
                              s3w_event_sub_t *out)
{
    return subscribe(base, id, cb, ctx, false, out);
}

esp_err_t s3w_ui_subscribe(esp_event_base_t base, int32_t id, s3w_event_cb_t cb, void *ctx, s3w_event_sub_t *out)
{
    return subscribe(base, id, cb, ctx, true, out);
}

esp_err_t s3w_event_unsubscribe(s3w_event_sub_t sub)
{
    ESP_RETURN_ON_FALSE(sub, ESP_ERR_INVALID_ARG, TAG, "sub");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_unregister_with(s_loop, sub->base, sub->id, sub->instance), TAG,
                        "unregister");
    // UI deliveries may still sit in the mailbox: free behind them, in order.
    if (sub->ui && s3w_ui_post(ui_free_sub, sub) == ESP_OK) {
        return ESP_OK;
    }
    free(sub);
    return ESP_OK;
}
