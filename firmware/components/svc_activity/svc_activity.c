// Activity service (svc_activity.h, docs/03 F12): steps, distance, kcal, active minutes,
// goals and the midnight rollover.
//
// No task and no timer of its own: everything runs in svc_sensors' FIFO batches (one per
// ~4.6 s, on the svc_sensors task), in API calls and in settings / power events. NVS writes
// and log files go through svc_worker (the svc_sensors stack is in PSRAM).
#include "svc_activity.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "s3w_event.h"
#include "step_detect.h"
#include "svc_power.h"
#include "svc_sensors.h"
#include "svc_settings.h"
#include "svc_time.h"
#include "svc_worker.h"

static const char *TAG = "svc_activity";

ESP_EVENT_DEFINE_BASE(SVC_ACTIVITY_EVENT);

#define SAVE_EVERY_MS (10 * 60 * 1000) // NVS writes while steps change: flash wear vs steps lost on a reset
#define NVS_NS        "s3w_act"
#define NVS_KEY       "today"
#define BLOB_VERSION  1

typedef struct {
    uint32_t version;
    activity_totals_t t;
    uint32_t crc; // esp_rom_crc32_le over the bytes before it
} blob_t;

typedef struct {
    uint32_t t_ms;
    int16_t x, y, z;
} log_rec_t;

typedef struct {
    uint16_t n;
    log_rec_t rec[];
} log_job_t;

typedef struct {
    FILE *f;
    uint32_t watch_steps;
} log_close_t;

static struct {
    SemaphoreHandle_t lock; // everything below
    step_detect_t sd;
    activity_day_t *day; // PSRAM
    activity_profile_t prof;
    uint32_t clock_ms; // time given to the last sample (esp_timer ms)
    uint32_t last_end_ms; // previous batch's end_ms
    bool have_end;
    uint32_t last_save_ms;
    uint32_t saved_steps;
    // CSV log (svc_activity_log_start)
    FILE *log; // written and closed on svc_worker only
    bool logging;
    bool log_timed;
    uint32_t log_end_ms;
    uint32_t log_t0_ms;
    uint32_t log_steps;
    svc_activity_stats_t st;
} s;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void local_now(uint32_t *day, uint16_t *minute)
{
    struct tm tm;
    svc_time_localtime(time(NULL), &tm);
    *day = svc_time_is_valid() ? (uint32_t)(tm.tm_year + 1900) * 10000 + (uint32_t)(tm.tm_mon + 1) * 100 + tm.tm_mday
                               : 0;
    *minute = (uint16_t)(tm.tm_hour * 60 + tm.tm_min);
}

static void load_profile(void)
{
    s.prof = (activity_profile_t){
        .height_cm = (uint16_t)svc_settings_get_int(S3W_SETTING_HEIGHT_CM),
        .weight_kg = (uint16_t)svc_settings_get_int(S3W_SETTING_WEIGHT_KG),
        .sex = (uint8_t)svc_settings_get_int(S3W_SETTING_SEX),
        .birth_year = (uint16_t)svc_settings_get_int(S3W_SETTING_BIRTH_YEAR),
        .step_goal = (uint32_t)svc_settings_get_int(S3W_SETTING_STEP_GOAL),
        .active_goal_min = (uint16_t)svc_settings_get_int(S3W_SETTING_ACTIVE_GOAL_MIN),
    };
}

// --- NVS (svc_worker) ------------------------------------------------------------------

static void save_job(const void *data, size_t len)
{
    blob_t b = {.version = BLOB_VERSION};
    memcpy(&b.t, data, len < sizeof b.t ? len : sizeof b.t);
    b.crc = esp_rom_crc32_le(0, (const uint8_t *)&b, offsetof(blob_t, crc));
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, NVS_KEY, &b, sizeof b);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    s.st.saves++;
    s.st.save_errors += err != ESP_OK;
    xSemaphoreGive(s.lock);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "save: %s", esp_err_to_name(err));
    }
}

// Under s.lock: queue a save of the current totals.
static void save_locked(uint32_t now)
{
    s.last_save_ms = now;
    s.saved_steps = s.day->t.steps;
    const activity_totals_t t = s.day->t;
    if (svc_worker_submit_copy(save_job, &t, sizeof t) != ESP_OK) {
        s.st.save_errors++;
    }
}

static bool restore(activity_totals_t *out)
{
    blob_t b;
    size_t len = sizeof b;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err == ESP_OK) {
        err = nvs_get_blob(h, NVS_KEY, &b, &len);
        nvs_close(h);
    }
    if (err != ESP_OK || len != sizeof b || b.version != BLOB_VERSION ||
        b.crc != esp_rom_crc32_le(0, (const uint8_t *)&b, offsetof(blob_t, crc))) {
        return false;
    }
    *out = b.t;
    return true;
}

// --- CSV log (svc_worker) ----------------------------------------------------------------

static void log_write_job(void *ctx)
{
    log_job_t *job = ctx;
    FILE *f = s.log; // set before the first job, cleared by the close job after the last
    bool ok = f != NULL;
    for (uint16_t i = 0; ok && i < job->n; i++) {
        const log_rec_t *r = &job->rec[i];
        ok = fprintf(f, "%lu,%d,%d,%d\n", (unsigned long)r->t_ms, r->x, r->y, r->z) > 0;
    }
    if (!ok) {
        xSemaphoreTake(s.lock, portMAX_DELAY);
        s.st.log_dropped += job->n;
        xSemaphoreGive(s.lock);
    }
    free(job);
}

static void log_close_job(const void *data, size_t len)
{
    log_close_t c;
    memcpy(&c, data, len < sizeof c ? len : sizeof c);
    fprintf(c.f, "# watch_steps=%lu\n", (unsigned long)c.watch_steps);
    fclose(c.f);
    xSemaphoreTake(s.lock, portMAX_DELAY);
    s.log = NULL;
    xSemaphoreGive(s.lock);
    ESP_LOGI(TAG, "log closed, the watch counted %lu steps", (unsigned long)c.watch_steps);
}

// Under s.lock.
static void log_close_locked(void)
{
    s.logging = false;
    const log_close_t c = {.f = s.log, .watch_steps = s.log_steps};
    if (svc_worker_submit_copy(log_close_job, &c, sizeof c) != ESP_OK) {
        ESP_LOGW(TAG, "log close not queued");
    }
}

static int16_t clamp16(int32_t v)
{
    return (int16_t)(v > INT16_MAX ? INT16_MAX : v < INT16_MIN ? INT16_MIN : v);
}

// --- Batches (svc_sensors task) ------------------------------------------------------------

static void post(svc_activity_event_t id, const void *data, size_t len)
{
    if (s3w_event_post(SVC_ACTIVITY_EVENT, id, data, len) != ESP_OK) {
        ESP_LOGW(TAG, "event %d dropped", id);
    }
}

static void post_results(unsigned r, const activity_summary_t *ended, uint32_t steps, uint32_t goal,
                         bool steps_changed)
{
    if (r & ACT_DAY_ENDED) {
        ESP_LOGI(TAG, "day %lu ended: %lu steps", (unsigned long)ended->day, (unsigned long)ended->steps);
        post(SVC_ACTIVITY_EVT_DAY_END, ended, sizeof *ended);
    }
    if (steps_changed || (r & ACT_DAY_ENDED)) {
        const svc_activity_evt_steps_t e = {.steps = steps, .step_goal = goal};
        post(SVC_ACTIVITY_EVT_STEPS, &e, sizeof e);
    }
    if (r & ACT_STEP_GOAL) {
        const svc_activity_evt_goal_t e = {.goal = SVC_ACTIVITY_GOAL_STEPS, .value = goal};
        post(SVC_ACTIVITY_EVT_GOAL, &e, sizeof e);
    }
    if (r & ACT_ACTIVE_GOAL) {
        xSemaphoreTake(s.lock, portMAX_DELAY);
        const svc_activity_evt_goal_t e = {.goal = SVC_ACTIVITY_GOAL_ACTIVE_MIN, .value = s.prof.active_goal_min};
        xSemaphoreGive(s.lock);
        post(SVC_ACTIVITY_EVT_GOAL, &e, sizeof e);
    }
}

static void on_batch(void *ctx, const svc_sensors_batch_t *b)
{
    (void)ctx;
    uint32_t day;
    uint16_t minute;
    local_now(&day, &minute);
    const uint32_t now = now_ms();

    log_job_t *job = NULL;
    xSemaphoreTake(s.lock, portMAX_DELAY);
    if (s.logging) {
        job = heap_caps_malloc(sizeof *job + b->n * sizeof job->rec[0], MALLOC_CAP_SPIRAM);
        if (job) {
            job->n = b->n;
        } else {
            s.st.log_dropped += b->n;
        }
    }
    // Sample times: spread evenly since the previous batch (the IMU's low-power rate is off
    // its nominal one by ~10 %, measured 23 Hz for "21 Hz"); after a gap, back from this
    // batch's time at the nominal period.
    const uint32_t nominal_ms = (uint32_t)((uint64_t)b->n * b->period_us / 1000);
    const uint32_t since = b->end_ms - s.last_end_ms;
    const bool spread = !b->restart && s.have_end && since > 0 && since < 2 * nominal_ms + 1000;
    const uint32_t first_ms = spread ? s.last_end_ms : b->end_ms - nominal_ms;
    const uint32_t span_ms = spread ? since : nominal_ms;
    if (!spread) {
        s.st.restarts++;
    }
    s.last_end_ms = b->end_ms;
    s.have_end = true;
    uint32_t steps = 0;
    for (uint16_t i = 0; i < b->n; i++) {
        uint32_t t = first_ms + (uint32_t)((uint64_t)span_ms * (i + 1u) / b->n);
        if ((int32_t)(t - s.clock_ms) <= 0) {
            t = s.clock_ms + 1; // never back in time
        }
        s.clock_ms = t;
        const svc_sensors_accel_t *a = &b->s[i];
        steps += step_detect_sample(&s.sd, s.clock_ms, a->x, a->y, a->z);
        if (job) {
            job->rec[i] = (log_rec_t){.t_ms = s.clock_ms - s.log_t0_ms,
                                      .x = clamp16(a->x),
                                      .y = clamp16(a->y),
                                      .z = clamp16(a->z)};
        }
    }
    s.st.batches++;
    s.st.samples += b->n;
    s.st.detector_steps += steps;
    activity_summary_t ended;
    const unsigned r = activity_day_add(s.day, &s.prof, day, minute, steps, &ended);
    const uint32_t total = s.day->t.steps;
    const uint32_t goal = s.prof.step_goal;
    if ((r & ACT_DAY_ENDED) || (total != s.saved_steps && now - s.last_save_ms >= SAVE_EVERY_MS)) {
        save_locked(now);
    }
    if (job) {
        s.log_steps += steps;
        s.st.log_samples += b->n;
        if (svc_worker_submit(log_write_job, job) != ESP_OK) {
            s.st.log_dropped += b->n;
            free(job);
        }
        if (s.log_timed && (int32_t)(now - s.log_end_ms) >= 0) {
            log_close_locked();
        }
    }
    xSemaphoreGive(s.lock);
    post_results(r, &ended, total, goal, steps > 0);
}

// --- Events ---------------------------------------------------------------------------------

static bool health_setting(s3w_setting_t id)
{
    return id == S3W_SETTING_HEIGHT_CM || id == S3W_SETTING_WEIGHT_KG || id == S3W_SETTING_SEX ||
           id == S3W_SETTING_BIRTH_YEAR || id == S3W_SETTING_STEP_GOAL || id == S3W_SETTING_ACTIVE_GOAL_MIN;
}

static void on_setting(void *ctx, esp_event_base_t base, int32_t id, const void *data, size_t len)
{
    (void)ctx;
    (void)base;
    if (id == SVC_SETTINGS_EVT_CHANGED && data && len >= sizeof(svc_settings_evt_changed_t)) {
        if (!health_setting(((const svc_settings_evt_changed_t *)data)->id)) {
            return;
        }
    } else if (id != SVC_SETTINGS_EVT_RESET) {
        return;
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    load_profile();
    const svc_activity_evt_steps_t e = {.steps = s.day->t.steps, .step_goal = s.prof.step_goal};
    xSemaphoreGive(s.lock);
    post(SVC_ACTIVITY_EVT_STEPS, &e, sizeof e); // the goal may have changed
}

// Powering off (or WATCH-ONLY's deep sleep): keep today's steps.
static void on_power(void *ctx, esp_event_base_t base, int32_t id, const void *data, size_t len)
{
    (void)ctx;
    (void)base;
    (void)id;
    if (len < sizeof(svc_power_evt_state_t)) {
        return;
    }
    const power_state_t st = ((const svc_power_evt_state_t *)data)->state;
    if (st == POWER_STATE_OFF || st == POWER_STATE_WATCH_ONLY) {
        xSemaphoreTake(s.lock, portMAX_DELAY);
        if (s.day->t.steps != s.saved_steps) {
            save_locked(now_ms());
        }
        xSemaphoreGive(s.lock);
    }
}

// --- API ------------------------------------------------------------------------------------

esp_err_t svc_activity_start(void)
{
    ESP_RETURN_ON_FALSE(!s.lock, ESP_ERR_INVALID_STATE, TAG, "already started");
    s.day = heap_caps_malloc(sizeof *s.day, MALLOC_CAP_SPIRAM);
    s.lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s.day && s.lock, ESP_ERR_NO_MEM, TAG, "alloc");
    load_profile();
    step_detect_init(&s.sd);
    uint32_t day;
    uint16_t minute;
    local_now(&day, &minute);
    activity_totals_t saved;
    if (day && restore(&saved) && saved.day == day) {
        activity_day_restore(s.day, &saved);
        s.saved_steps = saved.steps;
    } else {
        activity_day_init(s.day, day);
    }
    s.last_save_ms = now_ms();
    ESP_RETURN_ON_ERROR(s3w_event_subscribe(SVC_SETTINGS_EVENT, ESP_EVENT_ANY_ID, on_setting, NULL, NULL), TAG,
                        "settings");
    ESP_RETURN_ON_ERROR(s3w_event_subscribe(SVC_POWER_EVENT, SVC_POWER_EVT_STATE, on_power, NULL, NULL), TAG,
                        "power");
    ESP_RETURN_ON_ERROR(svc_sensors_set_batch_cb(on_batch, NULL), TAG, "sensors");
    ESP_LOGI(TAG, "started: day %lu, %lu steps restored, goal %lu", (unsigned long)s.day->t.day,
             (unsigned long)s.day->t.steps, (unsigned long)s.prof.step_goal);
    return ESP_OK;
}

void svc_activity_get(svc_activity_state_t *out)
{
    memset(out, 0, sizeof *out);
    if (!s.lock) {
        return;
    }
    uint32_t day;
    uint16_t minute;
    local_now(&day, &minute);
    xSemaphoreTake(s.lock, portMAX_DELAY);
    activity_summary_t ended;
    const unsigned r = activity_day_add(s.day, &s.prof, day, minute, 0, &ended);
    if (r & ACT_DAY_ENDED) {
        save_locked(now_ms());
    }
    activity_day_summary(s.day, &s.prof, minute, &out->today);
    out->cadence_spm = step_detect_cadence(&s.sd);
    out->walking = s.sd.walking;
    const uint32_t steps = s.day->t.steps;
    const uint32_t goal = s.prof.step_goal;
    xSemaphoreGive(s.lock);
    post_results(r, &ended, steps, goal, false);
}

void svc_activity_get_minutes(uint16_t *out, size_t n)
{
    if (!s.lock) {
        memset(out, 0, n * sizeof *out);
        return;
    }
    n = n > ACT_MINUTES ? ACT_MINUTES : n;
    xSemaphoreTake(s.lock, portMAX_DELAY);
    memcpy(out, s.day->minute_steps, n * sizeof *out);
    xSemaphoreGive(s.lock);
}

esp_err_t svc_activity_reset_today(void)
{
    ESP_RETURN_ON_FALSE(s.lock, ESP_ERR_INVALID_STATE, TAG, "not started");
    uint32_t day;
    uint16_t minute;
    local_now(&day, &minute);
    xSemaphoreTake(s.lock, portMAX_DELAY);
    activity_day_init(s.day, day);
    save_locked(now_ms());
    const svc_activity_evt_steps_t e = {.steps = 0, .step_goal = s.prof.step_goal};
    xSemaphoreGive(s.lock);
    post(SVC_ACTIVITY_EVT_STEPS, &e, sizeof e);
    return ESP_OK;
}

esp_err_t svc_activity_log_start(const char *path, uint32_t seconds)
{
    ESP_RETURN_ON_FALSE(s.lock, ESP_ERR_INVALID_STATE, TAG, "not started");
    ESP_RETURN_ON_FALSE(path && strlen(path) < sizeof s.st.log_path, ESP_ERR_INVALID_ARG, TAG, "path");
    xSemaphoreTake(s.lock, portMAX_DELAY);
    const bool busy = s.logging || s.log;
    xSemaphoreGive(s.lock);
    ESP_RETURN_ON_FALSE(!busy, ESP_ERR_INVALID_STATE, TAG, "a log is running");
    // Opened here (console task), written and closed on svc_worker.
    FILE *f = fopen(path, "w");
    ESP_RETURN_ON_FALSE(f, ESP_FAIL, TAG, "open %s", path);
    fprintf(f, "# s3w accel log v1: t_ms,x,y,z (mg, watch frame)\n"
               "# source=recorded on the watch (svc_sensors FIFO: 21 Hz, 62.5 Hz in raise windows)\n"
               "# steps=?   <- replace ? with the steps you counted; # tol=N (steps) if not 7 %%\n"
               "t_ms,x,y,z\n");
    xSemaphoreTake(s.lock, portMAX_DELAY);
    s.log = f;
    s.logging = true;
    s.log_timed = seconds > 0;
    s.log_t0_ms = s.clock_ms;
    s.log_end_ms = now_ms() + seconds * 1000;
    s.log_steps = 0;
    s.st.log_samples = 0;
    s.st.log_dropped = 0;
    strlcpy(s.st.log_path, path, sizeof s.st.log_path);
    xSemaphoreGive(s.lock);
    ESP_LOGI(TAG, "logging to %s for %lu s", path, (unsigned long)seconds);
    return ESP_OK;
}

esp_err_t svc_activity_log_stop(void)
{
    ESP_RETURN_ON_FALSE(s.lock, ESP_ERR_INVALID_STATE, TAG, "not started");
    xSemaphoreTake(s.lock, portMAX_DELAY);
    const bool on = s.logging;
    if (on) {
        log_close_locked();
    }
    xSemaphoreGive(s.lock);
    return on ? ESP_OK : ESP_ERR_INVALID_STATE;
}

void svc_activity_get_stats(svc_activity_stats_t *out)
{
    memset(out, 0, sizeof *out);
    if (!s.lock) {
        return;
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    *out = s.st;
    out->logging = s.logging;
    xSemaphoreGive(s.lock);
}
