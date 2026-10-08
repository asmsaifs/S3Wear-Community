// Power menu (PWR held 2 s, docs/03-firmware-features.md F3): battery saver,
// watch-only mode, restart, power off. The UI does not call svc_power: the app
// passes a handler in the push args, so the screen also runs in the simulator.
#include <stdio.h>

#include "ui_overlay.h"
#include "ui_screens.h"
#include "ui_theme.h"
#include "ui_widgets.h"

typedef struct {
    ui_power_menu_args_t args;
    ui_screen_t *screen;
} power_menu_t;

static void act(power_menu_t *m, ui_power_action_t action)
{
    if (m->args.on_action) {
        m->args.on_action(action, m->args.ctx);
    } else {
        ui_toast_show("Not available", 0);
    }
}

static void confirmed(bool ok, void *ctx, ui_power_action_t action)
{
    if (ok) {
        act(ctx, action);
    }
}

static void off_confirmed(bool ok, void *ctx)
{
    confirmed(ok, ctx, UI_POWER_ACTION_OFF);
}

static void restart_confirmed(bool ok, void *ctx)
{
    confirmed(ok, ctx, UI_POWER_ACTION_RESTART);
}

static void watch_only_confirmed(bool ok, void *ctx)
{
    confirmed(ok, ctx, UI_POWER_ACTION_WATCH_ONLY);
}

static void saver_cb(lv_event_t *e)
{
    power_menu_t *m = lv_event_get_user_data(e);
    const bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    act(m, on ? UI_POWER_ACTION_SAVER_ON : UI_POWER_ACTION_SAVER_OFF);
}

static void watch_only_cb(lv_event_t *e)
{
    power_menu_t *m = lv_event_get_user_data(e);
    s3w_dialog_show(ui_screen_root(m->screen), "Watch only?", "Shows the time only. Press PWR to exit.", "Start",
                    false, watch_only_confirmed, m);
}

static void restart_cb(lv_event_t *e)
{
    power_menu_t *m = lv_event_get_user_data(e);
    s3w_dialog_show(ui_screen_root(m->screen), "Restart?", NULL, "Restart", false, restart_confirmed, m);
}

static void off_cb(lv_event_t *e)
{
    power_menu_t *m = lv_event_get_user_data(e);
    s3w_dialog_show(ui_screen_root(m->screen), "Power off?", "Press PWR to turn on.", "Power off", true,
                    off_confirmed, m);
}

static void power_menu_create(ui_screen_t *s, lv_obj_t *root, const void *args)
{
    power_menu_t *m = ui_screen_state(s);
    m->screen = s;
    if (args) {
        m->args = *(const ui_power_menu_args_t *)args;
    } else {
        m->args.battery_pct = -1;
    }

    lv_obj_t *list = s3w_list_create(root);
    s3w_header_create(list, "Power");

    lv_obj_set_style_pad_row(list, UI_SPACE_M, 0);
    lv_obj_t *off = s3w_button_create(list, S3W_BUTTON_DANGER, LV_SYMBOL_POWER "  Power off");
    lv_obj_set_width(off, LV_PCT(90));
    lv_obj_add_event_cb(off, off_cb, LV_EVENT_CLICKED, m);
    lv_obj_t *restart = s3w_button_create(list, S3W_BUTTON_SECONDARY, LV_SYMBOL_REFRESH "  Restart");
    lv_obj_set_width(restart, LV_PCT(90));
    lv_obj_add_event_cb(restart, restart_cb, LV_EVENT_CLICKED, m);
    lv_obj_t *saver = s3w_toggle_row(list, LV_SYMBOL_BATTERY_1, "Battery saver", m->args.saver);
    lv_obj_add_event_cb(saver, saver_cb, LV_EVENT_VALUE_CHANGED, m);
    lv_obj_t *watch = s3w_list_add_row(list, LV_SYMBOL_EYE_CLOSE, "Watch only", "Time only, PWR to exit", NULL);
    lv_obj_add_event_cb(watch, watch_only_cb, LV_EVENT_CLICKED, m);

    char pct[8] = "--";
    if (m->args.battery_pct >= 0) {
        snprintf(pct, sizeof pct, "%d %%", m->args.battery_pct);
    }
    s3w_list_add_row(list, m->args.charging ? LV_SYMBOL_CHARGE : LV_SYMBOL_BATTERY_3, "Battery",
                     m->args.charging ? "Charging" : NULL, pct);
}

const screen_def_t ui_power_menu_screen = {
    .id = "power",
    .on_create = power_menu_create,
    .state_size = sizeof(power_menu_t),
};
