// TimeSync from the companion (docs/06 §4): time (a drift reference), POSIX zone and 12/24 h.
// Runs on the svc_link task: one short RTC read; the settings writes go to the worker.
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "link_status.h"
#include "link_time.h"
#include "svc_link.h"
#include "svc_time.h"

static const char *TAG = "link_time";

// Uptime (s, monotonic: wall-clock steps do not move it) of the last good sync; 0 = none.
// One 32-bit word: written on the link task, read from any task without a lock.
static volatile uint32_t s_sync_s;

static uint32_t uptime_s(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000) + 1; // + 1: never 0
}

static int32_t apply(const s3w_v1_TimeSync *t)
{
    // Zone first: a zone the watch cannot parse refuses the sync before the clock changes.
    if (t->tz_posix[0] != '\0' && svc_time_set_tz(t->tz_posix) != ESP_OK) {
        ESP_LOGW(TAG, "bad zone \"%s\" (%s)", t->tz_posix, t->tz_name);
        return s3w_v1_StatusCode_STATUS_INVALID;
    }
    if (svc_time_set_utc_ms(t->unix_ms, SVC_TIME_SRC_PHONE) != ESP_OK) {
        ESP_LOGW(TAG, "bad time %lld", (long long)t->unix_ms);
        return s3w_v1_StatusCode_STATUS_INVALID;
    }
    s_sync_s = uptime_s(); // before the SVC_TIME_EVT_CHANGED it posted reaches the UI
    if (svc_time_set_24h(t->is_24h) != ESP_OK) {
        return s3w_v1_StatusCode_STATUS_INTERNAL;
    }
    ESP_LOGI(TAG, "time set from phone: %s (%s), %s", t->tz_name, t->tz_posix, t->is_24h ? "24 h" : "12 h");
    return s3w_v1_StatusCode_STATUS_OK;
}

static void on_time_sync(void *ctx, const s3w_v1_Envelope *req)
{
    (void)ctx;
    const int32_t code = apply(&req->body.time_sync);
    if (req->id != 0) {
        svc_link_reply_status(req, code, NULL);
    }
    if (code == s3w_v1_StatusCode_STATUS_OK) {
        // Session start (docs/06 §4): TimeSync → DeviceStatus.
        link_status_send();
    }
}

esp_err_t link_time_register(void)
{
    return svc_link_register(s3w_v1_Envelope_time_sync_tag, on_time_sync, NULL);
}

int32_t link_time_sync_age_s(void)
{
    const uint32_t t = s_sync_s;
    return t == 0 ? -1 : (int32_t)(uptime_s() - t);
}
