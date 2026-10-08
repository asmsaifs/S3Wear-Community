// World clock (docs/03 F6): up to WORLD_CLOCK_MAX cities with their time, day and
// offset from home; tap a city to remove it; "world_clock.add" picks from the built-in
// list (world_clock.h). Refreshed on the minute while visible.
#include <stdio.h>

#include "clock_priv.h"
#include "tz_posix.h"
#include "ui_overlay.h"
#include "ui_theme.h"
#include "ui_widgets.h"

static void city_time(const world_city_t *c, time_t now, int32_t *off, char *buf, size_t len)
{
    *off = world_city_offset(c, now);
    struct tm tm;
    tz_posix_gmtime((int64_t)now + *off, &tm);
    clock_fmt_hm(tm.tm_hour, tm.tm_min, buf, len);
}

// Trailing time in the body font and full white: it is the point of the row.
static lv_obj_t *city_row(lv_obj_t *list, const world_city_t *c, const char *sub, const char *time)
{
    lv_obj_t *row = s3w_list_add_row(list, NULL, c->name, sub, time);
    lv_obj_t *t = lv_obj_get_child(row, -1);
    lv_obj_set_style_text_font(t, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(t, ui_color(UI_COLOR_TEXT), 0);
    return row;
}

// --- List -----------------------------------------------------------------------------------------

typedef struct {
    ui_screen_t *screen;
    lv_obj_t *list;
    const world_city_t *remove; // city in the open remove dialog
} world_t;

static void build(world_t *st);

static void remove_confirmed(bool ok, void *ctx)
{
    world_t *st = ctx;
    if (!ok || st->remove == NULL) {
        return;
    }
    const world_city_t *cities[WORLD_CLOCK_MAX];
    const world_city_t *keep[WORLD_CLOCK_MAX];
    const size_t n = clock_world_cities(cities);
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        if (cities[i] != st->remove) {
            keep[k++] = cities[i];
        }
    }
    st->remove = NULL;
    clock_world_save(keep, k);
    build(st);
}

static void row_clicked(lv_event_t *e)
{
    world_t *st = lv_event_get_user_data(e);
    st->remove = lv_obj_get_user_data(lv_event_get_current_target_obj(e));
    char title[40];
    snprintf(title, sizeof title, "Remove %s?", st->remove->name);
    s3w_dialog_show(ui_screen_root(st->screen), title, NULL, "Remove", true, remove_confirmed, st);
}

static void add_clicked(lv_event_t *e)
{
    (void)e;
    ui_nav_push(&clock_world_add_screen, NULL);
}

static void build(world_t *st)
{
    lv_obj_t *list = st->list;
    lv_obj_clean(list);
    s3w_header_create(list, "World clock");
    const world_city_t *cities[WORLD_CLOCK_MAX];
    const size_t n = clock_world_cities(cities);
    if (n == 0) {
        s3w_empty_state_create(list, LV_SYMBOL_GPS, "No cities", "Add up to 6 cities.");
    }
    const time_t now = ui_clock_now();
    const int32_t home = clock_home_offset(now);
    for (size_t i = 0; i < n; i++) {
        char time[16];
        char rel[40];
        int32_t off = 0;
        city_time(cities[i], now, &off, time, sizeof time);
        world_clock_relative(off, home, now, rel, sizeof rel);
        lv_obj_t *row = city_row(list, cities[i], rel, ui_clock_is_valid() ? time : "--:--");
        lv_obj_set_user_data(row, (void *)cities[i]);
        lv_obj_add_event_cb(row, row_clicked, LV_EVENT_CLICKED, st);
    }
    if (n < WORLD_CLOCK_MAX) {
        lv_obj_t *add = s3w_button_create(list, S3W_BUTTON_PRIMARY, LV_SYMBOL_PLUS "  Add city");
        lv_obj_set_width(add, LV_PCT(80));
        lv_obj_set_style_margin_top(add, UI_SPACE_M, 0);
        lv_obj_add_event_cb(add, add_clicked, LV_EVENT_CLICKED, NULL);
    }
    lv_obj_t *pad = clock_plain(list);
    lv_obj_set_height(pad, UI_SPACE_XL);
}

static void on_minute(void *ctx)
{
    build(ctx);
}

static void world_create(ui_screen_t *s, lv_obj_t *root, const void *args)
{
    (void)args;
    world_t *st = ui_screen_state(s);
    st->screen = s;
    st->list = s3w_list_create(root); // filled on resume
}

static void world_resume(ui_screen_t *s)
{
    world_t *st = ui_screen_state(s);
    build(st);
    ui_clock_add_listener(on_minute, st);
}

static void world_pause(ui_screen_t *s)
{
    ui_clock_remove_listener(on_minute, ui_screen_state(s));
}

const screen_def_t clock_world_screen = {
    .id = "world_clock",
    .on_create = world_create,
    .on_resume = world_resume,
    .on_pause = world_pause,
    .state_size = sizeof(world_t),
};

// --- Add a city --------------------------------------------------------------------------------

static void city_clicked(lv_event_t *e)
{
    const world_city_t *c = lv_event_get_user_data(e);
    const world_city_t *cities[WORLD_CLOCK_MAX + 1];
    size_t n = clock_world_cities(cities);
    if (n < WORLD_CLOCK_MAX) {
        cities[n++] = c;
        clock_world_save(cities, n);
    }
    ui_nav_back();
}

static void add_create(ui_screen_t *s, lv_obj_t *root, const void *args)
{
    (void)s;
    (void)args;
    lv_obj_t *list = s3w_list_create(root);
    s3w_header_create(list, "Add city");
    const world_city_t *chosen[WORLD_CLOCK_MAX];
    const size_t n = clock_world_cities(chosen);
    const time_t now = ui_clock_now();
    for (size_t i = 0; i < world_city_count(); i++) {
        const world_city_t *c = world_city_at(i);
        bool have = false;
        for (size_t k = 0; k < n; k++) {
            have |= chosen[k] == c;
        }
        if (have) {
            continue;
        }
        char time[16];
        int32_t off = 0;
        city_time(c, now, &off, time, sizeof time);
        lv_obj_t *row = city_row(list, c, NULL, ui_clock_is_valid() ? time : "--:--");
        lv_obj_add_event_cb(row, city_clicked, LV_EVENT_CLICKED, (void *)c);
    }
    lv_obj_t *pad = clock_plain(list);
    lv_obj_set_height(pad, UI_SPACE_XL);
}

const screen_def_t clock_world_add_screen = {
    .id = "world_clock.add",
    .on_create = add_create,
};
