// Stopwatch (docs/03 F6): start/stop, lap/reset, lap list (newest first). The state
// lives here, outside the screen, on the LVGL tick: it keeps running while the app is
// closed or the screen is off.
#include <stdio.h>

#include "clock_priv.h"
#include "stopwatch.h"
#include "ui_theme.h"
#include "ui_widgets.h"

// Display refresh only while running and visible: hundredths at 20 fps.
#define TICK_MS 50

static stopwatch_t s_sw;

typedef struct {
    lv_obj_t *time;
    lv_obj_t *cs;
    lv_obj_t *left;  // Lap / Reset
    lv_obj_t *right; // Start / Stop
    lv_obj_t *laps;
    lv_timer_t *timer;
} sw_screen_t;

static void show_time(sw_screen_t *st)
{
    char buf[16];
    uint8_t cs = 0;
    const uint32_t ms = stopwatch_elapsed(&s_sw, lv_tick_get());
    stopwatch_format(ms, buf, sizeof buf, &cs);
    lv_label_set_text(st->time, buf);
    if (ms >= 3600000u) {
        lv_obj_add_flag(st->cs, LV_OBJ_FLAG_HIDDEN); // H:MM:SS fills the width
    } else {
        lv_obj_remove_flag(st->cs, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(st->cs, ".%02u", cs);
    }
}

static void show_buttons(sw_screen_t *st)
{
    const bool run = s_sw.running;
    lv_label_set_text(lv_obj_get_child(st->right, 0), run ? "Stop" : "Start");
    lv_obj_set_style_bg_color(st->right, ui_color(run ? UI_COLOR_DANGER : UI_COLOR_SUCCESS), 0);
    lv_label_set_text(lv_obj_get_child(st->left, 0), run || stopwatch_elapsed(&s_sw, lv_tick_get()) == 0 ? "Lap" : "Reset");
    if (run) {
        lv_timer_resume(st->timer);
    } else {
        lv_timer_pause(st->timer);
    }
}

static void show_laps(sw_screen_t *st)
{
    lv_obj_clean(st->laps);
    for (int i = s_sw.lap_count - 1; i >= 0; i--) {
        char title[16];
        char hms[16];
        char len[20];
        uint8_t cs = 0;
        snprintf(title, sizeof title, "Lap %u", stopwatch_lap_number(&s_sw, (size_t)i));
        const uint32_t ms = stopwatch_lap_ms(&s_sw, (size_t)i);
        stopwatch_format(ms, hms, sizeof hms, &cs);
        if (ms == 0 && i == 0 && s_sw.lap_total > s_sw.lap_count) {
            snprintf(len, sizeof len, "--"); // its start dropped out of the kept laps
        } else {
            snprintf(len, sizeof len, "%s.%02u", hms, cs % 100u);
        }
        lv_obj_t *row = s3w_list_add_row(st->laps, NULL, title, NULL, len);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
    }
}

static void tick(lv_timer_t *t)
{
    sw_screen_t *st = lv_timer_get_user_data(t);
    show_time(st);
    if (!s_sw.running) {
        lv_timer_pause(t); // resumed by the next Start (the framework resumes it on show)
    }
}

static void right_clicked(lv_event_t *e)
{
    sw_screen_t *st = lv_event_get_user_data(e);
    if (s_sw.running) {
        stopwatch_stop(&s_sw, lv_tick_get());
    } else {
        stopwatch_start(&s_sw, lv_tick_get());
    }
    show_time(st);
    show_buttons(st);
}

static void left_clicked(lv_event_t *e)
{
    sw_screen_t *st = lv_event_get_user_data(e);
    if (s_sw.running) {
        stopwatch_lap(&s_sw, lv_tick_get());
    } else {
        stopwatch_reset(&s_sw);
    }
    show_time(st);
    show_buttons(st);
    show_laps(st);
}

static void sw_create(ui_screen_t *s, lv_obj_t *root, const void *args)
{
    (void)args;
    sw_screen_t *st = ui_screen_state(s);
    lv_obj_t *list = s3w_list_create(root);
    s3w_header_create(list, "Stopwatch");

    lv_obj_t *time = clock_plain(list);
    lv_obj_set_flex_flow(time, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(time, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    st->time = shell_label(time, "", ui_font_display_tabular(), UI_COLOR_TEXT);
    st->cs = shell_label(time, "", UI_FONT_TITLE, UI_COLOR_TEXT_DIM);
    lv_obj_set_style_pad_bottom(st->cs, 16, 0); // near the digits' baseline

    lv_obj_t *buttons = clock_plain(list);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(buttons, 64, 0);
    lv_obj_set_style_pad_ver(buttons, UI_SPACE_M, 0);
    st->left = clock_round_button(buttons, 100, UI_COLOR_SURFACE_HI, "Lap", UI_FONT_BODY);
    lv_obj_add_event_cb(st->left, left_clicked, LV_EVENT_CLICKED, st);
    st->right = clock_round_button(buttons, 100, UI_COLOR_SUCCESS, "Start", UI_FONT_BODY);
    lv_obj_add_event_cb(st->right, right_clicked, LV_EVENT_CLICKED, st);

    st->laps = clock_plain(list);
    lv_obj_set_width(st->laps, LV_PCT(100));
    lv_obj_set_flex_flow(st->laps, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(st->laps, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_bottom(st->laps, UI_SPACE_XL, 0);

    st->timer = ui_screen_timer_create(s, tick, TICK_MS, st);
    show_time(st);
    show_buttons(st);
    show_laps(st);
}

static void sw_resume(ui_screen_t *s)
{
    sw_screen_t *st = ui_screen_state(s);
    show_time(st);
    if (!s_sw.running) {
        lv_timer_pause(st->timer); // the framework resumed it
    }
}

const screen_def_t clock_stopwatch_screen = {
    .id = "stopwatch",
    .on_create = sw_create,
    .on_resume = sw_resume,
    .state_size = sizeof(sw_screen_t),
};
