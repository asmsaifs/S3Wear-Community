#include "svc_settings.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "svc_worker.h"
#include "sys_core.h"

static const char *TAG = "svc_settings";

#define NVS_NAMESPACE "s3w_set"

ESP_EVENT_DEFINE_BASE(SVC_SETTINGS_EVENT);

static s3w_mutex_t s_lock;
static settings_store_t s_store;  // under s_lock
static bool s_flush_queued;       // under s_lock: a flush job is waiting on svc_worker
static nvs_handle_t s_nvs;        // 0 if NVS could not be opened: settings live in RAM only

// --- NVS backend (runs on svc_worker, or in svc_settings_init at boot) ---------------

static esp_err_t map_err(esp_err_t err)
{
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_ERR_NOT_FOUND : err;
}

static esp_err_t nvs_be_get_i32(void *ctx, const char *key, int32_t *out)
{
    (void)ctx;
    return map_err(nvs_get_i32(s_nvs, key, out));
}

static esp_err_t nvs_be_set_i32(void *ctx, const char *key, int32_t v)
{
    (void)ctx;
    return nvs_set_i32(s_nvs, key, v);
}

static esp_err_t nvs_be_get_str(void *ctx, const char *key, char *out, size_t *len)
{
    (void)ctx;
    return map_err(nvs_get_str(s_nvs, key, out, len));
}

static esp_err_t nvs_be_set_str(void *ctx, const char *key, const char *v)
{
    (void)ctx;
    return nvs_set_str(s_nvs, key, v);
}

static esp_err_t nvs_be_commit(void *ctx)
{
    (void)ctx;
    return nvs_commit(s_nvs);
}

static esp_err_t nvs_be_erase_all(void *ctx)
{
    (void)ctx;
    ESP_RETURN_ON_ERROR(nvs_erase_all(s_nvs), TAG, "erase");
    return nvs_commit(s_nvs);
}

static const settings_backend_t s_backend = {
    .get_i32 = nvs_be_get_i32,
    .set_i32 = nvs_be_set_i32,
    .get_str = nvs_be_get_str,
    .set_str = nvs_be_set_str,
    .commit = nvs_be_commit,
    .erase_all = nvs_be_erase_all,
};

// --- Worker jobs ----------------------------------------------------------------------

// Snapshot under the lock, write outside it, so getters never wait for flash.
static void flush_job(void *ctx)
{
    (void)ctx;
    static settings_store_t snap; // only svc_worker runs this
    s3w_mutex_lock(s_lock, UINT32_MAX);
    snap = s_store;
    memset(s_store.dirty, 0, sizeof s_store.dirty);
    s_flush_queued = false;
    s3w_mutex_unlock(s_lock);

    if (!s_nvs) {
        return;
    }
    const esp_err_t err = settings_store_flush(&snap, &s_backend);
    if (err != ESP_OK) {
        // Keep the failed keys dirty; the next set retries them.
        s3w_mutex_lock(s_lock, UINT32_MAX);
        for (int w = 0; w < S3W_SETTINGS_DIRTY_WORDS; w++) {
            s_store.dirty[w] |= snap.dirty[w];
        }
        s3w_mutex_unlock(s_lock);
        ESP_LOGE(TAG, "NVS write failed: %s", esp_err_to_name(err));
    }
}

static void erase_job(void *ctx)
{
    (void)ctx;
    if (s_nvs) {
        const esp_err_t err = s_backend.erase_all(s_backend.ctx);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "NVS erase failed: %s", esp_err_to_name(err));
        }
    }
}

// Call without s_lock held: svc_worker_submit may wait up to 100 ms for queue space.
static void queue_flush(void)
{
    s3w_mutex_lock(s_lock, UINT32_MAX);
    const bool submit = !s_flush_queued && settings_store_any_dirty(&s_store);
    s_flush_queued |= submit;
    s3w_mutex_unlock(s_lock);
    if (submit && svc_worker_submit(flush_job, NULL) != ESP_OK) {
        s3w_mutex_lock(s_lock, UINT32_MAX);
        s_flush_queued = false;
        s3w_mutex_unlock(s_lock);
        ESP_LOGW(TAG, "worker queue full: write deferred to the next change");
    }
}

static esp_err_t finish_set(s3w_setting_t id, esp_err_t err, bool changed)
{
    if (err == ESP_OK && changed) {
        queue_flush();
        const svc_settings_evt_changed_t evt = {.id = (uint16_t)id};
        s3w_event_post(SVC_SETTINGS_EVENT, SVC_SETTINGS_EVT_CHANGED, &evt, sizeof evt);
        ESP_LOGD(TAG, "%s changed", settings_info(id)->key);
    }
    return err;
}

// --- API ------------------------------------------------------------------------------

esp_err_t svc_settings_init(void)
{
    s_lock = s3w_mutex_create();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "mutex");
    settings_store_defaults(&s_store);

    // NVS also holds PHY calibration and BLE bonds; a full or newer-format partition is
    // erased so the watch still boots (calibration is regenerated, bonds are lost).
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS %s: erasing", esp_err_to_name(err));
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs erase");
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs init (settings use defaults)");
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs), TAG, "nvs open (settings use defaults)");

    // Boot, before any other task uses settings: load directly.
    const int invalid = settings_store_load(&s_store, &s_backend);
    if (invalid) {
        ESP_LOGW(TAG, "%d stored settings invalid: defaults restored", invalid);
        queue_flush();
    }
    ESP_LOGI(TAG, "%d settings loaded", S3W_SETTING_COUNT);
    return ESP_OK;
}

int32_t svc_settings_get_int(s3w_setting_t id)
{
    s3w_mutex_lock(s_lock, UINT32_MAX);
    const int32_t v = settings_store_get_int(&s_store, id);
    s3w_mutex_unlock(s_lock);
    return v;
}

bool svc_settings_get_bool(s3w_setting_t id)
{
    s3w_mutex_lock(s_lock, UINT32_MAX);
    const bool v = settings_store_get_bool(&s_store, id);
    s3w_mutex_unlock(s_lock);
    return v;
}

esp_err_t svc_settings_get_str(s3w_setting_t id, char *buf, size_t len)
{
    const s3w_setting_info_t *in = settings_info(id);
    ESP_RETURN_ON_FALSE(in && in->type == S3W_SETTING_TYPE_STR && buf && len, ESP_ERR_INVALID_ARG, TAG, "get_str");
    esp_err_t err = ESP_OK;
    s3w_mutex_lock(s_lock, UINT32_MAX);
    const char *v = settings_store_get_str(&s_store, id);
    if (strlen(v) < len) {
        strcpy(buf, v);
    } else {
        buf[0] = '\0';
        err = ESP_ERR_INVALID_SIZE;
    }
    s3w_mutex_unlock(s_lock);
    return err;
}

esp_err_t svc_settings_set_int(s3w_setting_t id, int32_t v)
{
    bool changed;
    s3w_mutex_lock(s_lock, UINT32_MAX);
    const esp_err_t err = settings_store_set_int(&s_store, id, v, &changed);
    s3w_mutex_unlock(s_lock);
    return finish_set(id, err, changed);
}

esp_err_t svc_settings_set_bool(s3w_setting_t id, bool v)
{
    bool changed;
    s3w_mutex_lock(s_lock, UINT32_MAX);
    const esp_err_t err = settings_store_set_bool(&s_store, id, v, &changed);
    s3w_mutex_unlock(s_lock);
    return finish_set(id, err, changed);
}

esp_err_t svc_settings_set_str(s3w_setting_t id, const char *v)
{
    bool changed;
    s3w_mutex_lock(s_lock, UINT32_MAX);
    const esp_err_t err = settings_store_set_str(&s_store, id, v, &changed);
    s3w_mutex_unlock(s_lock);
    return finish_set(id, err, changed);
}

esp_err_t svc_settings_reset(s3w_setting_t id)
{
    const s3w_setting_info_t *in = settings_info(id);
    ESP_RETURN_ON_FALSE(in, ESP_ERR_INVALID_ARG, TAG, "reset");
    switch (in->type) {
    case S3W_SETTING_TYPE_INT:
        return svc_settings_set_int(id, in->def);
    case S3W_SETTING_TYPE_BOOL:
        return svc_settings_set_bool(id, in->def != 0);
    default:
        return svc_settings_set_str(id, in->def_str);
    }
}

esp_err_t svc_settings_factory_reset(void)
{
    // Defaults in RAM now (nothing dirty); the erase runs on svc_worker after any write
    // already queued, so a stale value can never land after it.
    s3w_mutex_lock(s_lock, UINT32_MAX);
    settings_store_defaults(&s_store);
    s3w_mutex_unlock(s_lock);
    ESP_RETURN_ON_ERROR(svc_worker_submit(erase_job, NULL), TAG, "queue erase");
    ESP_LOGW(TAG, "factory reset: all settings at defaults");
    return s3w_event_post(SVC_SETTINGS_EVENT, SVC_SETTINGS_EVT_RESET, NULL, 0);
}

int svc_settings_pending(void)
{
    int n = 0;
    s3w_mutex_lock(s_lock, UINT32_MAX);
    for (int i = 0; i < S3W_SETTING_COUNT; i++) {
        n += settings_store_is_dirty(&s_store, (s3w_setting_t)i);
    }
    s3w_mutex_unlock(s_lock);
    return n;
}
