#include "sim_clock.h"

#include <stdio.h>
#include <string.h>

#include "alarm_sched.h"
#include "clock_apps.h"
#include "lvgl.h"
#include "sim_script.h"
#include "timer_set.h"

/* Checks for finished timers; simulator only (time moves in script steps). */
#define TICK_MS 100

static alarm_set_t s_set;
static timer_set_t s_timers;
static tz_posix_t s_tz; /* UTC0, like the headless runs */
static bool s_ringing;
static clock_ring_t s_ring;

static void ring_start(const clock_ring_t *r)
{
    s_ringing = true;
    s_ring = *r;
    printf("clock: %s %u rings\n", r->kind == CLOCK_RING_ALARM ? "alarm" : "timer", r->id);
    clock_apps_ring(r);
}

static void ring_end(bool snoozed)
{
    s_ringing = false;
    clock_apps_ring_end(snoozed, s_ring.snooze_min);
    clock_apps_changed();
}

static void be_alarms(alarm_set_t *out, void *ctx)
{
    (void)ctx;
    *out = s_set;
}

static esp_err_t be_alarm_put(const alarm_t *a, void *ctx)
{
    (void)ctx;
    if (a->id == 0 && s_set.count >= ALARM_MAX) {
        return ESP_ERR_NO_MEM;
    }
    const uint8_t id = alarm_set_put_at(&s_set, a, &s_tz, sim_fixed_now());
    if (id == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    printf("clock: alarm %u %02u:%02u days %02x %s\n", id, a->hour, a->minute, a->days, a->enabled ? "on" : "off");
    clock_apps_changed();
    return ESP_OK;
}

static void be_alarm_delete(uint8_t id, void *ctx)
{
    (void)ctx;
    printf("clock: alarm %u deleted\n", id);
    alarm_set_remove(&s_set, id);
    clock_apps_changed();
}

static int64_t be_alarm_next(void *ctx)
{
    (void)ctx;
    const int64_t t = alarm_set_next(&s_set, &s_tz, sim_fixed_now(), NULL);
    return t == ALARM_NEVER ? 0 : t;
}

static void be_timers(timer_set_t *out, uint32_t *now_ms, void *ctx)
{
    (void)ctx;
    *out = s_timers;
    *now_ms = lv_tick_get();
}

static esp_err_t be_timer_start(uint32_t ms, void *ctx)
{
    (void)ctx;
    if (s_timers.count >= TIMER_MAX) {
        return ESP_ERR_NO_MEM;
    }
    const uint8_t id = timer_set_start(&s_timers, ms, lv_tick_get());
    if (id == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    printf("clock: timer %u started, %lu ms\n", id, (unsigned long)ms);
    clock_apps_changed();
    return ESP_OK;
}

static void be_timer_action(uint8_t id, clock_timer_action_t action, void *ctx)
{
    (void)ctx;
    const uint32_t now = lv_tick_get();
    switch (action) {
    case CLOCK_TIMER_PAUSE:
        timer_set_pause(&s_timers, id, now);
        break;
    case CLOCK_TIMER_RESUME:
        timer_set_resume(&s_timers, id, now);
        break;
    case CLOCK_TIMER_RESTART:
        timer_set_restart(&s_timers, id, now);
        break;
    case CLOCK_TIMER_REMOVE:
        timer_set_remove(&s_timers, id);
        break;
    }
    printf("clock: timer %u action %d\n", id, (int)action);
    if (s_ringing && s_ring.kind == CLOCK_RING_TIMER && s_ring.id == id &&
        (action == CLOCK_TIMER_REMOVE || action == CLOCK_TIMER_RESTART)) {
        ring_end(false);
    } else {
        clock_apps_changed();
    }
}

static void be_dismiss(void *ctx)
{
    (void)ctx;
    if (!s_ringing) {
        return;
    }
    if (s_ring.kind == CLOCK_RING_TIMER) {
        const countdown_t *c = timer_set_find(&s_timers, s_ring.id);
        if (c && c->state == TIMER_DONE) {
            timer_set_remove(&s_timers, s_ring.id);
        }
    }
    printf("clock: dismissed\n");
    ring_end(false);
}

static void be_snooze(void *ctx)
{
    if (!s_ringing || s_ring.kind != CLOCK_RING_ALARM) {
        be_dismiss(ctx);
        return;
    }
    alarm_set_snooze(&s_set, s_ring.id, sim_fixed_now());
    printf("clock: snoozed %u min\n", s_ring.snooze_min);
    ring_end(true);
}

static void tick(lv_timer_t *t)
{
    (void)t;
    uint8_t ids[TIMER_MAX];
    const size_t n = timer_set_expire(&s_timers, lv_tick_get(), ids, TIMER_MAX);
    if (n == 0) {
        return;
    }
    if (!s_ringing) {
        const countdown_t *c = timer_set_find(&s_timers, ids[0]);
        const clock_ring_t r = {.kind = CLOCK_RING_TIMER, .id = ids[0], .duration_ms = c ? c->duration_ms : 0};
        ring_start(&r);
    }
    clock_apps_changed();
}

void sim_clock_init(void)
{
    alarm_set_init(&s_set);
    timer_set_init(&s_timers);
    tz_posix_parse("UTC0", &s_tz);
    sim_clock_add_alarm(7, 0, ALARM_DAYS_ALL, "");
    sim_clock_add_alarm(8, 30, ALARM_DAYS_WEEKEND, "Run");
    alarm_set_find(&s_set, s_set.items[1].id)->enabled = false;
    const clock_backend_t b = {
        .alarms = be_alarms,
        .alarm_put = be_alarm_put,
        .alarm_delete = be_alarm_delete,
        .alarm_next = be_alarm_next,
        .timers = be_timers,
        .timer_start = be_timer_start,
        .timer_action = be_timer_action,
        .ring_snooze = be_snooze,
        .ring_dismiss = be_dismiss,
    };
    clock_apps_set_backend(&b);
    clock_apps_set_world("tokyo,london,new_york");
    lv_timer_create(tick, TICK_MS, NULL);
}

bool sim_clock_add_alarm(int hour, int minute, uint8_t days, const char *label)
{
    alarm_t a;
    alarm_default(&a, (uint8_t)hour, (uint8_t)minute);
    a.days = days;
    snprintf(a.label, sizeof a.label, "%s", label ? label : "");
    if (alarm_set_put_at(&s_set, &a, &s_tz, sim_fixed_now()) == 0) {
        return false;
    }
    clock_apps_changed();
    return true;
}

void sim_clock_clear_alarms(void)
{
    alarm_set_init(&s_set);
    clock_apps_changed();
}

bool sim_clock_start_timer(uint32_t ms)
{
    return be_timer_start(ms, NULL) == ESP_OK;
}

bool sim_clock_ring(bool alarm)
{
    if (alarm) {
        for (int i = 0; i < s_set.count; i++) {
            const alarm_t *a = &s_set.items[i];
            if (a->enabled) {
                const time_t now = sim_fixed_now();
                clock_ring_t r = {.kind = CLOCK_RING_ALARM, .id = a->id, .snooze_min = a->snooze_min};
                r.at = now - now % 86400 + a->hour * 3600 + a->minute * 60;
                memcpy(r.label, a->label, sizeof r.label);
                ring_start(&r);
                return true;
            }
        }
        return false;
    }
    for (int i = 0; i < s_timers.count; i++) {
        countdown_t *c = &s_timers.items[i];
        if (c->state == TIMER_RUNNING) {
            c->state = TIMER_DONE;
            const clock_ring_t r = {.kind = CLOCK_RING_TIMER, .id = c->id, .duration_ms = c->duration_ms};
            ring_start(&r);
            clock_apps_changed();
            return true;
        }
    }
    return false;
}
