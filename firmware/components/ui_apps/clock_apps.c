// Clock apps: setup, backend, ringing, face data and helpers shared by the clock
// screens (clock_apps.h).
#include <stdio.h>
#include <string.h>

#include "clock_priv.h"
#include "ui_overlay.h"
#include "ui_theme.h"
#include "wf_engine.h"

// --- Backend --------------------------------------------------------------------------------

static void stub_alarms(alarm_set_t *out, void *ctx)
{
    (void)ctx;
    alarm_set_init(out);
}

static esp_err_t stub_alarm_put(const alarm_t *a, void *ctx)
{
    (void)a;
    (void)ctx;
    return ESP_ERR_NOT_SUPPORTED;
}

static void stub_alarm_delete(uint8_t id, void *ctx)
{
    (void)id;
    (void)ctx;
}

static int64_t stub_alarm_next(void *ctx)
{
    (void)ctx;
    return 0;
}

static void stub_timers(timer_set_t *out, uint32_t *now_ms, void *ctx)
{
    (void)ctx;
    timer_set_init(out);
    *now_ms = lv_tick_get();
}

static esp_err_t stub_timer_start(uint32_t duration_ms, void *ctx)
{
    (void)duration_ms;
    (void)ctx;
    return ESP_ERR_NOT_SUPPORTED;
}

static void stub_timer_action(uint8_t id, clock_timer_action_t action, void *ctx)
{
    (void)id;
    (void)action;
    (void)ctx;
}

static void stub_ring(void *ctx)
{
    (void)ctx;
}

static clock_backend_t s_backend = {
    .alarms = stub_alarms,
    .alarm_put = stub_alarm_put,
    .alarm_delete = stub_alarm_delete,
    .alarm_next = stub_alarm_next,
    .timers = stub_timers,
    .timer_start = stub_timer_start,
    .timer_action = stub_timer_action,
    .ring_snooze = stub_ring,
    .ring_dismiss = stub_ring,
};

void clock_apps_set_backend(const clock_backend_t *b)
{
    s_backend.alarms = b && b->alarms ? b->alarms : stub_alarms;
    s_backend.alarm_put = b && b->alarm_put ? b->alarm_put : stub_alarm_put;
    s_backend.alarm_delete = b && b->alarm_delete ? b->alarm_delete : stub_alarm_delete;
    s_backend.alarm_next = b && b->alarm_next ? b->alarm_next : stub_alarm_next;
    s_backend.timers = b && b->timers ? b->timers : stub_timers;
    s_backend.timer_start = b && b->timer_start ? b->timer_start : stub_timer_start;
    s_backend.timer_action = b && b->timer_action ? b->timer_action : stub_timer_action;
    s_backend.ring_snooze = b && b->ring_snooze ? b->ring_snooze : stub_ring;
    s_backend.ring_dismiss = b && b->ring_dismiss ? b->ring_dismiss : stub_ring;
    s_backend.ctx = b ? b->ctx : NULL;
    clock_apps_changed();
}

const clock_backend_t *clock_backend(void)
{
    return &s_backend;
}

// --- Face data -----------------------------------------------------------------------------

// Next alarm and the earliest running timer (minute-resolution complications, tiles).
static void feed_face(void)
{
    const int64_t next = s_backend.alarm_next(s_backend.ctx);
    timer_set_t t;
    uint32_t now_ms = 0;
    s_backend.timers(&t, &now_ms, s_backend.ctx);
    uint32_t end = 0;
    time_t timer_end = 0;
    if (timer_set_next_end(&t, &end)) {
        const int32_t left = (int32_t)(end - now_ms);
        timer_end = ui_clock_now() + (left > 0 ? (left + 999) / 1000 : 0);
    }
    wf_data_t *d = wf_data_edit();
    uint32_t changed = 0;
    if (d->alarm_next != (time_t)next) {
        d->alarm_next = (time_t)next;
        changed |= WF_DATA_ALARM;
    }
    if (d->timer_end != timer_end) {
        d->timer_end = timer_end;
        changed |= WF_DATA_TIMER;
    }
    if (changed) {
        wf_data_changed(changed);
    }
}

static const world_city_t *s_world[WORLD_CLOCK_MAX];
static size_t s_world_n;
static void (*s_world_cb)(const char *csv, void *ctx);
static void *s_world_ctx;

// First city -> world time complication; its offset is re-read every minute (DST).
static void feed_world(void)
{
    wf_data_t *d = wf_data_edit();
    const bool valid = s_world_n > 0;
    const int32_t off = valid ? world_city_offset(s_world[0], ui_clock_now()) : 0;
    const char *label = valid ? s_world[0]->label : "";
    if (d->world_valid == valid && d->world_utc_offset_s == off && strcmp(d->world_label, label) == 0) {
        return;
    }
    d->world_valid = valid;
    d->world_utc_offset_s = off;
    snprintf(d->world_label, sizeof d->world_label, "%s", label);
    wf_data_changed(WF_DATA_WORLD);
}

// The first city's offset changes at its DST transitions: re-read it each minute.
static void on_minute(void *ctx)
{
    (void)ctx;
    if (wf_data_get()->world_valid) {
        feed_world();
    }
}

static bool s_refresh_pending;

// Deferred: a change often comes from a tap on a row that the refresh deletes.
static void refresh(void *arg)
{
    (void)arg;
    s_refresh_pending = false;
    feed_face();
    ui_screen_t *top = ui_nav_top();
    const screen_def_t *def = top ? ui_screen_def(top) : NULL;
    if (def == &clock_alarms_screen) {
        clock_alarms_refresh(top);
    } else if (def == &clock_timer_screen) {
        clock_timer_refresh(top);
    }
}

void clock_apps_changed(void)
{
    if (!s_refresh_pending && lv_async_call(refresh, NULL) == LV_RESULT_OK) {
        s_refresh_pending = true;
    }
}

void clock_apps_init(void)
{
    static bool s_done;
    if (s_done) {
        return;
    }
    s_done = true;
    ui_nav_register(&clock_alarms_screen);
    ui_nav_register(&clock_alarm_edit_screen);
    ui_nav_register(&clock_timer_screen);
    ui_nav_register(&clock_timer_custom_screen);
    ui_nav_register(&clock_stopwatch_screen);
    ui_nav_register(&clock_world_screen);
    ui_nav_register(&clock_world_add_screen);
    ui_clock_add_listener(on_minute, NULL);
}

// --- Ringing ------------------------------------------------------------------------------

static ui_screen_t *s_ring; // the open ring screen
static clock_ring_t s_ring_info;

static void ring_close(void)
{
    ui_screen_t *r = s_ring;
    s_ring = NULL;
    if (r && ui_nav_top() == r) {
        ui_nav_pop();
    }
}

void clock_apps_ring(const clock_ring_t *ring)
{
    if (s_ring && s_ring_info.kind == ring->kind && s_ring_info.id == ring->id) {
        return;
    }
    ring_close();
    ui_overlay_clear(); // an alert on the top layer would hide the ring screen
    if (ui_nav_depth() >= UI_NAV_MAX_DEPTH) {
        ui_nav_home();
    }
    s_ring_info = *ring;
    if (ui_nav_push(&clock_ring_screen, ring) == ESP_OK) {
        s_ring = ui_nav_top();
    }
}

void clock_apps_ring_end(bool snoozed, uint8_t snooze_min)
{
    ring_close();
    if (snoozed) {
        char msg[32];
        snprintf(msg, sizeof msg, "Snoozed for %u min", snooze_min);
        ui_toast_show(msg, 0);
    }
}

bool clock_apps_ring_active(void)
{
    return s_ring != NULL;
}

void clock_apps_ring_key(void)
{
    if (s_ring == NULL) {
        return;
    }
    const bool alarm = s_ring_info.kind == CLOCK_RING_ALARM;
    ring_close();
    if (alarm) {
        s_backend.ring_snooze(s_backend.ctx);
    } else {
        s_backend.ring_dismiss(s_backend.ctx);
    }
}

void clock_ring_closed(const ui_screen_t *s)
{
    if (s_ring == s) {
        s_ring = NULL;
    }
}

// --- World clock list -------------------------------------------------------------------------

void clock_apps_set_world(const char *csv)
{
    s_world_n = world_clock_parse(csv, s_world, WORLD_CLOCK_MAX);
    feed_world();
}

void clock_apps_set_world_listener(void (*cb)(const char *csv, void *ctx), void *ctx)
{
    s_world_cb = cb;
    s_world_ctx = ctx;
}

size_t clock_world_cities(const world_city_t **out)
{
    memcpy(out, s_world, s_world_n * sizeof s_world[0]);
    return s_world_n;
}

void clock_world_save(const world_city_t *const *cities, size_t n)
{
    s_world_n = n < WORLD_CLOCK_MAX ? n : WORLD_CLOCK_MAX;
    memcpy(s_world, cities, s_world_n * sizeof s_world[0]);
    feed_world();
    if (s_world_cb) {
        char csv[WORLD_CLOCK_MAX * 16];
        world_clock_join(s_world, s_world_n, csv, sizeof csv);
        s_world_cb(csv, s_world_ctx);
    }
}

// Days since 1970-01-01 of a proleptic Gregorian date.
static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}

int32_t clock_home_offset(time_t now)
{
    struct tm lt;
    localtime_r(&now, &lt);
    const int64_t local = days_from_civil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday) * 86400 +
                          lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec;
    return (int32_t)(local - (int64_t)now);
}

// --- Helpers --------------------------------------------------------------------------------

lv_obj_t *clock_plain(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

lv_obj_t *clock_round_button(lv_obj_t *parent, int32_t d, uint32_t color, const char *text, const lv_font_t *font)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, d, d);
    lv_obj_set_style_min_width(b, 0, 0); // the theme's 64 px button minimum would stretch it
    lv_obj_set_style_min_height(b, 0, 0);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, ui_color(color), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_t *l = shell_label(b, text, font, UI_COLOR_TEXT);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l);
    return b;
}

void clock_fmt_hm(int hour, int minute, char *buf, size_t len)
{
    if (ui_clock_is_24h()) {
        snprintf(buf, len, "%02d:%02d", hour, minute);
    } else {
        const int h12 = hour % 12 ? hour % 12 : 12;
        snprintf(buf, len, "%d:%02d %s", h12, minute, hour < 12 ? "AM" : "PM");
    }
}

void clock_fmt_duration(uint32_t ms, char *buf, size_t len)
{
    const uint32_t s = (ms + 999) / 1000;
    if (s >= 3600) {
        snprintf(buf, len, "%lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60),
                 (unsigned long)(s % 60));
    } else {
        snprintf(buf, len, "%02lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
    }
}

void clock_fmt_until(int64_t secs, char *buf, size_t len)
{
    const int64_t min = (secs + 59) / 60; // "in 1 min" until it rings
    if (secs < 60) {
        snprintf(buf, len, "in under a minute");
    } else if (min < 60) {
        snprintf(buf, len, "in %d min", (int)min);
    } else if (min % 60 == 0) {
        snprintf(buf, len, "in %d h", (int)(min / 60));
    } else {
        snprintf(buf, len, "in %d h %d min", (int)(min / 60), (int)(min % 60));
    }
}
