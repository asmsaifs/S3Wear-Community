// Battery screens: backend, charging screen trigger, low-battery flows and the text
// helpers the screens share (battery_apps.h).
#include <stdio.h>

#include "battery_priv.h"
#include "clock_apps.h"
#include "ui_overlay.h"
#include "ui_theme.h"

// --- Backend --------------------------------------------------------------------------------

static void stub_read(battery_info_t *out, void *ctx)
{
    (void)ctx;
    *out = (battery_info_t){.percent = -1, .minutes = -1};
    for (size_t i = 0; i < BATTERY_HIST_SLOTS; i++) {
        out->history[i] = BATTERY_HIST_NONE;
    }
}

static void stub_saver(bool on, void *ctx)
{
    (void)on;
    (void)ctx;
    ui_toast_show("Not available", 0);
}

static void stub_watch_only(void *ctx)
{
    (void)ctx;
    ui_toast_show("Not available", 0);
}

static battery_backend_t s_backend = {.read = stub_read, .set_saver = stub_saver, .watch_only = stub_watch_only};

void battery_apps_set_backend(const battery_backend_t *b)
{
    s_backend.read = b && b->read ? b->read : stub_read;
    s_backend.set_saver = b && b->set_saver ? b->set_saver : stub_saver;
    s_backend.watch_only = b && b->watch_only ? b->watch_only : stub_watch_only;
    s_backend.ctx = b ? b->ctx : NULL;
    battery_apps_changed();
}

const battery_backend_t *battery_backend(void)
{
    return &s_backend;
}

void battery_apps_init(void)
{
    static bool s_done;
    if (s_done) {
        return;
    }
    s_done = true;
    ui_nav_register(&battery_app_screen);
    ui_nav_register(&battery_charging_screen);
}

static bool s_refresh_pending;

// Deferred: a change often comes from a tap (the saver switch) inside the screen.
static void refresh(void *arg)
{
    (void)arg;
    s_refresh_pending = false;
    ui_screen_t *top = ui_nav_top();
    const screen_def_t *def = top ? ui_screen_def(top) : NULL;
    if (def == &battery_app_screen) {
        battery_app_refresh(top);
    } else if (def == &battery_charging_screen) {
        battery_charging_refresh(top);
    }
}

void battery_apps_changed(void)
{
    if (!s_refresh_pending && lv_async_call(refresh, NULL) == LV_RESULT_OK) {
        s_refresh_pending = true;
    }
}

// --- Charging screen --------------------------------------------------------------------------

static bool charging_on_top(void)
{
    const ui_screen_t *top = ui_nav_top();
    return top && ui_screen_def(top) == &battery_charging_screen;
}

static void show_charging(void)
{
    // An alarm or an alert is more important; the face stays one tap away.
    if (charging_on_top() || clock_apps_ring_active() || ui_alert_is_active() || ui_nav_depth() == 0) {
        return;
    }
    if (ui_nav_depth() >= UI_NAV_MAX_DEPTH) {
        ui_nav_home();
    }
    ui_nav_push_slide(&battery_charging_screen, NULL, UI_SLIDE_FROM_BOTTOM);
}

void battery_apps_charger(bool vbus)
{
    if (vbus) {
        show_charging();
    } else if (charging_on_top()) {
        ui_nav_pop();
    }
}

void battery_apps_screen_on(void)
{
    battery_info_t b;
    s_backend.read(&b, s_backend.ctx);
    if (b.vbus && ui_nav_depth() == 1) {
        show_charging();
    }
}

// --- Low battery ------------------------------------------------------------------------------

static void saver_result(int button, void *ctx)
{
    (void)ctx;
    if (button == 0) {
        s_backend.set_saver(true, s_backend.ctx);
        ui_toast_show("Battery saver on", 0);
    }
}

static void watch_only_result(int button, void *ctx)
{
    (void)ctx;
    if (button == 0) {
        s_backend.watch_only(s_backend.ctx);
    }
}

void battery_apps_low(uint8_t threshold, int8_t percent)
{
    char title[24];
    snprintf(title, sizeof title, "Battery %d%%", percent);
    battery_info_t b;
    s_backend.read(&b, s_backend.ctx);
    if (threshold > 10 || (threshold > 3 && b.saver)) {
        // 15 %: a toast (svc_audio plays the sound). 10 % with saver already on: the same.
        ui_toast_show(title, 3000);
        return;
    }
    if (threshold > 3) {
        const ui_alert_t a = {
            .icon = LV_SYMBOL_BATTERY_1,
            .title = title,
            .body = "Turn on battery saver? Dimmer screen, no always-on display or raise to wake.",
            .accent = 0, // theme accent: white text on yellow would be hard to read
            .primary = "Turn on",
            .secondary = "Not now",
            .on_result = saver_result,
            .prio = UI_ALERT_PRIO_LOW,
            .back_dismisses = true,
        };
        ui_alert_show(&a);
        return;
    }
    const ui_alert_t a = {
        .icon = LV_SYMBOL_BATTERY_EMPTY,
        .title = title,
        .body = "Watch only starts in a minute: the time only, PWR to exit. Charge soon.",
        .accent = UI_COLOR_DANGER,
        .primary = "Start now",
        .secondary = "OK",
        .on_result = watch_only_result,
        .prio = UI_ALERT_PRIO_NORMAL,
        .back_dismisses = true,
    };
    ui_alert_show(&a);
}

// --- Text -------------------------------------------------------------------------------------

void battery_fmt_minutes(int32_t minutes, char *buf, size_t len)
{
    if (minutes < 60) {
        snprintf(buf, len, "%d min", (int)minutes);
    } else if (minutes < 24 * 60) {
        if (minutes % 60) {
            snprintf(buf, len, "%d h %d min", (int)(minutes / 60), (int)(minutes % 60));
        } else {
            snprintf(buf, len, "%d h", (int)(minutes / 60));
        }
    } else {
        const int32_t h = (minutes + 30) / 60; // nearest hour
        if (h % 24) {
            snprintf(buf, len, "%d d %d h", (int)(h / 24), (int)(h % 24));
        } else {
            snprintf(buf, len, "%d d", (int)(h / 24));
        }
    }
}

uint32_t battery_level_color(int percent)
{
    return percent < 0 ? UI_COLOR_TEXT_DIM : percent <= 10 ? UI_COLOR_DANGER : percent <= 20 ? UI_COLOR_WARNING
                                                                                                : UI_COLOR_SUCCESS;
}

const char *battery_state_text(const battery_info_t *b)
{
    if (b->charging) {
        return "Charging";
    }
    if (b->vbus) {
        return b->percent >= 100 ? "Charged" : "Not charging";
    }
    return b->saver ? "Battery saver on" : "On battery";
}

void battery_estimate_text(const battery_info_t *b, char *buf, size_t len)
{
    char t[24];
    if (b->percent < 0) {
        snprintf(buf, len, "No battery reading");
    } else if (b->vbus && !b->charging) {
        snprintf(buf, len, "%s", ""); // the state line says it: "Charged" / "Not charging"
    } else if (b->minutes < 0) {
        snprintf(buf, len, b->charging ? "Estimating time to full" : "Estimating time left");
    } else if (b->charging) {
        battery_fmt_minutes(b->minutes, t, sizeof t);
        snprintf(buf, len, "Full in about %s", t);
    } else {
        battery_fmt_minutes(b->minutes, t, sizeof t);
        snprintf(buf, len, "About %s left", t);
    }
}
