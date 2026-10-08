// DeviceStatus (docs/06 §4): sent after each TimeSync and on every battery change
// (SVC_POWER_EVT_BATTERY: a % step, charger or USB plugged / unplugged) while the link is up.
// Event-driven: no timer.
#include <stdlib.h>
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "link_status.h"
#include "s3w_event.h"
#include "svc_link.h"
#include "svc_power.h"
#include "svc_storage.h"
#include "svc_time.h"

static const char *TAG = "link_status";

esp_err_t link_status_send(void)
{
    ESP_RETURN_ON_FALSE(svc_link_is_up(), ESP_ERR_INVALID_STATE, TAG, "link down");
    // Heap, not stack or a static: an Envelope is over 1 KB and callers run on different tasks.
    s3w_v1_Envelope *env = calloc(1, sizeof *env);
    ESP_RETURN_ON_FALSE(env, ESP_ERR_NO_MEM, TAG, "heap");
    env->which_body = s3w_v1_Envelope_device_status_tag;
    s3w_v1_DeviceStatus *d = &env->body.device_status;
    svc_power_battery_t b;
    if (svc_power_battery(&b) == ESP_OK) {
        d->battery_pct = b.percent >= 0 ? (uint32_t)b.percent : 0;
        d->charging = b.charging;
        d->usb_power = b.vbus;
    }
    uint64_t total = 0, used = 0;
    if (svc_storage_usage(SVC_STORAGE_FLASH_PATH, &total, &used) == ESP_OK) {
        d->storage_total_kb = (uint32_t)(total / 1024);
        d->storage_free_kb = (uint32_t)((total - used) / 1024);
    }
    d->time_valid = svc_time_is_valid();
    const esp_err_t err = svc_link_send(env);
    free(env);
    return err;
}

static void on_battery(void *ctx, esp_event_base_t base, int32_t id, const void *data, size_t len)
{
    (void)ctx;
    (void)base;
    (void)id;
    (void)data;
    (void)len;
    if (svc_link_is_up()) {
        link_status_send();
    }
}

esp_err_t link_status_register(void)
{
    return s3w_event_subscribe(SVC_POWER_EVENT, SVC_POWER_EVT_BATTERY, on_battery, NULL, NULL);
}
