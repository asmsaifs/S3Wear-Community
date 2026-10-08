// Time service: system time, zone, 12/24 h, time-valid flag, RTC drift calibration and
// system-clock discipline. See svc_time.h.
#include "svc_time.h"

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "hal_rtc.h"
#include "nvs.h"
#include "power_fsm.h"
#include "s3w_event.h"
#include "svc_power_events.h"
#include "svc_settings.h"
#include "svc_worker.h"
#include "tz_posix.h"

static const char *TAG = "svc_time";

ESP_EVENT_DEFINE_BASE(SVC_TIME_EVENT);

#define NVS_NS          "s3w_time"
#define NVS_KEY         "drift"
#define DRIFT_BLOB_VER  1
#define DEFAULT_TZ      "UTC0"
// RTC range (PCF85063 years 00..99 = 2000..2099).
#define UTC_MS_MIN      946684800000LL
#define UTC_MS_MAX      4102444800000LL
// Discipline: every 10 min bounds the RC slow-clock error between checks (a few hundred
// ppm -> < 0.3 s). skip_unhandled_events keeps the timer from waking light sleep: a
// missed tick runs at the next wake-up. Screen-on also checks, at most once a minute.
#define DISCIPLINE_PERIOD_US  (10LL * 60 * 1000 * 1000)
#define DISCIPLINE_MIN_GAP_US (60LL * 1000 * 1000)

typedef struct {
    uint8_t ver;
    uint8_t reserved[7];
    time_drift_t drift;
} drift_blob_t;

static StaticSemaphore_t s_mutex_buf;
static SemaphoreHandle_t s_mutex;
static bool s_started;
static bool s_valid;
static svc_time_source_t s_source;
static time_drift_t s_drift;
static tz_posix_t s_tz;
static char s_tz_str[64];
static char s_newlib_tz[TZ_POSIX_FIXED_MAX];
static int64_t s_next_transition = INT64_MAX;
static bool s_rtc_write_pending;
static int64_t s_last_discipline_us;
static esp_timer_handle_t s_rtc_write_timer; // one-shot: RTC write on a whole second
static esp_timer_handle_t s_zone_timer;      // one-shot: next DST transition (wakes light sleep, twice a year)
static esp_timer_handle_t s_discipline_timer;

static void lock(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s_mutex);
}

static int64_t now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static void set_system_ms(int64_t ms)
{
    const struct timeval tv = {.tv_sec = (time_t)(ms / 1000), .tv_usec = (suseconds_t)(ms % 1000) * 1000};
    settimeofday(&tv, NULL);
}

const char *svc_time_source_name(svc_time_source_t src)
{
    static const char *const names[] = {"none", "rtc", "phone", "sntp", "manual"};
    return (unsigned)src < sizeof names / sizeof names[0] ? names[src] : "?";
}

static void post_changed(uint8_t what)
{
    lock();
    const svc_time_evt_changed_t evt = {
        .what = what,
        .source = (uint8_t)s_source,
        .valid = s_valid,
        .h24 = svc_settings_get_bool(S3W_SETTING_TIME_24H),
        .utc_offset_s = tz_posix_offset(&s_tz, time(NULL), NULL),
    };
    unlock();
    if (s3w_event_post(SVC_TIME_EVENT, SVC_TIME_EVT_CHANGED, &evt, sizeof evt) != ESP_OK) {
        ESP_LOGW(TAG, "event dropped");
    }
}

// --- Drift state in NVS (worker) -------------------------------------------------------

static void save_job(void *ctx)
{
    (void)ctx;
    drift_blob_t blob = {.ver = DRIFT_BLOB_VER};
    lock();
    blob.drift = s_drift;
    unlock();
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, NVS_KEY, &blob, sizeof blob);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "drift save failed: %s", esp_err_to_name(err));
    }
}

static void save_drift(void)
{
    if (svc_worker_submit(save_job, NULL) != ESP_OK) {
        ESP_LOGW(TAG, "drift save not queued");
    }
}

static void load_drift(void)
{
    time_drift_init(&s_drift, 0);
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    drift_blob_t blob;
    size_t len = sizeof blob;
    if (nvs_get_blob(h, NVS_KEY, &blob, &len) == ESP_OK && len == sizeof blob && blob.ver == DRIFT_BLOB_VER) {
        time_drift_t clamped;
        time_drift_init(&clamped, blob.drift.steps);
        s_drift = blob.drift;
        s_drift.steps = clamped.steps;
    } else {
        ESP_LOGI(TAG, "no drift calibration stored");
    }
    nvs_close(h);
}

// --- Zone ----------------------------------------------------------------------------

// Give newlib the offset in force now and arm the timer for the next transition.
static void apply_zone_locked(void)
{
    const int64_t now = time(NULL);
    tz_posix_fixed_string(&s_tz, now, s_newlib_tz, sizeof s_newlib_tz);
    setenv("TZ", s_newlib_tz, 1);
    tzset();
    s_next_transition = tz_posix_next_transition(&s_tz, now);
    esp_timer_stop(s_zone_timer); // not running is fine
    if (s_next_transition != INT64_MAX) {
        esp_timer_start_once(s_zone_timer, (uint64_t)(s_next_transition - now) * 1000000ULL);
    }
}

static void zone_timer_cb(void *arg)
{
    (void)arg;
    lock();
    apply_zone_locked();
    ESP_LOGI(TAG, "DST transition: TZ %s", s_newlib_tz);
    unlock();
    post_changed(SVC_TIME_CHANGED_ZONE);
}

// Zone from the TIMEZONE setting; an invalid string keeps the zone in use (UTC at boot).
static void load_zone(void)
{
    char buf[sizeof s_tz_str];
    tz_posix_t tz;
    if (svc_settings_get_str(S3W_SETTING_TIMEZONE, buf, sizeof buf) != ESP_OK || !tz_posix_parse(buf, &tz)) {
        ESP_LOGW(TAG, "invalid TZ setting \"%s\", keeping %s", buf, s_tz_str[0] ? s_tz_str : DEFAULT_TZ);
        if (s_tz_str[0]) {
            return;
        }
        strcpy(buf, DEFAULT_TZ);
        tz_posix_parse(buf, &tz);
    }
    lock();
    s_tz = tz;
    strcpy(s_tz_str, buf);
    apply_zone_locked();
    unlock();
    ESP_LOGI(TAG, "zone %s (newlib %s)", s_tz_str, s_newlib_tz);
}

// --- RTC write on a whole second ------------------------------------------------------

static void rtc_write_cb(void *arg)
{
    (void)arg;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    const time_t sec = tv.tv_sec + (tv.tv_usec >= 500000 ? 1 : 0);
    const esp_err_t err = hal_rtc_set(sec);
    lock();
    s_rtc_write_pending = false;
    unlock();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "RTC write failed: %s", esp_err_to_name(err));
    }
}

static void schedule_rtc_write_locked(int64_t ms)
{
    esp_timer_stop(s_rtc_write_timer);
    s_rtc_write_pending = true;
    esp_timer_start_once(s_rtc_write_timer, (uint64_t)(1000 - ms % 1000) * 1000ULL);
}

// --- Discipline ------------------------------------------------------------------------

static int64_t discipline(void)
{
    lock();
    if (!s_valid || s_rtc_write_pending) {
        unlock();
        return 0;
    }
    s_last_discipline_us = esp_timer_get_time();
    time_t rtc = 0;
    bool rtc_valid = false;
    if (hal_rtc_get(&rtc, &rtc_valid) != ESP_OK) {
        unlock();
        return 0;
    }
    if (!rtc_valid) { // RTC lost power while we kept time: put it back
        ESP_LOGW(TAG, "RTC lost power: rewriting it from system time");
        time_drift_reset_window(&s_drift);
        schedule_rtc_write_locked(now_ms());
        unlock();
        save_drift();
        return 0;
    }
    const int64_t sys = now_ms();
    const int64_t corr = time_discipline_correction(&s_drift, sys, rtc);
    if (corr) {
        set_system_ms(sys + corr);
        apply_zone_locked();
    }
    unlock();
    if (corr) {
        ESP_LOGI(TAG, "system clock stepped %+lld ms to the RTC", (long long)corr);
        post_changed(SVC_TIME_CHANGED_CLOCK);
    }
    return corr;
}

static void discipline_timer_cb(void *arg)
{
    (void)arg;
    discipline();
}

static void on_power_state(void *ctx, esp_event_base_t base, int32_t id, const void *data, size_t len)
{
    (void)ctx;
    (void)base;
    (void)id;
    if (len < sizeof(svc_power_evt_state_t)) {
        return;
    }
    const svc_power_evt_state_t *st = data;
    if (st->state == POWER_STATE_ACTIVE && st->prev != POWER_STATE_DIM &&
        esp_timer_get_time() - s_last_discipline_us >= DISCIPLINE_MIN_GAP_US) {
        discipline();
    }
}

static void on_setting(void *ctx, esp_event_base_t base, int32_t id, const void *data, size_t len)
{
    (void)ctx;
    (void)base;
    uint8_t what = 0;
    if (id == SVC_SETTINGS_EVT_RESET) {
        what = SVC_TIME_CHANGED_ZONE | SVC_TIME_CHANGED_FORMAT;
    } else if (id == SVC_SETTINGS_EVT_CHANGED && len >= sizeof(svc_settings_evt_changed_t)) {
        const uint16_t sid = ((const svc_settings_evt_changed_t *)data)->id;
        what = sid == S3W_SETTING_TIMEZONE ? SVC_TIME_CHANGED_ZONE
               : sid == S3W_SETTING_TIME_24H ? SVC_TIME_CHANGED_FORMAT
                                             : 0;
    }
    if (what & SVC_TIME_CHANGED_ZONE) {
        load_zone();
    }
    if (what) {
        post_changed(what);
    }
}

// --- API -------------------------------------------------------------------------------

esp_err_t svc_time_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_buf);
    const esp_timer_create_args_t rtc_args = {.callback = rtc_write_cb, .name = "time_rtc"};
    const esp_timer_create_args_t zone_args = {.callback = zone_timer_cb, .name = "time_zone"};
    const esp_timer_create_args_t disc_args = {
        .callback = discipline_timer_cb,
        .name = "time_disc",
        .skip_unhandled_events = true,
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&rtc_args, &s_rtc_write_timer), TAG, "timer");
    ESP_RETURN_ON_ERROR(esp_timer_create(&zone_args, &s_zone_timer), TAG, "timer");
    ESP_RETURN_ON_ERROR(esp_timer_create(&disc_args, &s_discipline_timer), TAG, "timer");

    load_drift();
    if (hal_rtc_set_offset(s_drift.steps) != ESP_OK) { // lost with the RTC's power: re-apply
        ESP_LOGW(TAG, "RTC offset not applied");
    }

    time_t rtc = 0;
    bool rtc_valid = false;
    if (hal_rtc_get(&rtc, &rtc_valid) == ESP_OK && rtc_valid) {
        set_system_ms((int64_t)rtc * 1000 + 500 - s_drift.rtc_bias_ms);
        s_valid = true;
        s_source = SVC_TIME_SRC_RTC;
        struct tm utc;
        gmtime_r(&rtc, &utc);
        ESP_LOGI(TAG, "system time from RTC: %04d-%02d-%02d %02d:%02d:%02d UTC", utc.tm_year + 1900,
                 utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec);
    } else {
        ESP_LOGW(TAG, "RTC time invalid (oscillator stopped or no RTC): time unknown");
        time_drift_reset_window(&s_drift);
    }
    load_zone();

    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_discipline_timer, DISCIPLINE_PERIOD_US), TAG, "timer");
    ESP_RETURN_ON_ERROR(s3w_event_subscribe(SVC_SETTINGS_EVENT, ESP_EVENT_ANY_ID, on_setting, NULL, NULL), TAG,
                        "settings");
    ESP_RETURN_ON_ERROR(s3w_event_subscribe(SVC_POWER_EVENT, SVC_POWER_EVT_STATE, on_power_state, NULL, NULL), TAG,
                        "power");
    s_started = true;
    ESP_LOGI(TAG, "started: %s, drift steps %d", s_valid ? "time valid" : "time unknown", s_drift.steps);
    post_changed(SVC_TIME_CHANGED_CLOCK | SVC_TIME_CHANGED_ZONE | SVC_TIME_CHANGED_FORMAT);
    return ESP_OK;
}

bool svc_time_is_valid(void)
{
    return s_valid;
}

svc_time_source_t svc_time_source(void)
{
    return s_source;
}

esp_err_t svc_time_set_utc_ms(int64_t utc_ms, svc_time_source_t src)
{
    ESP_RETURN_ON_FALSE(s_started, ESP_ERR_INVALID_STATE, TAG, "not started");
    ESP_RETURN_ON_FALSE(utc_ms >= UTC_MS_MIN && utc_ms < UTC_MS_MAX && src > SVC_TIME_SRC_RTC &&
                            src <= SVC_TIME_SRC_MANUAL,
                        ESP_ERR_INVALID_ARG, TAG, "bad time or source");
    lock();
    time_t rtc = 0;
    bool rtc_valid = false;
    const bool rtc_ok = hal_rtc_get(&rtc, &rtc_valid) == ESP_OK;
    const int8_t old_steps = s_drift.steps;
    bool rewrite = true;
    time_drift_result_t res = TIME_DRIFT_ANCHORED;
    const bool reference = src == SVC_TIME_SRC_PHONE || src == SVC_TIME_SRC_SNTP;
    if (reference && rtc_ok) {
        // The RTC was pending a rewrite (back-to-back syncs): its reading is stale.
        res = time_drift_on_sync(&s_drift, utc_ms, rtc, rtc_valid && !s_rtc_write_pending, &rewrite);
    } else {
        time_drift_reset_window(&s_drift);
    }
    const int64_t err_ms = rtc_ok ? (int64_t)rtc * 1000 + 500 - utc_ms : 0;
    set_system_ms(utc_ms);
    if (rewrite || !rtc_valid) {
        schedule_rtc_write_locked(utc_ms);
    }
    const int8_t steps = s_drift.steps;
    const int32_t ppb = s_drift.last_ppb;
    const bool was_valid = s_valid;
    s_valid = true;
    s_source = src;
    apply_zone_locked();
    unlock();

    if (steps != old_steps) {
        const esp_err_t err = hal_rtc_set_offset(steps);
        ESP_LOGI(TAG, "RTC drift %+ld ppb: offset %d -> %d steps%s", (long)ppb, old_steps, steps,
                 err == ESP_OK ? "" : " (write failed)");
    }
    static const char *const results[] = {"window started", "measuring", "calibrated", "rejected"};
    ESP_LOGI(TAG, "time set from %s (RTC was %+lld ms off; drift: %s)%s", svc_time_source_name(src),
             (long long)err_ms, reference ? results[res] : "window reset", was_valid ? "" : ", time now valid");
    save_drift();
    post_changed(SVC_TIME_CHANGED_CLOCK | SVC_TIME_CHANGED_ZONE);
    return ESP_OK;
}

esp_err_t svc_time_set_tz(const char *posix_tz)
{
    tz_posix_t tz;
    ESP_RETURN_ON_FALSE(posix_tz && strlen(posix_tz) < sizeof s_tz_str && tz_posix_parse(posix_tz, &tz),
                        ESP_ERR_INVALID_ARG, TAG, "invalid POSIX TZ");
    return svc_settings_set_str(S3W_SETTING_TIMEZONE, posix_tz);
}

bool svc_time_is_24h(void)
{
    return svc_settings_get_bool(S3W_SETTING_TIME_24H);
}

esp_err_t svc_time_set_24h(bool h24)
{
    return svc_settings_set_bool(S3W_SETTING_TIME_24H, h24);
}

void svc_time_localtime(time_t utc, struct tm *out)
{
    if (!s_started) {
        tz_posix_gmtime(utc, out);
        return;
    }
    lock();
    tz_posix_localtime(&s_tz, utc, out);
    unlock();
}

int32_t svc_time_utc_offset(void)
{
    if (!s_started) {
        return 0;
    }
    lock();
    const int32_t off = tz_posix_offset(&s_tz, time(NULL), NULL);
    unlock();
    return off;
}

void svc_time_get_status(svc_time_status_t *out)
{
    memset(out, 0, sizeof *out);
    if (!s_started) {
        return;
    }
    lock();
    out->valid = s_valid;
    out->source = s_source;
    strncpy(out->tz, s_tz_str, sizeof out->tz - 1);
    strncpy(out->newlib_tz, s_newlib_tz, sizeof out->newlib_tz - 1);
    out->offset_s = tz_posix_offset(&s_tz, time(NULL), &out->dst);
    strncpy(out->abbr, out->dst ? s_tz.dst_name : s_tz.std_name, sizeof out->abbr - 1);
    out->next_transition = s_next_transition;
    out->drift = s_drift;
    out->rtc_write_pending = s_rtc_write_pending;
    unlock();
}

int64_t svc_time_discipline_now(void)
{
    return s_started ? discipline() : 0;
}

esp_err_t svc_time_drift_reset(void)
{
    ESP_RETURN_ON_FALSE(s_started, ESP_ERR_INVALID_STATE, TAG, "not started");
    lock();
    time_drift_init(&s_drift, 0);
    unlock();
    save_drift();
    return hal_rtc_set_offset(0);
}
