// Ring screen (docs/03 F6, docs/04 §4c): full screen while an alarm or timer rings.
// Alarm: the time, the label, Snooze, and a slider that must be dragged to the end to
// stop it (a tap cannot dismiss an alarm). Timer: the duration, Stop and Restart.
// BACK and edge swipes do nothing; PWR snoozes (clock_apps_ring_key()). Flip to
// snooze is detected by the backend, which then closes this screen.
#include <stdio.h>

#include "clock_priv.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define DISMISS_AT 85 // slider value (0..100) that counts as "dragged to the end"

typedef struct {
    ui_screen_t *screen;
    clock_ring_t ring;
} ring_t;

// Close this screen, then tell the backend (which may report the end synchronously).
static void finish(ring_t *r, void (*action)(void *ctx))
{
    ui_screen_t *s = r->screen;
    clock_ring_closed(s);
    if (ui_nav_top() == s) {
        ui_nav_pop();
    }
    action(clock_backend()->ctx);
}

static void snooze_clicked(lv_event_t *e)
{
    finish(lv_event_get_user_data(e), clock_backend()->ring_snooze);
}

static void stop_clicked(lv_event_t *e)
{
    finish(lv_event_get_user_data(e), clock_backend()->ring_dismiss);
}

static void restart_clicked(lv_event_t *e)
{
    ring_t *r = lv_event_get_user_data(e);
    const uint8_t id = r->ring.id;
    ui_screen_t *s = r->screen;
    clock_ring_closed(s);
    if (ui_nav_top() == s) {
        ui_nav_pop();
    }
    clock_backend()->timer_action(id, CLOCK_TIMER_RESTART, clock_backend()->ctx); // stops the ringing too
}

static void slider_released(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target_obj(e);
    if (lv_slider_get_value(slider) >= DISMISS_AT) {
        finish(lv_event_get_user_data(e), clock_backend()->ring_dismiss);
    } else {
        lv_slider_set_value(slider, 0, LV_ANIM_ON);
    }
}

static lv_obj_t *dismiss_slider(lv_obj_t *root, ring_t *r)
{
    lv_obj_t *sl = lv_slider_create(root);
    lv_obj_set_size(sl, 320, 72);
    lv_slider_set_range(sl, 0, 100);
    lv_obj_add_flag(sl, LV_OBJ_FLAG_ADV_HITTEST); // only the knob can be grabbed: a tap does nothing
    lv_obj_set_style_bg_color(sl, ui_color(UI_COLOR_SURFACE_HI), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(sl, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(sl, 36, LV_PART_MAIN); // the knob's centre travels inside the track
    lv_obj_set_style_bg_opa(sl, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, ui_color(UI_COLOR_DANGER), LV_PART_KNOB);
    lv_obj_set_style_pad_all(sl, -4, LV_PART_KNOB); // 64 px knob in the 72 px track
    lv_obj_set_style_shadow_width(sl, 0, LV_PART_KNOB);
    lv_obj_t *hint = shell_label(sl, "Slide to stop  " LV_SYMBOL_RIGHT, UI_FONT_BODY, UI_COLOR_TEXT_DIM);
    lv_obj_align(hint, LV_ALIGN_CENTER, 24, 0);
    lv_obj_add_event_cb(sl, slider_released, LV_EVENT_RELEASED, r);
    return sl;
}

static void ring_create(ui_screen_t *s, lv_obj_t *root, const void *args)
{
    ring_t *r = ui_screen_state(s);
    r->screen = s;
    if (args) {
        r->ring = *(const clock_ring_t *)args;
    }
    const bool alarm = r->ring.kind == CLOCK_RING_ALARM;
    lv_obj_set_style_bg_color(root, ui_color(UI_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon = shell_label(root, alarm ? LV_SYMBOL_BELL : LV_SYMBOL_LOOP, UI_FONT_TITLE, CLOCK_ORANGE);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 44);
    const char *title = r->ring.label[0] ? r->ring.label : alarm ? "Alarm" : "Timer";
    lv_obj_t *t = shell_label(root, title, UI_FONT_TITLE, UI_COLOR_TEXT);
    lv_obj_set_width(t, 340);
    lv_obj_set_height(t, lv_font_get_line_height(UI_FONT_TITLE));
    s3w_label_fit(t); // one line, then "..."
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 92);

    char big[16];
    const char *caption;
    char ampm[4] = "";
    if (alarm) {
        const time_t at = r->ring.at > 0 ? (time_t)r->ring.at : ui_clock_now();
        struct tm lt;
        localtime_r(&at, &lt);
        if (ui_clock_is_24h()) {
            snprintf(big, sizeof big, "%02d:%02d", lt.tm_hour, lt.tm_min);
        } else {
            snprintf(big, sizeof big, "%d:%02d", lt.tm_hour % 12 ? lt.tm_hour % 12 : 12, lt.tm_min);
            snprintf(ampm, sizeof ampm, "%s", lt.tm_hour < 12 ? "AM" : "PM");
        }
        caption = ampm;
    } else {
        clock_fmt_duration(r->ring.duration_ms, big, sizeof big);
        caption = "Time's up";
    }
    lv_obj_t *time = shell_label(root, big, ui_font_display_tabular(), UI_COLOR_TEXT);
    lv_obj_align(time, LV_ALIGN_TOP_MID, 0, 140);
    if (caption[0]) {
        lv_obj_t *c = shell_label(root, caption, UI_FONT_BODY, UI_COLOR_TEXT_DIM);
        lv_obj_align_to(c, time, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
    }

    if (alarm) {
        char snooze[24];
        snprintf(snooze, sizeof snooze, "Snooze %u min", r->ring.snooze_min);
        lv_obj_t *b = s3w_button_create(root, S3W_BUTTON_SECONDARY, snooze);
        lv_obj_set_width(b, 300);
        lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -132);
        lv_obj_add_event_cb(b, snooze_clicked, LV_EVENT_CLICKED, r);
        lv_obj_t *sl = dismiss_slider(root, r);
        lv_obj_align(sl, LV_ALIGN_BOTTOM_MID, 0, -40);
    } else {
        lv_obj_t *stop = s3w_button_create(root, S3W_BUTTON_PRIMARY, "Stop");
        lv_obj_set_width(stop, 300);
        lv_obj_set_style_bg_color(stop, ui_color(CLOCK_ORANGE), 0);
        lv_obj_align(stop, LV_ALIGN_BOTTOM_MID, 0, -124);
        lv_obj_add_event_cb(stop, stop_clicked, LV_EVENT_CLICKED, r);
        lv_obj_t *again = s3w_button_create(root, S3W_BUTTON_SECONDARY, LV_SYMBOL_REFRESH "  Restart");
        lv_obj_set_width(again, 300);
        lv_obj_align(again, LV_ALIGN_BOTTOM_MID, 0, -44);
        lv_obj_add_event_cb(again, restart_clicked, LV_EVENT_CLICKED, r);
    }
}

static void ring_destroy(ui_screen_t *s)
{
    clock_ring_closed(s);
}

static bool ring_back(ui_screen_t *s)
{
    (void)s;
    return true; // BACK does not stop an alarm
}

const screen_def_t clock_ring_screen = {
    .id = "ring",
    .on_create = ring_create,
    .on_destroy = ring_destroy,
    .on_back = ring_back,
    .flags = UI_SCREEN_FULLSCREEN | UI_SCREEN_NO_SWIPE_BACK | UI_SCREEN_KEEP_ON,
    .state_size = sizeof(ring_t),
};
