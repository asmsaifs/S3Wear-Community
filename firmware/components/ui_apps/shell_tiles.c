// Tiles (docs/04 §3): swipe left on the face. Horizontal pages, each one compact
// widget drawn from the watch face data with the complication renderer
// (wf_comp_render), so tiles and complications always agree. Swipe left/right pages,
// a swipe right on the first tile closes, a tap opens the tile's app.
//
// Fixed order for now; user ordering (watch and phone) and mini app tiles come later
// (docs/05 §11). The Home tile (Home Assistant, P9-05) is drawn by ha_apps.c.
// The Community edition has only the Activity and Timer tiles (docs/10 §3).
#include <string.h>

#include "s3w_edition.h"
#include "shell_priv.h"
#if S3W_EDITION_PRO
#include "media_apps.h"
#include "weather_apps.h"
#endif
#include "ui_theme.h"
#include "ui_widgets.h"
#include "wf_engine.h"

#define SCREEN_W    410
#define TITLE_Y     60
#define BODY_CY     262
#define RING_D      220
#define RING_W      22
#define DETAIL_GAP  UI_SPACE_M
#define DEGREE      "\xC2\xB0"

typedef enum {
    TILE_COMP,  // a complication (comp)
    TILE_MEDIA, // the media session (media_apps_state)
    TILE_HOME,  // Home Assistant entities (ha_apps.c)
} tile_kind_t;

typedef struct {
    const char *title;
    const char *icon;
    tile_kind_t kind;
    wf_comp_t comp; // TILE_COMP
    const char *app;
} tile_def_t;

static const tile_def_t TILES[] = {
    {"Activity", LV_SYMBOL_REFRESH, TILE_COMP, WF_COMP_STEPS, "activity"},
#if S3W_EDITION_PRO
    {"Weather", "\xEF\x86\x85" /* sun */, TILE_COMP, WF_COMP_WEATHER, "weather"},
    {"Media", LV_SYMBOL_AUDIO, TILE_MEDIA, WF_COMP_NONE, "media"},
    {"Next event", LV_SYMBOL_LIST, TILE_COMP, WF_COMP_NEXT_EVENT, "calendar"},
#endif
    {"Timer", LV_SYMBOL_LOOP, TILE_COMP, WF_COMP_TIMER, "timer"},
#if S3W_EDITION_PRO
    {"Heart rate", LV_SYMBOL_PLUS, TILE_COMP, WF_COMP_HEART_RATE, "heart_rate"},
    {"Home", LV_SYMBOL_HOME, TILE_HOME, WF_COMP_NONE, "home"},
#endif
};
#define TILE_N (sizeof TILES / sizeof TILES[0])

typedef struct {
    lv_obj_t *icon;
    lv_obj_t *ring;   // NULL unless the complication has a gauge
    lv_obj_t *value;
    lv_obj_t *degree; // "°" next to the value (the display font has no °); Media: play / pause mark
    lv_obj_t *detail;
    lv_obj_t *text_img;       // Media: the phone-drawn title + artist in place of value / detail
    lv_image_dsc_t *text_dsc;
} tile_t;

typedef struct {
    lv_obj_t *strip; // TILE_N pages side by side, moved to show one
    lv_obj_t *dots;
    tile_t tile[TILE_N];
    uint8_t index;
} tiles_t;

/** True if the display font has every character of s (digits and ": . , - + %"). */
static bool display_font_has(const char *s)
{
    for (; *s; s++) {
        if (!strchr("0123456789:.,-+%", *s)) {
            return false;
        }
    }
    return true;
}

#if S3W_EDITION_PRO
// Media: the title and artist of what plays on the phone, or why there is nothing.
static void media_refresh(tile_t *t)
{
    bool connected;
    const media_state_t *st = media_apps_state(&connected);
    const bool active = connected && st->active;
    const char *value = !connected ? "Not connected" : !active ? "Not playing" : st->title[0] ? st->title : "Unknown";
    const char *detail = !connected ? "Connect your phone"
                         : !active  ? "Play music on your phone"
                         : st->artist[0] ? st->artist
                                         : st->app;
    s3w_label_set_fit_text(t->value, value);
    lv_obj_set_style_text_color(t->value, ui_color(active ? UI_COLOR_TEXT : UI_COLOR_TEXT_DIM), 0);
    s3w_label_set_fit_text(t->detail, detail);
    lv_label_set_text(t->degree, active ? (st->playing ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE) : "");
    if (t->text_img) {
        const bool drawn = media_apps_text_show(t->text_img, t->text_dsc);
        lv_obj_set_flag(t->value, LV_OBJ_FLAG_HIDDEN, drawn);
        lv_obj_set_flag(t->detail, LV_OBJ_FLAG_HIDDEN, drawn);
    }
}
#endif

static void tile_refresh(tile_t *t, const tile_def_t *def, const wf_ctx_t *ctx)
{
#if S3W_EDITION_PRO
    if (def->kind == TILE_MEDIA) {
        media_refresh(t);
        return;
    }
    if (def->kind == TILE_HOME) {
        return; // ha_apps.c keeps it up to date
    }
#endif
    wf_comp_view_t v;
    wf_comp_render(def->comp, ctx, &v);
    const uint32_t color = v.known ? v.color : UI_COLOR_TEXT_DIM;
    lv_obj_set_style_text_color(t->icon, ui_color(v.color), 0);
#if S3W_EDITION_PRO
    if (def->comp == WF_COMP_WEATHER) {
        // The condition's icon (sun, clouds, rain, ...) once the phone sent a forecast.
        const wf_data_t *d = ctx->data;
        lv_label_set_text(t->icon, v.known ? weather_apps_symbol(d->weather, !d->weather_night) : def->icon);
        if (v.known && !d->weather_stale) {
            lv_obj_set_style_text_color(t->icon, ui_color(weather_apps_color(d->weather, !d->weather_night)), 0);
        }
    }
#endif

    // Weather: "18°" -> "18" in the display font and a small "°".
    char value[sizeof v.value];
    strcpy(value, v.value);
    const size_t n = strlen(value);
    const bool degree = n > 2 && strcmp(value + n - 2, DEGREE) == 0;
    if (degree) {
        value[n - 2] = '\0';
    }
    const bool big = t->ring == NULL && display_font_has(value);
    lv_label_set_text(t->value, value);
    lv_obj_set_style_text_font(t->value, big ? UI_FONT_DISPLAY : UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(t->value, ui_color(t->ring ? UI_COLOR_TEXT : color), 0);
    s3w_label_set_fit_text(t->detail, v.detail);
    if (t->ring) {
        s3w_ring_set_value(t->ring, v.ratio < 0 ? 0 : v.ratio / 10);
        lv_obj_align(t->value, LV_ALIGN_TOP_MID, 0, BODY_CY - lv_font_get_line_height(UI_FONT_TITLE) / 2);
        lv_obj_align(t->detail, LV_ALIGN_TOP_MID, 0, BODY_CY + RING_D / 2 + DETAIL_GAP);
    } else {
        const lv_font_t *f = big ? UI_FONT_DISPLAY : UI_FONT_TITLE;
        lv_obj_align(t->value, LV_ALIGN_TOP_MID, 0, BODY_CY - lv_font_get_line_height(f) / 2 - UI_SPACE_L);
        lv_obj_update_layout(t->value);
        lv_obj_align_to(t->detail, t->value, LV_ALIGN_OUT_BOTTOM_MID, 0, DETAIL_GAP);
    }
    if (degree) {
        lv_obj_remove_flag(t->degree, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(t->degree, ui_color(color), 0);
        lv_obj_align_to(t->degree, t->value, LV_ALIGN_OUT_RIGHT_TOP, 0, UI_SPACE_M);
    } else {
        lv_obj_add_flag(t->degree, LV_OBJ_FLAG_HIDDEN);
    }
}

static void refresh(tiles_t *st)
{
    wf_ctx_t ctx;
    wf_ctx_init(&ctx, wf_data_get(), ui_clock_now(), ui_clock_is_24h(), ui_clock_is_valid());
    for (size_t i = 0; i < TILE_N; i++) {
        tile_refresh(&st->tile[i], &TILES[i], &ctx);
    }
}

static void clock_changed(void *ctx)
{
    refresh(ctx);
}

static void anim_x_cb(void *var, int32_t v)
{
    lv_obj_set_x(var, v);
}

static void show(tiles_t *st, uint8_t index, bool animate)
{
    st->index = index;
#if S3W_EDITION_PRO
    if (TILES[index].kind == TILE_HOME) {
        ha_apps_tile_shown();
    }
#endif
    s3w_page_dots_set_active(st->dots, index);
    const int32_t to = -(int32_t)index * SCREEN_W;
    lv_anim_delete(st->strip, anim_x_cb);
    if (!animate) {
        lv_obj_set_x(st->strip, to);
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, st->strip);
    lv_anim_set_exec_cb(&a, anim_x_cb);
    lv_anim_set_values(&a, lv_obj_get_x(st->strip), to);
    lv_anim_set_duration(&a, UI_MOTION_NORMAL);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void tiles_gesture(lv_event_t *e)
{
    tiles_t *st = lv_event_get_user_data(e);
    lv_indev_t *indev = lv_indev_active();
    if (indev == NULL) {
        return;
    }
    const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir != LV_DIR_LEFT && dir != LV_DIR_RIGHT) {
        return;
    }
    // A swipe never ends in a tap on the tile.
    lv_indev_wait_release(indev);
    if (dir == LV_DIR_LEFT && st->index + 1u < TILE_N) {
        show(st, st->index + 1, true);
    } else if (dir == LV_DIR_RIGHT && st->index > 0) {
        show(st, st->index - 1, true);
    } else if (dir == LV_DIR_RIGHT) {
        ui_nav_back(); // right of the first tile is the face
    }
}

static void tile_clicked(lv_event_t *e)
{
    const tile_def_t *def = lv_event_get_user_data(e);
    shell_app_open(shell_app_find(def->app));
}

static lv_obj_t *page_create(lv_obj_t *strip, size_t i, tile_t *t)
{
    const tile_def_t *def = &TILES[i];
    lv_obj_t *p = lv_obj_create(strip);
    lv_obj_remove_style_all(p);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(p, SCREEN_W, LV_PCT(100));
    lv_obj_set_pos(p, (int32_t)i * SCREEN_W, 0);
    lv_obj_add_event_cb(p, tile_clicked, LV_EVENT_CLICKED, (void *)def);

    lv_obj_t *head = lv_obj_create(p);
    lv_obj_remove_style_all(head);
    lv_obj_remove_flag(head, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(head, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(head, UI_SPACE_S, 0);
    lv_obj_align(head, LV_ALIGN_TOP_MID, 0, TITLE_Y);
    t->icon = shell_label(head, def->icon, UI_FONT_TITLE, UI_COLOR_TEXT_DIM);
    shell_label(head, def->title, UI_FONT_TITLE, UI_COLOR_TEXT);

#if S3W_EDITION_PRO
    if (def->kind == TILE_HOME) {
        lv_obj_set_style_text_color(t->icon, ui_color(0xFF9F0A), 0);
        ha_apps_tile_create(p, BODY_CY);
        return p;
    }
    if (def->kind == TILE_MEDIA) {
        // Media: title (one line), artist, and a play / pause mark above them.
        lv_obj_set_style_text_color(t->icon, ui_color(0xBF5AF2), 0);
        t->degree = shell_label(p, "", UI_FONT_TITLE, 0xBF5AF2); // the play / pause mark
        lv_obj_align(t->degree, LV_ALIGN_TOP_MID, 0, BODY_CY - lv_font_get_line_height(UI_FONT_TITLE) * 2);
        t->value = shell_label(p, "", UI_FONT_TITLE, UI_COLOR_TEXT);
        t->detail = shell_label(p, "", UI_FONT_CAPTION, UI_COLOR_TEXT_DIM);
        lv_obj_t *labels[] = {t->value, t->detail};
        for (size_t k = 0; k < 2; k++) {
            lv_obj_set_size(labels[k], SCREEN_W - 2 * (UI_SAFE_INSET + UI_SPACE_M),
                            lv_font_get_line_height(lv_obj_get_style_text_font(labels[k], 0)));
            s3w_label_fit(labels[k]); // one line, then "..."
            lv_obj_set_style_text_align(labels[k], LV_TEXT_ALIGN_CENTER, 0);
        }
        lv_obj_align(t->value, LV_ALIGN_TOP_MID, 0, BODY_CY - lv_font_get_line_height(UI_FONT_TITLE));
        lv_obj_align(t->detail, LV_ALIGN_TOP_MID, 0, BODY_CY + UI_SPACE_M);
        t->text_img = media_apps_text_create(p, &t->text_dsc);
        lv_obj_align(t->text_img, LV_ALIGN_TOP_MID, 0, BODY_CY - lv_font_get_line_height(UI_FONT_TITLE));
        return p;
    }
#endif
    wf_comp_view_t probe;
    wf_ctx_t ctx;
    wf_ctx_init(&ctx, wf_data_get(), ui_clock_now(), ui_clock_is_24h(), ui_clock_is_valid());
    wf_comp_render(def->comp, &ctx, &probe);
    if (def->comp == WF_COMP_STEPS) {
        // The gauge ring is the tile; it stays (empty) while steps are unknown.
        t->ring = s3w_ring_create(p, RING_D, RING_W, probe.color);
        lv_obj_align(t->ring, LV_ALIGN_TOP_MID, 0, BODY_CY - RING_D / 2);
    }
    t->value = shell_label(p, "", UI_FONT_TITLE, UI_COLOR_TEXT);
    t->degree = shell_label(p, DEGREE, UI_FONT_TITLE, UI_COLOR_TEXT);
    t->detail = shell_label(p, "", UI_FONT_BODY, UI_COLOR_TEXT_DIM);
    lv_obj_set_style_max_width(t->detail, SCREEN_W - 2 * (UI_SAFE_INSET + UI_SPACE_M), 0);
    lv_obj_set_height(t->detail, lv_font_get_line_height(UI_FONT_BODY));
    s3w_label_fit(t->detail);
    return p;
}

static void tiles_create(ui_screen_t *s, lv_obj_t *root, const void *args)
{
    (void)args;
    tiles_t *st = ui_screen_state(s);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(root, tiles_gesture, LV_EVENT_GESTURE, st);

    st->strip = lv_obj_create(root);
    lv_obj_remove_style_all(st->strip);
    lv_obj_remove_flag(st->strip, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(st->strip, (int32_t)TILE_N * SCREEN_W, LV_PCT(100));
    for (size_t i = 0; i < TILE_N; i++) {
        page_create(st->strip, i, &st->tile[i]);
    }

    lv_obj_t *clock = shell_label(root, "", UI_FONT_CAPTION, UI_COLOR_TEXT);
    ui_clock_bind_label(clock);
    lv_obj_align(clock, LV_ALIGN_TOP_MID, 0, UI_SAFE_INSET);
    st->dots = s3w_page_dots_create(root, TILE_N);
    lv_obj_align(st->dots, LV_ALIGN_BOTTOM_MID, 0, -UI_SAFE_INSET - UI_SPACE_S);
    show(st, 0, false);
    refresh(st);
}

static void tiles_resume(ui_screen_t *s)
{
    tiles_t *st = ui_screen_state(s);
    refresh(st);
#if S3W_EDITION_PRO
    if (TILES[st->index].kind == TILE_HOME) {
        ha_apps_tile_shown(); // back from the Home app or the face
    }
#endif
    // Minute updates while visible (timer, next event); data from services is
    // picked up on the next minute or when the tiles are shown again.
    ui_clock_add_listener(clock_changed, st);
}

static void tiles_pause(ui_screen_t *s)
{
    ui_clock_remove_listener(clock_changed, ui_screen_state(s));
}

const screen_def_t shell_tiles_screen = {
    .id = "tiles",
    .on_create = tiles_create,
    .on_resume = tiles_resume,
    .on_pause = tiles_pause,
    .flags = UI_SCREEN_NO_SWIPE_BACK, // horizontal swipes page
    .state_size = sizeof(tiles_t),
};
