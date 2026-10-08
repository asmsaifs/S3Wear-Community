// Modes service (svc_modes.h, docs/03 F2/F4/F7).
//
// No task. The state is worked out under a mutex by whoever changes it (API, settings
// and time events) and published from one esp_timer callback, so events go out in
// order. That timer also wakes the watch at each schedule edge (start or end of a
// window: a few per day at most), and is otherwise idle.
#include "svc_modes.h"

#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "s3w_event.h"
#include "svc_settings.h"
#include "svc_time.h"

static const char *TAG = "svc_modes";

ESP_EVENT_DEFINE_BASE(SVC_MODES_EVENT);

#define PUBLISH_NOW_US 1     // esp_timer: "as soon as possible"
#define EDGE_MARGIN_US 50000 // fire just after the minute starts (system time is slewed)

static const struct {
    s3w_setting_t manual, days, start, end;
} k_keys[] = {
    [MODE_DND] = {S3W_SETTING_DND, S3W_SETTING_DND_DAYS, S3W_SETTING_DND_START, S3W_SETTING_DND_END},
    [MODE_SLEEP] = {S3W_SETTING_SLEEP_MODE, S3W_SETTING_SLEEP_DAYS, S3W_SETTING_SLEEP_START, S3W_SETTING_SLEEP_END},
};

static struct {
    SemaphoreHandle_t mutex;
    esp_timer_handle_t timer;
    // Under the mutex
    modes_t m;
    modes_state_t state;     // as of the last refresh
    modes_state_t published; // as of the last event
    bool published_once;
    int64_t next_edge;
    uint32_t changes;
} s;

static void lock(void)
{
    xSemaphoreTake(s.mutex, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s.mutex);
}

static bool has_sched(mode_id_t id)
{
    return id == MODE_DND || id == MODE_SLEEP;
}

// Local weekday and minute now (wday -1 while the time is unknown), and microseconds
// into that minute.
static void local_now(int *wday, int *min, int64_t *into_min_us, int64_t *utc)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    *utc = tv.tv_sec;
    if (!svc_time_is_valid()) {
        *wday = -1;
        *min = 0;
        *into_min_us = 0;
        return;
    }
    struct tm lt;
    svc_time_localtime(tv.tv_sec, &lt);
    *wday = lt.tm_wday;
    *min = lt.tm_hour * 60 + lt.tm_min;
    *into_min_us = (int64_t)lt.tm_sec * 1000000 + tv.tv_usec;
}

// Mutex held. Returns microseconds to the next schedule edge (0 = none).
static int64_t refresh_locked(void)
{
    int wday;
    int min;
    int64_t into_us;
    int64_t utc;
    local_now(&wday, &min, &into_us, &utc);
    s.state = modes_eval(&s.m, wday, min);
    const uint32_t edge = modes_next_edge(&s.m, wday, min);
    if (!edge) {
        s.next_edge = 0;
        return 0;
    }
    const int64_t us = (int64_t)edge * 60 * 1000000 - into_us + EDGE_MARGIN_US;
    s.next_edge = utc + us / 1000000;
    return us > 0 ? us : PUBLISH_NOW_US;
}

static void arm(uint64_t us)
{
    esp_timer_stop(s.timer); // ESP_ERR_INVALID_STATE if it was not running
    esp_timer_start_once(s.timer, us);
}

// Publish soon from the timer callback (mutex held or not).
static void kick(void)
{
    arm(PUBLISH_NOW_US);
}

static bool same(const modes_state_t *a, const modes_state_t *b)
{
    return a->dnd == b->dnd && a->sleep == b->sleep && a->theater == b->theater;
}

// esp_timer task: refresh, publish a change, wait for the next edge.
static void on_timer(void *arg)
{
    (void)arg;
    lock();
    const int64_t us = refresh_locked();
    const modes_state_t st = s.state;
    const bool changed = !s.published_once || !same(&st, &s.published);
    s.published = st;
    s.published_once = true;
    if (changed) {
        s.changes++;
    }
    if (us > 0) {
        esp_timer_start_once(s.timer, (uint64_t)us);
    }
    unlock();
    if (changed) {
        ESP_LOGI(TAG, "DND %d, sleep %d, theater %d", st.dnd, st.sleep, st.theater);
        if (s3w_event_post(SVC_MODES_EVENT, SVC_MODES_EVT_CHANGED, &st, sizeof st) != ESP_OK) {
            ESP_LOGW(TAG, "event dropped");
        }
    }
}

// Mutex held.
static void load_sched(mode_id_t id)
{
    s.m.sched[id] = (mode_sched_t){
        .days = (uint8_t)svc_settings_get_int(k_keys[id].days),
        .start_min = (uint16_t)svc_settings_get_int(k_keys[id].start),
        .end_min = (uint16_t)svc_settings_get_int(k_keys[id].end),
    };
}

// Mutex held. A by-hand setting changed (from here, the console or a reset).
static void apply_manual(mode_id_t id, bool on)
{
    int wday;
    int min;
    int64_t into_us;
    int64_t utc;
    local_now(&wday, &min, &into_us, &utc);
    modes_set(&s.m, id, on, wday, min);
}

static void on_setting(void *ctx, esp_event_base_t base, int32_t id, const void *data, size_t len)
{
    (void)ctx;
    (void)base;
    const bool reset = id == SVC_SETTINGS_EVT_RESET;
    if (!reset && (id != SVC_SETTINGS_EVT_CHANGED || len < sizeof(svc_settings_evt_changed_t))) {
        return;
    }
    const s3w_setting_t sid = reset ? S3W_SETTING_COUNT : ((const svc_settings_evt_changed_t *)data)->id;
    bool hit = false;
    lock();
    for (int i = 0; i < MODE_COUNT; i++) {
        if (!has_sched((mode_id_t)i)) {
            continue;
        }
        if (reset || sid == k_keys[i].days || sid == k_keys[i].start || sid == k_keys[i].end) {
            load_sched((mode_id_t)i);
            hit = true;
        }
        if (reset || sid == k_keys[i].manual) {
            const bool on = svc_settings_get_bool(k_keys[i].manual);
            if (reset || on != s.m.manual[i]) { // svc_modes_set() already applied its own write
                apply_manual((mode_id_t)i, on);
            }
            hit = true;
        }
    }
    if (hit) {
        refresh_locked();
        kick();
    }
    unlock();
}

// New clock, zone or valid flag: windows move.
static void on_time(void *ctx, esp_event_base_t base, int32_t id, const void *data, size_t len)
{
    (void)ctx;
    (void)base;
    (void)id;
    (void)data;
    (void)len;
    lock();
    refresh_locked();
    kick();
    unlock();
}

// --- API -----------------------------------------------------------------------------

esp_err_t svc_modes_start(void)
{
    ESP_RETURN_ON_FALSE(!s.mutex, ESP_ERR_INVALID_STATE, TAG, "already started");
    s.mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s.mutex, ESP_ERR_NO_MEM, TAG, "mutex");
    const esp_timer_create_args_t ta = {.callback = on_timer, .name = "modes"};
    ESP_RETURN_ON_ERROR(esp_timer_create(&ta, &s.timer), TAG, "timer");

    lock();
    for (int i = 0; i < MODE_COUNT; i++) {
        if (has_sched((mode_id_t)i)) {
            s.m.manual[i] = svc_settings_get_bool(k_keys[i].manual);
            load_sched((mode_id_t)i);
        }
    }
    refresh_locked();
    s.published = s.state; // svc_power and svc_sensors read it at their start
    s.published_once = true;
    kick();                 // arm the first edge
    const modes_state_t st = s.state;
    unlock();

    ESP_RETURN_ON_ERROR(s3w_event_subscribe(SVC_SETTINGS_EVENT, ESP_EVENT_ANY_ID, on_setting, NULL, NULL), TAG,
                        "settings");
    ESP_RETURN_ON_ERROR(s3w_event_subscribe(SVC_TIME_EVENT, SVC_TIME_EVT_CHANGED, on_time, NULL, NULL), TAG, "time");
    ESP_LOGI(TAG, "started: DND %d (days 0x%02x), sleep %d (days 0x%02x)", st.dnd, s.m.sched[MODE_DND].days,
             st.sleep, s.m.sched[MODE_SLEEP].days);
    return ESP_OK;
}

void svc_modes_get(modes_state_t *out)
{
    if (!s.mutex) {
        memset(out, 0, sizeof *out);
        return;
    }
    lock();
    *out = s.state;
    unlock();
}

esp_err_t svc_modes_set(mode_id_t id, bool on)
{
    ESP_RETURN_ON_FALSE((unsigned)id < MODE_COUNT, ESP_ERR_INVALID_ARG, TAG, "mode %d", (int)id);
    ESP_RETURN_ON_FALSE(s.mutex, ESP_ERR_INVALID_STATE, TAG, "not started");
    lock();
    apply_manual(id, on);
    refresh_locked();
    kick();
    unlock();
    // Keep it (the settings event finds it applied). Theater mode is not kept.
    return has_sched(id) ? svc_settings_set_bool(k_keys[id].manual, on) : ESP_OK;
}

esp_err_t svc_modes_get_sched(mode_id_t id, mode_sched_t *out)
{
    ESP_RETURN_ON_FALSE(has_sched(id), ESP_ERR_INVALID_ARG, TAG, "no schedule for %s", mode_name(id));
    *out = (mode_sched_t){
        .days = (uint8_t)svc_settings_get_int(k_keys[id].days),
        .start_min = (uint16_t)svc_settings_get_int(k_keys[id].start),
        .end_min = (uint16_t)svc_settings_get_int(k_keys[id].end),
    };
    return ESP_OK;
}

esp_err_t svc_modes_set_sched(mode_id_t id, const mode_sched_t *sched)
{
    ESP_RETURN_ON_FALSE(has_sched(id), ESP_ERR_INVALID_ARG, TAG, "no schedule for %s", mode_name(id));
    ESP_RETURN_ON_FALSE(sched->days < 0x80 && sched->start_min < MODES_MIN_PER_DAY &&
                            sched->end_min < MODES_MIN_PER_DAY,
                        ESP_ERR_INVALID_ARG, TAG, "bad schedule");
    // Three events; each reloads the whole schedule, so a half-written one lasts
    // only until the last write.
    ESP_RETURN_ON_ERROR(svc_settings_set_int(k_keys[id].start, sched->start_min), TAG, "start");
    ESP_RETURN_ON_ERROR(svc_settings_set_int(k_keys[id].end, sched->end_min), TAG, "end");
    return svc_settings_set_int(k_keys[id].days, sched->days);
}

void svc_modes_get_status(svc_modes_status_t *out)
{
    memset(out, 0, sizeof *out);
    if (!s.mutex) {
        return;
    }
    lock();
    out->m = s.m;
    out->state = s.state;
    out->next_edge = s.next_edge;
    out->changes = s.changes;
    unlock();
}
