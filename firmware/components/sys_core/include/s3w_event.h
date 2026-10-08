// S3Wear event bus (docs/02-firmware-architecture.md §4): a dedicated esp_event loop
// (core 0, prio 11) with copied payloads of at most S3W_EVENT_PAYLOAD_MAX bytes, plus
// UI-side subscriptions whose callbacks run on the UI task (safe to touch LVGL).
//
// Declare event bases in the owning service's svc_<name>_events.h:
//   ESP_EVENT_DECLARE_BASE(S3W_EVT_POWER);
//   enum { POWER_EVT_BATTERY_CHANGED, ... };
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_event.h"
#include "s3w_mailbox.h"

#ifdef __cplusplus
extern "C" {
#endif

#define S3W_EVENT_PAYLOAD_MAX S3W_MBOX_PAYLOAD_MAX

/** data is a copy, valid only during the call; len is what the poster passed. */
typedef void (*s3w_event_cb_t)(void *ctx, esp_event_base_t base, int32_t id, const void *data, size_t len);

typedef struct s3w_event_sub *s3w_event_sub_t;

esp_err_t s3w_event_init(void);

/**
 * Publish from any task (not from an ISR). Waits up to 10 ms for queue space, then
 * returns ESP_ERR_TIMEOUT. Payloads larger than S3W_EVENT_PAYLOAD_MAX are rejected:
 * put big data in a store and post an id.
 */
esp_err_t s3w_event_post(esp_event_base_t base, int32_t id, const void *data, size_t len);

/** Callback runs on the event-bus task. id may be ESP_EVENT_ANY_ID. out may be NULL. */
esp_err_t s3w_event_subscribe(esp_event_base_t base, int32_t id, s3w_event_cb_t cb, void *ctx,
                              s3w_event_sub_t *out);

/** Callback runs on the UI task, in post order. */
esp_err_t s3w_ui_subscribe(esp_event_base_t base, int32_t id, s3w_event_cb_t cb, void *ctx, s3w_event_sub_t *out);

/** After this returns no new deliveries start; UI deliveries already queued still run. */
esp_err_t s3w_event_unsubscribe(s3w_event_sub_t sub);

#ifdef __cplusplus
}
#endif
