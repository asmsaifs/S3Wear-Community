// Alarms (docs/03 F6): the list ("alarms": time, repeat days, on/off switch, next
// alarm) and the editor ("alarms.edit": time pickers, repeat days, snooze length,
// save, delete). Labels come from the phone or the console (no keyboard yet).
#include <stdio.h>
#include <string.h>

#include "clock_priv.h"
#include "ui_overlay.h"
#include "ui_theme.h"
#include "ui_widgets.h"

static alarm_set_t s_set; // UI task scratch copy

// --- List -------------------------------------------------------------------------------------

typedef struct {
    lv_obj_t *list;
} alarms_t;

static void open_editor(const alarm_t *a)
{
    ui_nav_push(&clock_alarm_edit_screen, a);
}

static void row_clicked(lv_event_t *e)
{
    const uint8_t id = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    clock_backend()->alarms(&s_set, clock_backend()->ctx);
    const alarm_t *a = alarm_set_find(&s_set, id);
    if (a) {
        open_editor(a);
    }
}

static void switch_changed(lv_event_t *e)
{
    const uint8_t id = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    const bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    const clock_backend_t *b = clock_backend();
    b->alarms(&s_set, b->ctx);
    alarm_t *a = alarm_set_find(&s_set, id);
    if (a && a->enabled != on) {
        a->enabled = on;
        b->alarm_put(a, b->ctx); // the list is rebuilt by clock_apps_changed()
    }
}

static void add_clicked(lv_event_t *e)
{
    (void)e;
    clock_backend()->alarms(&s_set, clock_backend()->ctx);
    if (s_set.count >= ALARM_MAX) {
        ui_toast_show("Up to 16 alarms", 0);
        return;
    }
    // Start the pickers at the next full hour.
    const time_t now = ui_clock_now();
    struct tm lt;
    localtime_r(&now, &lt);
    alarm_t a;
    alarm_default(&a, (uint8_t)((lt.tm_hour + 1) % 24), 0);
    open_editor(&a);
}

static void build(alarms_t *st)
{
    lv_obj_t *list = st->list;
    lv_obj_clean(list);
    s3w_header_create(list, "Alarms");
    const clock_backend_t *b = clock_backend();
    b->alarms(&s_set, b->ctx);

    if (s_set.count == 0) {
        s3w_empty_state_create(list, LV_SYMBOL_BELL, "No alarms", "Tap Add to set one.");
    } else {
        char next[48] = "No alarm on";
        if (!ui_clock_is_valid()) {
            snprintf(next, sizeof next, "Time not set");
        } else {
            const int64_t at = b->alarm_next(b->ctx);
            if (at > 0) {
                char in[24];
                clock_fmt_until(at - ui_clock_now(), in, sizeof in);
                snprintf(next, sizeof next, "Next %s", in);
            }
        }
        lv_obj_t *n = shell_label(list, next, UI_FONT_CAPTION, UI_COLOR_TEXT_DIM);
        lv_obj_set_style_pad_bottom(n, UI_SPACE_S, 0);
    }
    for (int i = 0; i < s_set.count; i++) {
        const alarm_t *a = &s_set.items[i];
        char hm[16];
        char sub[64];
        char days[32];
        clock_fmt_hm(a->hour, a->minute, hm, sizeof hm);
        alarm_days_text(a->days, days, sizeof days);
        if (s_set.snooze_id == a->id) {
            snprintf(sub, sizeof sub, "Snoozed, %s", days);
        } else if (a->label[0]) {
            snprintf(sub, sizeof sub, "%s, %s", a->label, days);
        } else {
            snprintf(sub, sizeof sub, "%s", days);
        }
        lv_obj_t *row = s3w_list_add_row(list, NULL, hm, sub, NULL);
        lv_obj_add_event_cb(row, row_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)a->id);
        lv_obj_t *sw = lv_switch_create(row);
        lv_obj_set_size(sw, 64, 36);
        lv_obj_set_ext_click_area(sw, UI_SPACE_M);
        lv_obj_set_style_bg_color(sw, ui_color(UI_COLOR_SURFACE_HI), LV_PART_MAIN);
        lv_obj_set_style_bg_color(sw, ui_color(CLOCK_ORANGE), LV_PART_INDICATOR | LV_STATE_CHECKED);
        if (a->enabled) {
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        }
        lv_obj_add_event_cb(sw, switch_changed, LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)a->id);
    }
    lv_obj_t *add = s3w_button_create(list, S3W_BUTTON_PRIMARY, LV_SYMBOL_PLUS "  Add alarm");
    lv_obj_set_width(add, LV_PCT(80));
    lv_obj_set_style_margin_top(add, UI_SPACE_M, 0);
    lv_obj_set_style_margin_bottom(add, UI_SPACE_XL, 0);
    lv_obj_add_event_cb(add, add_clicked, LV_EVENT_CLICKED, NULL);
}

static void alarms_create(ui_screen_t *s, lv_obj_t *root, const void *args)
{
    (void)args;
    alarms_t *st = ui_screen_state(s);
    st->list = s3w_list_create(root); // filled on resume
}

// Shown, back from the editor or the display on again: fresh data and "Next in ...".
static void alarms_resume(ui_screen_t *s)
{
    build(ui_screen_state(s));
}

void clock_alarms_refresh(ui_screen_t *s)
{
    build(ui_screen_state(s));
}

const screen_def_t clock_alarms_screen = {
    .id = "alarms",
    .on_create = alarms_create,
    .on_resume = alarms_resume,
    .state_size = sizeof(alarms_t),
};

// --- Editor -----------------------------------------------------------------------------------

typedef struct {
    ui_screen_t *screen;
    alarm_t a;
    lv_obj_t *hour;
    lv_obj_t *minute;
    lv_obj_t *days_text;
    lv_obj_t *snooze_value;
} edit_t;

static void days_update(edit_t *ed)
{
    char buf[32];
    alarm_days_text(ed->a.days, buf, sizeof buf);
    lv_label_set_text(ed->days_text, buf);
}

static void day_toggled(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target_obj(e);
    edit_t *ed = lv_event_get_user_data(e);
    const unsigned d = (unsigned)(uintptr_t)lv_obj_get_user_data(btn);
    if (lv_obj_has_state(btn, LV_STATE_CHECKED)) {
        ed->a.days |= (uint8_t)(1u << d);
    } else {
        ed->a.days &= (uint8_t)~(1u << d);
    }
    days_update(ed);
}

static void snooze_update(edit_t *ed)
{
    lv_label_set_text_fmt(ed->snooze_value, "%u min", ed->a.snooze_min);
}

static void snooze_clicked(lv_event_t *e)
{
    edit_t *ed = lv_event_get_user_data(e);
    ed->a.snooze_min = ed->a.snooze_min >= ALARM_SNOOZE_MAX ? ALARM_SNOOZE_MIN : ed->a.snooze_min + 5;
    snooze_update(ed);
}

static void save_clicked(lv_event_t *e)
{
    edit_t *ed = lv_event_get_user_data(e);
    ed->a.hour = (uint8_t)s3w_picker_get_value(ed->hour);
    ed->a.minute = (uint8_t)s3w_picker_get_value(ed->minute);
    ed->a.enabled = true; // saving turns it on
    const clock_backend_t *b = clock_backend();
    const esp_err_t err = b->alarm_put(&ed->a, b->ctx);
    if (err == ESP_ERR_NO_MEM) {
        ui_toast_show("Up to 16 alarms", 0);
        return;
    }
    if (err != ESP_OK) {
        ui_toast_show("Could not save the alarm", 0);
        return;
    }
    ui_nav_back();
}

static void delete_confirmed(bool ok, void *ctx)
{
    edit_t *ed = ctx;
    if (ok) {
        clock_backend()->alarm_delete(ed->a.id, clock_backend()->ctx);
        ui_nav_back();
    }
}

static void delete_clicked(lv_event_t *e)
{
    edit_t *ed = lv_event_get_user_data(e);
    s3w_dialog_show(ui_screen_root(ed->screen), "Delete alarm?", NULL, "Delete", true, delete_confirmed, ed);
}

static lv_obj_t *day_button(lv_obj_t *parent, edit_t *ed, unsigned d)
{
    static const char *const k_letters[7] = {"S", "M", "T", "W", "T", "F", "S"};
    lv_obj_t *b = clock_round_button(parent, 44, UI_COLOR_SURFACE_HI, k_letters[d], UI_FONT_CAPTION);
    lv_obj_set_style_bg_color(b, ui_color(CLOCK_ORANGE), LV_STATE_CHECKED);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_ext_click_area(b, 4); // 44 + 2 x 4 px per side and the gap: ~52 px targets
    lv_obj_set_user_data(b, (void *)(uintptr_t)d);
    if (ed->a.days & (1u << d)) {
        lv_obj_add_state(b, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(b, day_toggled, LV_EVENT_VALUE_CHANGED, ed);
    return b;
}

static void edit_create(ui_screen_t *s, lv_obj_t *root, const void *args)
{
    edit_t *ed = ui_screen_state(s);
    ed->screen = s;
    if (args) {
        ed->a = *(const alarm_t *)args;
    } else {
        alarm_default(&ed->a, 7, 0);
    }

    lv_obj_t *list = s3w_list_create(root);
    s3w_header_create(list, ed->a.id ? "Edit alarm" : "New alarm");

    // 24 h pickers in either clock format: unambiguous without an AM/PM wheel.
    lv_obj_t *time = clock_plain(list);
    lv_obj_set_flex_flow(time, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(time, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(time, UI_SPACE_S, 0);
    ed->hour = s3w_picker_create(time, 0, 23, 1, ed->a.hour, true);
    shell_label(time, ":", UI_FONT_TITLE, UI_COLOR_TEXT);
    ed->minute = s3w_picker_create(time, 0, 59, 1, ed->a.minute, true);

    // Repeat days, Monday first.
    lv_obj_t *days = clock_plain(list);
    lv_obj_set_flex_flow(days, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(days, 6, 0);
    lv_obj_set_style_pad_top(days, UI_SPACE_M, 0);
    for (unsigned k = 1; k <= 7; k++) {
        day_button(days, ed, k % 7);
    }
    ed->days_text = shell_label(list, "", UI_FONT_CAPTION, UI_COLOR_TEXT_DIM);
    lv_obj_set_style_pad_bottom(ed->days_text, UI_SPACE_S, 0);
    days_update(ed);

    lv_obj_t *snooze = s3w_list_add_row(list, LV_SYMBOL_REFRESH, "Snooze", NULL, "");
    ed->snooze_value = lv_obj_get_child(snooze, -1);
    lv_obj_add_event_cb(snooze, snooze_clicked, LV_EVENT_CLICKED, ed);
    snooze_update(ed);

    lv_obj_t *save = s3w_button_create(list, S3W_BUTTON_PRIMARY, "Save");
    lv_obj_set_width(save, LV_PCT(80));
    lv_obj_set_style_margin_top(save, UI_SPACE_M, 0);
    lv_obj_add_event_cb(save, save_clicked, LV_EVENT_CLICKED, ed);
    if (ed->a.id) {
        lv_obj_t *del = s3w_button_create(list, S3W_BUTTON_DANGER, "Delete");
        lv_obj_set_width(del, LV_PCT(80));
        lv_obj_set_style_margin_top(del, UI_SPACE_M, 0);
        lv_obj_add_event_cb(del, delete_clicked, LV_EVENT_CLICKED, ed);
    }
    lv_obj_t *pad = clock_plain(list);
    lv_obj_set_height(pad, UI_SPACE_XL);
}

const screen_def_t clock_alarm_edit_screen = {
    .id = "alarms.edit",
    .on_create = edit_create,
    .state_size = sizeof(edit_t),
};
