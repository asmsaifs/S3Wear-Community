// Persistent metrics ring: RAM copy guarded by a mutex, saved to NVS on svc_worker.
#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "s3w_event.h"
#include "svc_ble.h"
#include "svc_diag.h"
#include "svc_power.h"
#include "svc_worker.h"

static const char *TAG = "svc_diag_metrics";

#define NVS_NS  "s3w_diag"
#define NVS_KEY "metrics"
// Periodic save: 15 min bounds what a crash loses (heap minimum, drain) to 4 flash
// writes/hour. skip_unhandled_events keeps the timer from waking light sleep: a missed
// tick is simply merged into the next one the CPU is awake for.
#define FLUSH_PERIOD_US (15ll * 60 * 1000 * 1000)

static metrics_t s_m;
static StaticSemaphore_t s_mutex_buf;
static SemaphoreHandle_t s_mutex;
static bool s_started;
static uint32_t s_last_drain;

static void lock(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s_mutex);
}

static void note_heap_locked(void)
{
    metrics_note_heap(&s_m, (uint32_t)esp_get_minimum_free_heap_size(),
                      (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
}

static void save_job(void *ctx)
{
    (void)ctx;
    static metrics_t snap; // worker task only
    lock();
    note_heap_locked();
    metrics_seal(&s_m);
    snap = s_m;
    unlock();

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, NVS_KEY, &snap, sizeof(snap));
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "save failed: %s", esp_err_to_name(err));
    }
}

// Record the last 1 % drain step once per change; the periodic tick is the only sampler.
static void sample_drain(void)
{
    svc_power_stats_t st;
    svc_power_get_stats(&st);
    if (st.last_drain_ma_x10 && st.last_drain_ma_x10 != s_last_drain) {
        s_last_drain = st.last_drain_ma_x10;
        svc_diag_metric_record(METRIC_DRAIN, st.state, st.last_drain_ma_x10);
    }
}

// A BLE link went down: reason and how long it was up (docs/02 §11).
static void on_ble_disconnected(void *ctx, esp_event_base_t base, int32_t id, const void *data, size_t len)
{
    (void)ctx;
    (void)base;
    (void)id;
    if (len >= sizeof(svc_ble_evt_disconnected_t)) {
        const svc_ble_evt_disconnected_t *e = data;
        svc_diag_metric_record(METRIC_BLE_DISCONNECT, e->reason, e->seconds);
    }
}

static void flush_timer_cb(void *arg)
{
    (void)arg;
    sample_drain();
    svc_diag_metrics_flush();
}

esp_err_t svc_diag_metrics_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_buf);

    metrics_init(&s_m);
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        static metrics_t blob;
        size_t len = sizeof(blob);
        if (nvs_get_blob(h, NVS_KEY, &blob, &len) == ESP_OK && !metrics_load(&s_m, &blob, len)) {
            ESP_LOGW(TAG, "stored metrics invalid, starting over");
        }
        nvs_close(h);
    }
    metrics_note_boot(&s_m, (unsigned)esp_reset_reason());
    s_started = true;
    ESP_LOGI(TAG, "boot %lu, reset reason: %s", (unsigned long)s_m.boot_count,
             metrics_reset_name((unsigned)esp_reset_reason()));

    const esp_timer_create_args_t args = {
        .callback = flush_timer_cb,
        .name = "diag_metrics",
        .skip_unhandled_events = true,
    };
    esp_timer_handle_t t;
    ESP_RETURN_ON_ERROR(esp_timer_create(&args, &t), TAG, "timer");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(t, FLUSH_PERIOD_US), TAG, "timer start");
    ESP_RETURN_ON_ERROR(s3w_event_subscribe(SVC_BLE_EVENT, SVC_BLE_EVT_DISCONNECTED, on_ble_disconnected, NULL, NULL),
                        TAG, "ble");
    return svc_diag_metrics_flush();
}

void svc_diag_metric_record(metric_kind_t kind, uint32_t a, uint32_t b)
{
    if (!s_started) {
        return;
    }
    lock();
    metrics_push(&s_m, kind, a, b);
    unlock();
}

void svc_diag_metrics_get(metrics_t *out)
{
    if (!s_started) {
        metrics_init(out);
        return;
    }
    lock();
    note_heap_locked();
    *out = s_m;
    unlock();
}

esp_err_t svc_diag_metrics_flush(void)
{
    return svc_worker_submit(save_job, NULL);
}

void svc_diag_metrics_clear(void)
{
    if (!s_started) {
        return;
    }
    lock();
    metrics_init(&s_m);
    unlock();
    svc_diag_metrics_flush();
}
