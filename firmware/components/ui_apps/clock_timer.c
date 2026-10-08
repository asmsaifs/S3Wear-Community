// Timer (docs/03 F6): running, paused and finished countdowns with pause/resume,
// restart and remove; presets (1, 3, 5, 10, 15, 30 min) and a custom duration
// ("timer.custom"). Up to TIMER_MAX at once; they ring through the backend.
#include <stdio.h>
#include <string.h>

#include "clock_priv.h"
#include "ui_overlay.h"
#include "ui_theme.h"
#include "ui_widgets.h"

// Countdown labels only while the screen is visible; 4 Hz keeps the shown second
// within 250 ms of the real one.
#define TICK_MS 250

static const uint8_t PRESETS_MIN[] = {1, 3, 5, 10, 15, 30};

typedef struct {
    uint8_t id;
    lv_obj_t *time;
} timer_row_t;

typedef struct {
    lv_obj_t *list;
    timer_row_t rows[TIMER_MAX];
    uint8_t row_count;
} timers_t;

static timer_set_t s_timers; // UI task scratch copy

static void start(uint32_t ms)
{
    const clock_backend_t *b = clock_backend();
    const esp_err_t err = b->timer_start(ms, b->ctx);
    if (err == ESP_ERR_NO_MEM) {
        ui_toast_show("Up to 6 timers", 0);
    } else if (err != ESP_OK) {
        ui_toast_show("Could not start the timer", 0);
    }
}

static void preset_clicked(lv_event_t *e)
{
    start((uint32_t)(uintptr_t)lv_event_get_user_data(e) * 60000u);
}

static void custom_clicked(lv_event_t *e)
{
    (void)e;
    ui_nav_push(&clock_timer_custom_screen, NULL);
}

static void action_clicked(lv_event_t *e)
{
    const uintptr_t v = (uintptr_t)lv_event_get_user_data(e);
    const clock_backend_t *b = clock_backend();
    b->timer_action((uint8_t)(v & 0xFF), (clock_timer_action_t)(v >> 8), b->ctx);
}

static lv_obj_t *small_button(lv_obj_t *row, const char *symbol, uint32_t color, uint8_t id,
                              clock_timer_action_t action)
{
    lv_obj_t *b = clock_round_button(row, 56, color, symbol, UI_FONT_BODY);
    lv_obj_set_ext_click_area(b, 4);
    lv_obj_add_event_cb(b, action_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)(id | (unsigned)action << 8));
    return b;
}

static void update_times(timers_t *st)
{
    uint32_t now = 0;
    const clock_backend_t *b = clock_backend();
    b->timers(&s_timers, &now, b->ctx);
    for (int i = 0; i < st->row_count; i++) {
        const countdown_t *c = timer_set_find(&s_timers, st->rows[i].id);
        if (c) {
            char buf[16];
            clock_fmt_duration(timer_left_ms(c, now), buf, sizeof buf);
            lv_label_set_text(st->rows[i].time, buf);
        }
    }
}

static void tick(lv_timer_t *t)
{
    update_times(lv_timer_get_user_data(t));
}

static void build(timers_t *st)
{
    lv_obj_t *list = st->list;
    lv_obj_clean(list);
    st->row_count = 0;
    s3w_header_create(list, "Timer");
    uint32_t now = 0;
    const clock_backend_t *b = clock_backend();
    b->timers(&s_timers, &now, b->ctx);

    for (int i = 0; i < s_timers.count; i++) {
        const countdown_t *c = &s_timers.items[i];
        char left[16];
        char total[24];
        char dur[16];
        clock_fmt_duration(timer_left_ms(c, now), left, sizeof left);
        clock_fmt_duration(c->duration_ms, dur, sizeof dur);
        snprintf(total, sizeof total, "%s %s", c->state == TIMER_PAUSED ? "Paused," : c->state == TIMER_DONE ? "Done," : "Of",
                 dur);
        lv_obj_t *row = s3w_list_add_row(list, NULL, left, total, NULL);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_pad_column(row, UI_SPACE_S, 0);
        lv_obj_t *time = lv_obj_get_child(lv_obj_get_child(row, 0), 0);
        lv_obj_set_style_text_font(time, UI_FONT_TITLE, 0);
        lv_obj_set_height(time, LV_SIZE_CONTENT);
        st->rows[st->row_count++] = (timer_row_t){.id = c->id, .time = time};
        if (c->state == TIMER_RUNNING) {
            small_button(row, LV_SYMBOL_PAUSE, CLOCK_ORANGE, c->id, CLOCK_TIMER_PAUSE);
        } else if (c->state == TIMER_PAUSED) {
            small_button(row, LV_SYMBOL_PLAY, CLOCK_ORANGE, c->id, CLOCK_TIMER_RESUME);
        } else {
            small_button(row, LV_SYMBOL_REFRESH, CLOCK_ORANGE, c->id, CLOCK_TIMER_RESTART);
        }
        small_button(row, LV_SYMBOL_CLOSE, UI_COLOR_SURFACE_HI, c->id, CLOCK_TIMER_REMOVE);
    }

    if (s_timers.count) {
        s3w_list_add_section(list, "New timer");
    }
    lv_obj_t *grid = clock_plain(list);
    lv_obj_set_width(grid, 340);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(grid, UI_SPACE_M, 0);
    lv_obj_set_style_pad_row(grid, UI_SPACE_M, 0);
    lv_obj_set_style_pad_ver(grid, UI_SPACE_S, 0);
    for (size_t i = 0; i < sizeof PRESETS_MIN; i++) {
        char text[12];
        snprintf(text, sizeof text, "%u\nmin", PRESETS_MIN[i]);
        lv_obj_t *p = clock_round_button(grid, 96, UI_COLOR_SURFACE_HI, text, UI_FONT_BODY);
        lv_obj_add_event_cb(p, preset_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)PRESETS_MIN[i]);
    }
    lv_obj_t *custom = s3w_button_create(list, S3W_BUTTON_SECONDARY, "Custom");
    lv_obj_set_width(custom, LV_PCT(80));
    lv_obj_set_style_margin_top(custom, UI_SPACE_S, 0);
    lv_obj_set_style_margin_bottom(custom, UI_SPACE_XL, 0);
    lv_obj_add_event_cb(custom, custom_clicked, LV_EVENT_CLICKED, NULL);
}

static void timers_create(ui_screen_t *s, lv_obj_t *root, const void *args)
{
    (void)args;
    timers_t *st = ui_screen_state(s);
    st->list = s3w_list_create(root); // filled on resume
    ui_screen_timer_create(s, tick, TICK_MS, st);
}

static void timers_resume(ui_screen_t *s)
{
    build(ui_screen_state(s));
}

void clock_timer_refresh(ui_screen_t *s)
{
    build(ui_screen_state(s));
}

const screen_def_t clock_timer_screen = {
    .id = "timer",
    .on_create = timers_create,
    .on_resume = timers_resume,
    .state_size = sizeof(timers_t),
};

// --- Custom duration ------------------------------------------------------------------------

typedef struct {
    lv_obj_t *h;
    lv_obj_t *m;
    lv_obj_t *s;
} custom_t;

static void custom_start(lv_event_t *e)
{
    custom_t *c = lv_event_get_user_data(e);
    const uint32_t sec = (uint32_t)(s3w_picker_get_value(c->h) * 3600 + s3w_picker_get_value(c->m) * 60 +
                                    s3w_picker_get_value(c->s));
    if (sec == 0) {
        ui_toast_show("Set a time first", 0);
        return;
    }
    start(sec * 1000u);
    ui_nav_back();
}

static lv_obj_t *picker(lv_obj_t *parent, int32_t max, int32_t value, const char *unit)
{
    lv_obj_t *col = clock_plain(parent);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    shell_label(col, unit, UI_FONT_CAPTION, UI_COLOR_TEXT_DIM);
    lv_obj_t *p = s3w_picker_create(col, 0, max, 1, value, true);
    lv_obj_set_width(p, 104);
    return p;
}

static void custom_create(ui_screen_t *s, lv_obj_t *root, const void *args)
{
    (void)args;
    custom_t *c = ui_screen_state(s);
    lv_obj_t *list = s3w_list_create(root);
    s3w_header_create(list, "Custom timer");
    lv_obj_t *row = clock_plain(list);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, UI_SPACE_S, 0);
    c->h = picker(row, 23, 0, "h");
    c->m = picker(row, 59, 10, "min");
    c->s = picker(row, 59, 0, "s");
    lv_obj_t *go = s3w_button_create(list, S3W_BUTTON_PRIMARY, LV_SYMBOL_PLAY "  Start");
    lv_obj_set_width(go, LV_PCT(80));
    lv_obj_set_style_bg_color(go, ui_color(CLOCK_ORANGE), 0);
    lv_obj_set_style_margin_top(go, UI_SPACE_M, 0);
    lv_obj_set_style_margin_bottom(go, UI_SPACE_XL, 0);
    lv_obj_add_event_cb(go, custom_start, LV_EVENT_CLICKED, c);
}

const screen_def_t clock_timer_custom_screen = {
    .id = "timer.custom",
    .on_create = custom_create,
    .state_size = sizeof(custom_t),
};
