// Battery app ("battery") and charging screen ("charging"), docs/04-ui-ux.md §4d.
#include <stdio.h>

#include "battery_priv.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define APP_RING_D       260
#define APP_RING_W       18
#define CHG_RING_D       300
#define CHG_RING_W       20
#define GRAPH_BARS       24 // one per hour
#define GRAPH_W          312
#define GRAPH_H          96
#define BAR_W            9
// Charging screen: a bright segment runs around the ring, 15 degrees per step at
// 10 fps (one turn in 2.4 s), only while the screen is visible and charging.
#define SWEEP_MS         100
#define SWEEP_STEP_DEG   15
#define SWEEP_LEN_DEG    30

static lv_obj_t *plain(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

// "80" in the display font and "%" in the title font, baseline-ish aligned.
static lv_obj_t *percent_row(lv_obj_t *parent, lv_obj_t **num)
{
    lv_obj_t *row = plain(parent);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    *num = shell_label(row, "--", ui_font_display_tabular(), UI_COLOR_TEXT);
    lv_obj_t *pct = shell_label(row, "%", UI_FONT_TITLE, UI_COLOR_TEXT_DIM);
    lv_obj_set_style_pad_bottom(pct, 18, 0);
    return row;
}

static void set_percent(lv_obj_t *num, lv_obj_t *ring, int percent)
{
    if (percent >= 0) {
        lv_label_set_text_fmt(num, "%d", percent);
    } else {
        lv_label_set_text(num, "--");
    }
    s3w_ring_set_value(ring, percent >= 0 ? percent : 0);
    lv_obj_set_style_arc_color(ring, ui_color(battery_level_color(percent)), LV_PART_INDICATOR);
}

// --- Battery app ------------------------------------------------------------------------------

typedef struct {
    ui_screen_t *screen;
    lv_obj_t *ring;
    lv_obj_t *num;
    lv_obj_t *state;
    lv_obj_t *estimate;
    lv_obj_t *bars[GRAPH_BARS];
    lv_obj_t *saver;
    lv_obj_t *volts;
} app_t;

// Last reading of each hour (the newest slot with one), or BATTERY_HIST_NONE.
static uint8_t hour_value(const uint8_t *history, int hour)
{
    const int per = BATTERY_HIST_SLOTS / GRAPH_BARS;
    for (int i = per - 1; i >= 0; i--) {
        const uint8_t v = history[hour * per + i];
        if (v != BATTERY_HIST_NONE) {
            return v;
        }
    }
    return BATTERY_HIST_NONE;
}

static void app_fill(app_t *a)
{
    battery_info_t b;
    battery_backend()->read(&b, battery_backend()->ctx);
    set_percent(a->num, a->ring, b.percent);
    lv_label_set_text(a->state, battery_state_text(&b));
    char buf[48];
    battery_estimate_text(&b, buf, sizeof buf);
    lv_label_set_text(a->estimate, buf);

    for (int i = 0; i < GRAPH_BARS; i++) {
        const uint8_t v = hour_value(b.history, i);
        if (v == BATTERY_HIST_NONE) {
            lv_obj_add_flag(a->bars[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const int pct = BATTERY_HIST_PCT(v);
        lv_obj_remove_flag(a->bars[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_height(a->bars[i], LV_MAX(4, pct * GRAPH_H / 100));
        const lv_color_t c = (v & BATTERY_HIST_CHARGING) ? ui_theme_accent_color() : ui_color(battery_level_color(pct));
        lv_obj_set_style_bg_color(a->bars[i], c, 0);
    }

    if (lv_obj_has_state(a->saver, LV_STATE_CHECKED) != b.saver) {
        lv_obj_set_state(a->saver, LV_STATE_CHECKED, b.saver);
    }
    if (b.mv > 0) {
        snprintf(buf, sizeof buf, "%u.%02u V", b.mv / 1000u, (b.mv % 1000u) / 10u);
    } else {
        snprintf(buf, sizeof buf, "--");
    }
    // The row's last child is the trailing value (s3w_list_add_row).
    lv_label_set_text(lv_obj_get_child(a->volts, -1), buf);
}

static void saver_changed(lv_event_t *e)
{
    const bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    battery_backend()->set_saver(on, battery_backend()->ctx);
    battery_apps_changed();
}

static void watch_only_confirmed(bool ok, void *ctx)
{
    (void)ctx;
    if (ok) {
        battery_backend()->watch_only(battery_backend()->ctx);
    }
}

static void watch_only_clicked(lv_event_t *e)
{
    app_t *a = lv_event_get_user_data(e);
    s3w_dialog_show(ui_screen_root(a->screen), "Watch only?", "Shows the time only. Press PWR to exit.", "Start", false,
                    watch_only_confirmed, NULL);
}

static void app_create(ui_screen_t *s, lv_obj_t *root, const void *args)
{
    (void)args;
    app_t *a = ui_screen_state(s);
    a->screen = s;
    lv_obj_t *list = s3w_list_create(root);
    s3w_header_create(list, "Battery");

    a->ring = s3w_ring_create(list, APP_RING_D, APP_RING_W, UI_COLOR_SUCCESS);
    lv_obj_t *mid = percent_row(a->ring, &a->num);
    lv_obj_center(mid);

    a->state = shell_label(list, "", UI_FONT_BODY, UI_COLOR_TEXT);
    lv_obj_set_style_pad_top(a->state, UI_SPACE_S, 0);
    a->estimate = shell_label(list, "", UI_FONT_CAPTION, UI_COLOR_TEXT_DIM);

    s3w_list_add_section(list, "Last 24 hours");
    lv_obj_t *graph = plain(list);
    lv_obj_set_size(graph, GRAPH_W, GRAPH_H);
    lv_obj_set_flex_flow(graph, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(graph, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    for (int i = 0; i < GRAPH_BARS; i++) {
        lv_obj_t *bar = plain(graph);
        lv_obj_set_size(bar, BAR_W, 4);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(bar, 3, 0);
        a->bars[i] = bar;
    }
    lv_obj_t *axis = plain(list);
    lv_obj_set_width(axis, GRAPH_W);
    shell_label(axis, "24 h ago", UI_FONT_CAPTION, UI_COLOR_TEXT_DIM);
    lv_obj_align(shell_label(axis, "Now", UI_FONT_CAPTION, UI_COLOR_TEXT_DIM), LV_ALIGN_TOP_RIGHT, 0, 0);

    a->saver = s3w_toggle_row(list, LV_SYMBOL_BATTERY_1, "Battery saver", false);
    lv_obj_add_event_cb(a->saver, saver_changed, LV_EVENT_VALUE_CHANGED, a);
    lv_obj_t *wo = s3w_list_add_row(list, LV_SYMBOL_EYE_CLOSE, "Watch only", "Time only, PWR to exit", NULL);
    lv_obj_add_event_cb(wo, watch_only_clicked, LV_EVENT_CLICKED, a);
    a->volts = s3w_list_add_row(list, LV_SYMBOL_CHARGE, "Voltage", NULL, "--");
    lv_obj_remove_flag(a->volts, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_bottom(list, UI_SPACE_XL, 0);

    app_fill(a);
}

static void app_resume(ui_screen_t *s)
{
    app_fill(ui_screen_state(s));
}

void battery_app_refresh(ui_screen_t *s)
{
    app_fill(ui_screen_state(s));
}

const screen_def_t battery_app_screen = {
    .id = "battery",
    .on_create = app_create,
    .on_resume = app_resume,
    .state_size = sizeof(app_t),
};

// --- Charging screen --------------------------------------------------------------------------

typedef struct {
    lv_obj_t *ring;
    lv_obj_t *sweep;
    lv_obj_t *num;
    lv_obj_t *state;
    lv_obj_t *estimate;
    lv_timer_t *timer;
    uint16_t angle;
} chg_t;

static void sweep_tick(lv_timer_t *t)
{
    chg_t *c = lv_timer_get_user_data(t);
    c->angle = (uint16_t)((c->angle + SWEEP_STEP_DEG) % 360);
    lv_arc_set_rotation(c->sweep, c->angle);
}

static void chg_fill(chg_t *c)
{
    battery_info_t b;
    battery_backend()->read(&b, battery_backend()->ctx);
    set_percent(c->num, c->ring, b.percent);
    lv_label_set_text(c->state, battery_state_text(&b));
    char buf[48];
    battery_estimate_text(&b, buf, sizeof buf);
    lv_label_set_text(c->estimate, buf);
    if (b.charging) {
        lv_obj_remove_flag(c->sweep, LV_OBJ_FLAG_HIDDEN);
        lv_timer_resume(c->timer);
    } else {
        lv_obj_add_flag(c->sweep, LV_OBJ_FLAG_HIDDEN);
        lv_timer_pause(c->timer);
    }
}

static void chg_clicked(lv_event_t *e)
{
    (void)e;
    ui_nav_back(); // the face is one tap away
}

static void chg_create(ui_screen_t *s, lv_obj_t *root, const void *args)
{
    (void)args;
    chg_t *c = ui_screen_state(s);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(root, chg_clicked, LV_EVENT_CLICKED, c);

    c->ring = s3w_ring_create(root, CHG_RING_D, CHG_RING_W, UI_COLOR_SUCCESS);
    lv_obj_align(c->ring, LV_ALIGN_CENTER, 0, -40);
    c->sweep = s3w_ring_create(c->ring, CHG_RING_D, CHG_RING_W, UI_COLOR_TEXT);
    lv_obj_center(c->sweep);
    lv_obj_set_style_arc_opa(c->sweep, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(c->sweep, LV_OPA_60, LV_PART_INDICATOR);
    lv_arc_set_bg_angles(c->sweep, 0, SWEEP_LEN_DEG);
    lv_arc_set_range(c->sweep, 0, SWEEP_LEN_DEG);
    lv_arc_set_value(c->sweep, SWEEP_LEN_DEG);
    lv_arc_set_rotation(c->sweep, 0);

    lv_obj_t *mid = plain(c->ring);
    lv_obj_set_flex_flow(mid, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(mid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    shell_label(mid, LV_SYMBOL_CHARGE, UI_FONT_TITLE, UI_COLOR_SUCCESS);
    percent_row(mid, &c->num);
    lv_obj_center(mid);

    // Centred labels below the ring (lv_obj_align keeps them centred as the text changes).
    c->state = shell_label(root, "", UI_FONT_BODY, UI_COLOR_TEXT);
    lv_obj_align(c->state, LV_ALIGN_CENTER, 0, CHG_RING_D / 2 - 40 + UI_SPACE_L + 14);
    c->estimate = shell_label(root, "", UI_FONT_CAPTION, UI_COLOR_TEXT_DIM);
    lv_obj_align(c->estimate, LV_ALIGN_CENTER, 0, CHG_RING_D / 2 - 40 + UI_SPACE_L + 14 + 36);

    c->timer = ui_screen_timer_create(s, sweep_tick, SWEEP_MS, c);
    chg_fill(c);
}

static void chg_resume(ui_screen_t *s)
{
    chg_fill(ui_screen_state(s)); // also pauses the sweep again when not charging
}

void battery_charging_refresh(ui_screen_t *s)
{
    chg_fill(ui_screen_state(s));
}

const screen_def_t battery_charging_screen = {
    .id = "charging",
    .on_create = chg_create,
    .on_resume = chg_resume,
    .state_size = sizeof(chg_t),
};
