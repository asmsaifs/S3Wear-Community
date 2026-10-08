// Alarm service (svc_alarm.h, docs/03 F6).
//
// Wake-ups of the svc_alarm task: requests on its queue (API calls, the two
// esp_timers, the PCF85063 interrupt, time and flip events; all event driven) and,
// only while ringing, a RING_POLL_MS timeout to check the ring timeout (the sound itself
// is svc_audio's).
#include "svc_alarm.h"

#include <string.h>
#include <sys/time.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "hal.h"
#include "nvs.h"
#include "s3w_event.h"
#include "s3w_task.h"
#include "svc_audio.h"
#include "svc_power.h"
#include "svc_sensors.h"
#include "svc_settings.h"
#include "svc_time.h"

static const char *TAG = "svc_alarm";

ESP_EVENT_DEFINE_BASE(SVC_ALARM_EVENT);

#define NVS_NS     "s3w_alarm"
#define NVS_KEY    "alarms"
#define QUEUE_LEN  8

// Ringing: svc_audio plays the beeps. While ringing the task wakes every RING_POLL_MS
// to check the ring timeout (1 min .. 2 min, so the precision is not critical).
#define RING_POLL_MS 250

typedef enum {
    MSG_ALARMS_CHANGED, // save, reschedule, publish
    MSG_TIMERS_CHANGED, // reschedule, publish
    MSG_FIRE,           // an esp_timer or the RTC alarm: check what is due
    MSG_SNOOZE,
    MSG_DISMISS,
    MSG_FLIP,
    MSG_TIME,           // clock, zone or valid flag changed
} msg_type_t;

typedef struct {
    uint8_t type;
} msg_t;

static struct {
    SemaphoreHandle_t mutex;
    QueueHandle_t queue;
    TaskHandle_t task;
    esp_timer_handle_t alarm_timer;
    esp_timer_handle_t countdown_timer;

    // Under the mutex
    alarm_set_t set;
    timer_set_t timers;
    tz_posix_t tz;
    bool time_valid;
    bool ringing;
    svc_alarm_evt_ring_t ring;
    svc_alarm_stats_t st;

    // svc_alarm task only
    int64_t rtc_next;      // what the PCF85063 is programmed with (0 = off, -1 = unknown)
    uint32_t ring_start_ms;
    uint8_t auto_snoozes;
} s;

static uint8_t s_blob[ALARM_BLOB_MAX]; // svc_alarm task (and start) only

static void lock(void)
{
    xSemaphoreTake(s.mutex, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s.mutex);
}

static int64_t now_utc_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

static uint32_t mono_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static esp_err_t post(msg_type_t type)
{
    ESP_RETURN_ON_FALSE(s.queue, ESP_ERR_INVALID_STATE, TAG, "not started");
    const msg_t m = {.type = (uint8_t)type};
    return xQueueSend(s.queue, &m, pdMS_TO_TICKS(50)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void publish(svc_alarm_event_t id, const void *data, size_t len)
{
    if (s3w_event_post(SVC_ALARM_EVENT, id, data, len) != ESP_OK) {
        ESP_LOGW(TAG, "event %d dropped", (int)id);
    }
}

// --- Inputs (timer, driver and bus contexts: queue only) ------------------------------

static void on_timer(void *arg)
{
    (void)arg;
    post(MSG_FIRE);
}

static void on_rtc_alarm(void *ctx)
{
    (void)ctx;
    lock();
    s.st.rtc_irqs++;
    unlock();
    post(MSG_FIRE);
}

static void on_time(void *ctx, esp_event_base_t base, int32_t id, const void *data, size_t len)
{
    (void)ctx;
    (void)base;
    (void)id;
    (void)data;
    (void)len;
    post(MSG_TIME);
}

static void on_flip(void *ctx, esp_event_base_t base, int32_t id, const void *data, size_t len)
{
    (void)ctx;
    (void)base;
    (void)id;
    (void)data;
    (void)len;
    post(MSG_FLIP);
}

// --- Persistence ------------------------------------------------------------------------

static void load(void)
{
    nvs_handle_t h;
    size_t len = sizeof s_blob;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err == ESP_OK) {
        err = nvs_get_blob(h, NVS_KEY, s_blob, &len);
        nvs_close(h);
    }
    if (err == ESP_OK && !alarm_set_decode(&s.set, s_blob, len)) {
        ESP_LOGW(TAG, "stored alarms damaged, starting empty");
    } else if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "load: %s", esp_err_to_name(err));
    }
}

static void save(void)
{
    lock();
    const size_t len = alarm_set_encode(&s.set, s_blob, sizeof s_blob);
    unlock();
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, NVS_KEY, s_blob, len);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "save: %s", esp_err_to_name(err));
    }
}

// --- Scheduling (task) --------------------------------------------------------------------

static void read_zone(void)
{
    static svc_time_status_t st; // task only (and start)
    svc_time_get_status(&st);
    tz_posix_t tz;
    const bool ok = tz_posix_parse(st.tz, &tz);
    lock();
    if (ok) {
        s.tz = tz;
    }
    s.time_valid = st.valid;
    alarm_set_clock_changed(&s.set, now_utc_us() / 1000000);
    unlock();
}

static void reschedule(void)
{
    const int64_t now_us = now_utc_us();
    const uint32_t now_ms = mono_ms();
    lock();
    const bool valid = s.time_valid;
    const bool due = valid && alarm_set_due(&s.set, &s.tz, now_us / 1000000, NULL);
    const int64_t next = valid && !due ? alarm_set_next(&s.set, &s.tz, now_us / 1000000, NULL) : ALARM_NEVER;
    uint32_t end_ms = 0;
    const bool countdown = timer_set_next_end(&s.timers, &end_ms);
    s.st.next_alarm = next == ALARM_NEVER ? 0 : next;
    unlock();

    esp_timer_stop(s.alarm_timer);
    esp_timer_stop(s.countdown_timer);
    if (due || (countdown && (int32_t)(end_ms - now_ms) <= 0)) {
        post(MSG_FIRE);
    }
    if (next != ALARM_NEVER) {
        esp_timer_start_once(s.alarm_timer, (uint64_t)(next * 1000000 - now_us));
    }
    if (countdown && (int32_t)(end_ms - now_ms) > 0) {
        esp_timer_start_once(s.countdown_timer, (uint64_t)(end_ms - now_ms) * 1000);
    }

    // Backup interrupt and the WATCH-ONLY wake (alarms only; timers end with a reboot).
    const int64_t rtc = next == ALARM_NEVER ? 0 : next;
    if (rtc != s.rtc_next) {
        const esp_err_t err = rtc ? hal_rtc_set_alarm((time_t)rtc) : hal_rtc_cancel_alarm();
        if (err == ESP_OK) {
            s.rtc_next = rtc;
        } else {
            ESP_LOGW(TAG, "RTC alarm: %s", esp_err_to_name(err));
        }
        lock();
        s.st.rtc_armed = s.rtc_next != 0;
        unlock();
    }
    svc_power_set_alarm_wake(rtc);
}

// --- Ringing (task) -------------------------------------------------------------------------

static void ring_start(const svc_alarm_evt_ring_t *r)
{
    lock();
    s.ring = *r;
    s.ringing = true;
    s.st.rings++;
    unlock();
    s.ring_start_ms = mono_ms();
    // Ringers ignore Silent and the quiet modes (svc_audio policy).
    svc_audio_ring_start(r->kind == SVC_ALARM_RING_ALARM ? SND_ALARM : SND_TIMER);
    lock();
    s.st.audio = svc_audio_available();
    unlock();
    svc_power_wake(SVC_POWER_WAKE_ALARM);
    svc_sensors_watch_flip(true);
    publish(SVC_ALARM_EVT_RING, r, sizeof *r);
}

static bool snooze_allowed(svc_alarm_end_reason_t reason)
{
    switch (reason) {
    case SVC_ALARM_END_SNOOZE:
    case SVC_ALARM_END_FLIP:
        return true;
    case SVC_ALARM_END_TIMEOUT:
        return s.auto_snoozes < SVC_ALARM_AUTO_SNOOZES;
    default:
        return false;
    }
}

static void ring_end(svc_alarm_end_reason_t reason)
{
    svc_audio_stop(s.ring.kind == SVC_ALARM_RING_ALARM ? SND_ALARM : SND_TIMER);
    svc_sensors_watch_flip(false);
    lock();
    const svc_alarm_evt_ring_t r = s.ring;
    s.ringing = false;
    unlock();

    svc_alarm_evt_ring_end_t evt = {.kind = r.kind, .id = r.id, .reason = (uint8_t)reason};
    bool alarms_changed = false;
    bool timers_changed = false;
    bool snoozed = false;
    if (r.kind == SVC_ALARM_RING_ALARM) {
        if (snooze_allowed(reason)) {
            s.auto_snoozes = reason == SVC_ALARM_END_TIMEOUT ? s.auto_snoozes + 1 : 0;
            const int64_t now = now_utc_us() / 1000000;
            lock();
            alarm_set_snooze(&s.set, r.id, now);
            unlock();
            evt.snooze_min = r.snooze_min;
            alarms_changed = snoozed = true;
        } else {
            s.auto_snoozes = 0;
        }
    } else if (reason != SVC_ALARM_END_TIMEOUT && reason != SVC_ALARM_END_REPLACED) {
        lock();
        const countdown_t *c = timer_set_find(&s.timers, r.id);
        if (c && c->state == TIMER_DONE) {
            timer_set_remove(&s.timers, r.id); // stopped: gone from the list (not if restarted)
        }
        unlock();
        timers_changed = true;
    }
    ESP_LOGI(TAG, "%s %u stops: reason %d%s", r.kind == SVC_ALARM_RING_ALARM ? "alarm" : "timer", r.id, (int)reason,
             snoozed ? " (snoozed)" : "");
    publish(SVC_ALARM_EVT_RING_END, &evt, sizeof evt);
    if (alarms_changed) {
        save();
    }
    if (alarms_changed || timers_changed) {
        reschedule();
        publish(SVC_ALARM_EVT_CHANGED, NULL, 0);
    }
    // Woken from WATCH-ONLY only for this alarm: go back once it is over.
    if (r.kind == SVC_ALARM_RING_ALARM && !snoozed && svc_power_alarm_boot()) {
        ESP_LOGI(TAG, "alarm over: back to watch-only");
        svc_power_enter_watch_only();
    }
}

static void ring_step(void)
{
    const uint32_t el = mono_ms() - s.ring_start_ms;
    const bool alarm = s.ring.kind == SVC_ALARM_RING_ALARM; // s.ring changes only on this task
    if (el >= (alarm ? SVC_ALARM_RING_MS : SVC_TIMER_RING_MS)) {
        ring_end(SVC_ALARM_END_TIMEOUT);
    }
}

// --- Due check (task) --------------------------------------------------------------------------

static void fire(void)
{
    const int64_t now_us = now_utc_us();
    const int64_t now = now_us / 1000000;
    uint8_t done[TIMER_MAX];
    alarm_due_t due;
    svc_alarm_evt_ring_t alarm_ring = {.kind = SVC_ALARM_RING_ALARM};
    svc_alarm_evt_ring_t timer_ring = {.kind = SVC_ALARM_RING_TIMER};

    lock();
    const size_t n_done = timer_set_expire(&s.timers, mono_ms(), done, TIMER_MAX);
    if (n_done) {
        timer_ring.id = done[0];
        timer_ring.duration_ms = timer_set_find(&s.timers, done[0])->duration_ms;
    }
    const bool is_due = s.time_valid && alarm_set_due(&s.set, &s.tz, now, &due);
    if (is_due) {
        const alarm_t *a = alarm_set_find_const(&s.set, due.id);
        alarm_ring.id = due.id;
        alarm_ring.snoozed = due.snooze;
        alarm_ring.at = due.at;
        alarm_ring.snooze_min = a ? a->snooze_min : ALARM_SNOOZE_DEFAULT;
        if (a) {
            memcpy(alarm_ring.label, a->label, sizeof alarm_ring.label);
        }
        alarm_set_mark_rung(&s.set, &s.tz, now);
        s.st.last_late_ms = (int32_t)((now_us - due.at * 1000000) / 1000);
    }
    const bool ringing = s.ringing;
    const uint8_t ringing_kind = s.ring.kind;
    unlock();

    if (is_due) {
        ESP_LOGI(TAG, "alarm %u%s due, %+ld ms", due.id, due.snooze ? " (snooze)" : "", (long)s.st.last_late_ms);
        if (!ringing || ringing_kind == SVC_ALARM_RING_TIMER) {
            if (ringing) {
                ring_end(SVC_ALARM_END_REPLACED);
            }
            if (!due.snooze) {
                s.auto_snoozes = 0;
            }
            ring_start(&alarm_ring);
        } // else another alarm is ringing: this one counts as rung
        save();
    } else if (n_done) {
        ESP_LOGI(TAG, "timer %u done", timer_ring.id);
        if (!ringing) {
            ring_start(&timer_ring);
        }
    }
    reschedule();
    if (is_due || n_done) {
        publish(SVC_ALARM_EVT_CHANGED, NULL, 0);
    }
}

static void handle(const msg_t *m)
{
    switch ((msg_type_t)m->type) {
    case MSG_ALARMS_CHANGED:
        save();
        reschedule();
        publish(SVC_ALARM_EVT_CHANGED, NULL, 0);
        break;
    case MSG_TIMERS_CHANGED:
        reschedule();
        publish(SVC_ALARM_EVT_CHANGED, NULL, 0);
        break;
    case MSG_FIRE:
        fire();
        break;
    case MSG_SNOOZE:
    case MSG_DISMISS:
    case MSG_FLIP:
        if (s.ringing) {
            ring_end(m->type == MSG_SNOOZE  ? SVC_ALARM_END_SNOOZE
                     : m->type == MSG_FLIP ? SVC_ALARM_END_FLIP
                                           : SVC_ALARM_END_DISMISS);
        }
        break;
    case MSG_TIME:
        read_zone();
        reschedule();
        publish(SVC_ALARM_EVT_CHANGED, NULL, 0); // "next alarm" may read differently
        break;
    }
}

static void alarm_task(void *arg)
{
    (void)arg;
    for (;;) {
        const TickType_t wait = s.ringing ? pdMS_TO_TICKS(RING_POLL_MS) : portMAX_DELAY;
        msg_t m;
        if (xQueueReceive(s.queue, &m, wait)) {
            handle(&m);
        }
        if (s.ringing) {
            ring_step();
        }
    }
}

// --- API --------------------------------------------------------------------------------------

esp_err_t svc_alarm_start(void)
{
    ESP_RETURN_ON_FALSE(!s.queue, ESP_ERR_INVALID_STATE, TAG, "already started");
    s.mutex = xSemaphoreCreateMutex();
    s.queue = xQueueCreate(QUEUE_LEN, sizeof(msg_t));
    ESP_RETURN_ON_FALSE(s.mutex && s.queue, ESP_ERR_NO_MEM, TAG, "queue");
    alarm_set_init(&s.set);
    timer_set_init(&s.timers);
    s.rtc_next = -1; // a previous boot may have left an alarm programmed: always write it once
    tz_posix_parse("UTC0", &s.tz);
    load();
    read_zone();

    // Not skip_unhandled_events: these must end light sleep.
    const esp_timer_create_args_t at = {.callback = on_timer, .name = "alarm"};
    ESP_RETURN_ON_ERROR(esp_timer_create(&at, &s.alarm_timer), TAG, "timer");
    const esp_timer_create_args_t ct = {.callback = on_timer, .name = "countdown"};
    ESP_RETURN_ON_ERROR(esp_timer_create(&ct, &s.countdown_timer), TAG, "timer");

    const s3w_task_cfg_t task = {
        .name = "svc_alarm",
        .fn = alarm_task,
        .stack_bytes = S3W_STACK_ALARM,
        .prio = S3W_PRIO_ALARM,
        .core = S3W_CORE_SERVICES, // internal stack: NVS writes
    };
    ESP_RETURN_ON_ERROR(s3w_task_create(&task, &s.task), TAG, "task");
    hal_rtc_set_alarm_cb(on_rtc_alarm, NULL);
    ESP_RETURN_ON_ERROR(s3w_event_subscribe(SVC_TIME_EVENT, SVC_TIME_EVT_CHANGED, on_time, NULL, NULL), TAG, "time");
    ESP_RETURN_ON_ERROR(s3w_event_subscribe(SVC_SENSORS_EVENT, SVC_SENSORS_EVT_FLIP, on_flip, NULL, NULL), TAG,
                        "flip");
    post(MSG_FIRE); // anything missed while off (deep-sleep wake), then the schedule
    ESP_LOGI(TAG, "started: %u alarm(s), time %s", s.set.count, s.time_valid ? "valid" : "unknown");
    return ESP_OK;
}

void svc_alarm_get(alarm_set_t *out)
{
    lock();
    *out = s.set;
    unlock();
}

esp_err_t svc_alarm_put(const alarm_t *a, uint8_t *id)
{
    ESP_RETURN_ON_FALSE(a && s.mutex, ESP_ERR_INVALID_ARG, TAG, "arg");
    esp_err_t err = ESP_OK;
    lock();
    if (a->id == 0 && s.set.count >= ALARM_MAX) {
        err = ESP_ERR_NO_MEM;
    } else {
        const uint8_t got = alarm_set_put_at(&s.set, a, &s.tz, now_utc_us() / 1000000);
        if (got == 0) {
            err = ESP_ERR_INVALID_ARG;
        } else {
            if (id) {
                *id = got;
            }
        }
    }
    unlock();
    return err == ESP_OK ? post(MSG_ALARMS_CHANGED) : err;
}

esp_err_t svc_alarm_delete(uint8_t id)
{
    ESP_RETURN_ON_FALSE(s.mutex, ESP_ERR_INVALID_STATE, TAG, "not started");
    lock();
    const bool ok = alarm_set_remove(&s.set, id);
    unlock();
    return ok ? post(MSG_ALARMS_CHANGED) : ESP_ERR_NOT_FOUND;
}

int64_t svc_alarm_next(void)
{
    if (!s.mutex) {
        return 0;
    }
    const int64_t now = now_utc_us() / 1000000;
    lock();
    const int64_t next = s.time_valid ? alarm_set_next(&s.set, &s.tz, now, NULL) : ALARM_NEVER;
    unlock();
    return next == ALARM_NEVER ? 0 : next;
}

void svc_alarm_timers(timer_set_t *out, uint32_t *now_ms)
{
    if (!s.mutex) {
        timer_set_init(out);
        *now_ms = mono_ms();
        return;
    }
    lock();
    *out = s.timers;
    *now_ms = mono_ms();
    unlock();
}

esp_err_t svc_alarm_timer_start(uint32_t duration_ms, uint8_t *id)
{
    ESP_RETURN_ON_FALSE(s.mutex, ESP_ERR_INVALID_STATE, TAG, "not started");
    lock();
    const bool full = s.timers.count >= TIMER_MAX;
    const uint8_t got = full ? 0 : timer_set_start(&s.timers, duration_ms, mono_ms());
    unlock();
    if (got == 0) {
        return full ? ESP_ERR_NO_MEM : ESP_ERR_INVALID_ARG;
    }
    if (id) {
        *id = got;
    }
    return post(MSG_TIMERS_CHANGED);
}

esp_err_t svc_alarm_timer_action(uint8_t id, svc_timer_action_t action)
{
    ESP_RETURN_ON_FALSE(s.mutex, ESP_ERR_INVALID_STATE, TAG, "not started");
    const uint32_t now = mono_ms();
    lock();
    bool ok = false;
    switch (action) {
    case SVC_TIMER_PAUSE:
        ok = timer_set_pause(&s.timers, id, now);
        break;
    case SVC_TIMER_RESUME:
        ok = timer_set_resume(&s.timers, id, now);
        break;
    case SVC_TIMER_RESTART:
        ok = timer_set_restart(&s.timers, id, now);
        break;
    case SVC_TIMER_REMOVE:
        ok = timer_set_remove(&s.timers, id);
        break;
    }
    // A ringing timer that is removed or restarted stops ringing.
    const bool stop = ok && s.ringing && s.ring.kind == SVC_ALARM_RING_TIMER && s.ring.id == id &&
                      (action == SVC_TIMER_REMOVE || action == SVC_TIMER_RESTART);
    unlock();
    if (!ok) {
        return ESP_ERR_NOT_FOUND;
    }
    if (stop) {
        post(MSG_DISMISS); // the timer is gone or running: removing it again does nothing
    }
    return post(MSG_TIMERS_CHANGED);
}

esp_err_t svc_alarm_snooze(void)
{
    return post(MSG_SNOOZE);
}

esp_err_t svc_alarm_dismiss(void)
{
    return post(MSG_DISMISS);
}

bool svc_alarm_ringing(svc_alarm_evt_ring_t *out)
{
    if (!s.mutex) {
        return false;
    }
    lock();
    const bool r = s.ringing;
    if (r && out) {
        *out = s.ring;
    }
    unlock();
    return r;
}

void svc_alarm_get_stats(svc_alarm_stats_t *out)
{
    if (!s.mutex) {
        memset(out, 0, sizeof *out);
        return;
    }
    lock();
    *out = s.st;
    unlock();
    out->stack_free = s.task ? uxTaskGetStackHighWaterMark(s.task) : 0;
}
