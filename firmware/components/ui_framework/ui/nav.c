// Navigation stack, screen lifecycle, left-edge swipe back and the minute clock.
#include "ui_nav.h"

#include <string.h>

#include "ui_internal.h"
#include "ui_theme.h"

#define SCREEN_TIMERS     4
#define REGISTRY_MAX      32
#define CLOCK_LABELS_MAX  16
#define CLOCK_LISTENERS   4
#define EDGE_W            UI_SAFE_INSET // strip along the left edge that starts a back swipe
#define EDGE_TRIGGER_DX   80            // px of travel that counts as "back"
#define EDGE_KNOB         56

struct ui_screen {
    const screen_def_t *def;
    lv_obj_t *root;
    void *state;
    bool visible;
    ui_slide_t slide; // side it slid in from; it leaves the same way
    lv_timer_t *timers[SCREEN_TIMERS];
};

static ui_screen_t s_stack[UI_NAV_MAX_DEPTH];
static size_t s_depth;
static bool s_active = true;
static bool s_covered;
static const screen_def_t *s_registry[REGISTRY_MAX];
static size_t s_registry_n;

static const screen_def_t *s_home_swipe[4]; // by finger direction: left, right, up, down

static lv_obj_t *s_edge;      // touch strip on the top layer
static lv_obj_t *s_edge_knob; // feedback circle that follows the finger
static lv_point_t s_edge_start;

// --- Visibility ----------------------------------------------------------------------

static void set_visible(ui_screen_t *s, bool visible)
{
    if (s->visible == visible) {
        return;
    }
    s->visible = visible;
    for (int i = 0; i < SCREEN_TIMERS; i++) {
        if (s->timers[i] == NULL) {
            continue;
        }
        if (visible) {
            lv_timer_resume(s->timers[i]);
        } else {
            lv_timer_pause(s->timers[i]);
        }
    }
    if (visible && s->def->on_resume) {
        s->def->on_resume(s);
    } else if (!visible && s->def->on_pause) {
        s->def->on_pause(s);
    }
}

static void edge_update(void);

static void (*s_listener)(void *ctx);
static void *s_listener_ctx;

static void notify_listener(void)
{
    if (s_listener) {
        s_listener(s_listener_ctx);
    }
}

/** Only the top screen is visible, and only while the display is on and no alert covers it. */
static void update_visibility(void)
{
    for (size_t i = 0; i < s_depth; i++) {
        const bool top = i + 1 == s_depth;
        // Pause before resume so two screens are never "visible" at once.
        if (!top) {
            set_visible(&s_stack[i], false);
        }
    }
    if (s_depth > 0) {
        set_visible(&s_stack[s_depth - 1], s_active && !s_covered);
    }
    edge_update();
    notify_listener();
}

static void destroy(ui_screen_t *s, bool delete_root)
{
    set_visible(s, false);
    if (s->def->on_destroy) {
        s->def->on_destroy(s);
    }
    for (int i = 0; i < SCREEN_TIMERS; i++) {
        if (s->timers[i]) {
            lv_timer_delete(s->timers[i]);
        }
    }
    lv_free(s->state);
    if (delete_root) {
        lv_obj_delete(s->root);
    }
    memset(s, 0, sizeof *s);
}

// --- Edge swipe ----------------------------------------------------------------------

static void edge_event_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    lv_point_t p = {0};
    if (indev) {
        lv_indev_get_point(indev, &p);
    }
    if (code == LV_EVENT_PRESSED) {
        s_edge_start = p;
        lv_obj_set_y(s_edge_knob, p.y - EDGE_KNOB / 2);
    } else if (code == LV_EVENT_PRESSING) {
        int32_t dx = LV_CLAMP(0, p.x - s_edge_start.x, EDGE_TRIGGER_DX);
        lv_obj_set_x(s_edge_knob, dx - EDGE_KNOB);
        lv_obj_set_style_bg_color(s_edge_knob,
                                  dx >= EDGE_TRIGGER_DX ? ui_theme_accent_color() : ui_color(UI_COLOR_SURFACE_HI), 0);
        if (dx > 0) {
            lv_obj_remove_flag(s_edge_knob, LV_OBJ_FLAG_HIDDEN);
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        lv_obj_add_flag(s_edge_knob, LV_OBJ_FLAG_HIDDEN);
        const int32_t dx = p.x - s_edge_start.x;
        const int32_t dy = LV_ABS(p.y - s_edge_start.y);
        if (code == LV_EVENT_RELEASED && dx >= EDGE_TRIGGER_DX && dy < dx) {
            ui_nav_back();
        }
    }
}

// The top layer goes with its display (lv_deinit when the simulator's window closes), possibly
// before the base screen, whose delete callback then calls edge_update(): forget the objects.
static void edge_deleted_cb(lv_event_t *e)
{
    lv_obj_t **ref = lv_event_get_user_data(e);
    *ref = NULL;
}

static void edge_create(void)
{
    lv_obj_t *layer = lv_layer_top();
    s_edge = lv_obj_create(layer);
    lv_obj_remove_style_all(s_edge);
    lv_obj_set_size(s_edge, EDGE_W, LV_PCT(100));
    lv_obj_remove_flag(s_edge, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(s_edge, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_edge, edge_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(s_edge, edge_deleted_cb, LV_EVENT_DELETE, &s_edge);

    s_edge_knob = lv_obj_create(layer);
    lv_obj_remove_style_all(s_edge_knob);
    lv_obj_set_size(s_edge_knob, EDGE_KNOB, EDGE_KNOB);
    lv_obj_set_style_radius(s_edge_knob, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_edge_knob, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_edge_knob, ui_color(UI_COLOR_SURFACE_HI), 0);
    lv_obj_remove_flag(s_edge_knob, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_edge_knob, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_edge_knob, edge_deleted_cb, LV_EVENT_DELETE, &s_edge_knob);
    lv_obj_t *arrow = lv_label_create(s_edge_knob);
    lv_label_set_text(arrow, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(arrow, UI_FONT_CAPTION, 0);
    lv_obj_set_style_text_color(arrow, ui_color(UI_COLOR_TEXT), 0);
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -UI_SPACE_M, 0);
}

static void edge_update(void)
{
    if (s_edge == NULL || s_edge_knob == NULL) {
        return;
    }
    const ui_screen_t *top = ui_nav_top();
    const bool enabled = top && s_depth > 1 && s_active && !s_covered && !(top->def->flags & UI_SCREEN_NO_SWIPE_BACK);
    if (enabled) {
        lv_obj_remove_flag(s_edge, LV_OBJ_FLAG_HIDDEN);
        // Keep the strip below overlays created later (toast, banner, alert).
        lv_obj_move_to_index(s_edge, 0);
        lv_obj_move_to_index(s_edge_knob, 1);
    } else {
        lv_obj_add_flag(s_edge, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_edge_knob, LV_OBJ_FLAG_HIDDEN);
    }
}

// --- Clock ---------------------------------------------------------------------------

static time_t default_now(void)
{
    return time(NULL);
}

static time_t (*s_now)(void) = default_now;
static bool s_h24 = true;
static bool s_valid = true;
static lv_obj_t *s_clock_labels[CLOCK_LABELS_MAX];
static lv_timer_t *s_clock_timer;

// Objects shown only while the time is unknown (or only while it is known).
typedef struct {
    lv_obj_t *obj;
    bool show_when_unknown;
} clock_dep_t;
static clock_dep_t s_clock_deps[CLOCK_LABELS_MAX];

typedef struct {
    void (*cb)(void *ctx);
    void *ctx;
} clock_listener_t;
static clock_listener_t s_clock_listeners[CLOCK_LISTENERS];

static void clock_dep_apply(const clock_dep_t *d)
{
    if (d->show_when_unknown != s_valid) {
        lv_obj_remove_flag(d->obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(d->obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void clock_refresh(void)
{
    char buf[8];
    ui_clock_format(buf, sizeof buf);
    for (int i = 0; i < CLOCK_LABELS_MAX; i++) {
        if (s_clock_labels[i]) {
            lv_label_set_text(s_clock_labels[i], buf);
        }
        if (s_clock_deps[i].obj) {
            clock_dep_apply(&s_clock_deps[i]);
        }
    }
    for (int i = 0; i < CLOCK_LISTENERS; i++) {
        if (s_clock_listeners[i].cb) {
            s_clock_listeners[i].cb(s_clock_listeners[i].ctx);
        }
    }
}

/** Next period: up to the start of the next minute (at most one wake-up per minute). */
static uint32_t ms_to_next_minute(void)
{
    const time_t now = ui_clock_now();
    return (uint32_t)(60 - (now % 60)) * 1000u;
}

static void clock_timer_cb(lv_timer_t *t)
{
    clock_refresh();
    lv_timer_set_period(t, ms_to_next_minute());
}

static void clock_label_deleted(lv_event_t *e)
{
    lv_obj_t *label = lv_event_get_target(e);
    for (int i = 0; i < CLOCK_LABELS_MAX; i++) {
        if (s_clock_labels[i] == label) {
            s_clock_labels[i] = NULL;
        }
        if (s_clock_deps[i].obj == label) {
            s_clock_deps[i].obj = NULL;
        }
    }
}

void ui_clock_refresh(void)
{
    clock_refresh();
    if (s_clock_timer) {
        lv_timer_set_period(s_clock_timer, ms_to_next_minute());
        lv_timer_reset(s_clock_timer);
    }
}

void ui_clock_set_source(time_t (*now)(void))
{
    s_now = now ? now : default_now;
    ui_clock_refresh();
}

void ui_clock_set_valid(bool valid)
{
    if (valid != s_valid) {
        s_valid = valid;
        clock_refresh();
    }
}

bool ui_clock_is_valid(void)
{
    return s_valid;
}

void ui_clock_bind_unknown(lv_obj_t *obj, bool show_when_unknown)
{
    for (int i = 0; i < CLOCK_LABELS_MAX; i++) {
        if (s_clock_deps[i].obj == NULL) {
            s_clock_deps[i] = (clock_dep_t){.obj = obj, .show_when_unknown = show_when_unknown};
            lv_obj_add_event_cb(obj, clock_label_deleted, LV_EVENT_DELETE, NULL);
            clock_dep_apply(&s_clock_deps[i]);
            return;
        }
    }
    LV_LOG_WARN("ui_clock: no free slot");
}

time_t ui_clock_now(void)
{
    return s_now();
}

void ui_clock_set_24h(bool h24)
{
    s_h24 = h24;
    clock_refresh();
}

bool ui_clock_is_24h(void)
{
    return s_h24;
}

esp_err_t ui_clock_add_listener(void (*cb)(void *ctx), void *ctx)
{
    for (int i = 0; i < CLOCK_LISTENERS; i++) {
        if (s_clock_listeners[i].cb == NULL) {
            s_clock_listeners[i] = (clock_listener_t){.cb = cb, .ctx = ctx};
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

void ui_clock_remove_listener(void (*cb)(void *ctx), void *ctx)
{
    for (int i = 0; i < CLOCK_LISTENERS; i++) {
        if (s_clock_listeners[i].cb == cb && s_clock_listeners[i].ctx == ctx) {
            s_clock_listeners[i] = (clock_listener_t){0};
        }
    }
}

void ui_clock_format(char *buf, size_t len)
{
    if (!s_valid) {
        lv_snprintf(buf, len, "--:--");
        return;
    }
    const time_t now = ui_clock_now();
    struct tm tm;
    localtime_r(&now, &tm);
    if (s_h24) {
        lv_snprintf(buf, len, "%02d:%02d", tm.tm_hour, tm.tm_min);
    } else {
        const int h = tm.tm_hour % 12;
        lv_snprintf(buf, len, "%d:%02d", h == 0 ? 12 : h, tm.tm_min);
    }
}

void ui_clock_bind_label(lv_obj_t *label)
{
    for (int i = 0; i < CLOCK_LABELS_MAX; i++) {
        if (s_clock_labels[i] == NULL) {
            s_clock_labels[i] = label;
            lv_obj_add_event_cb(label, clock_label_deleted, LV_EVENT_DELETE, NULL);
            char buf[8];
            ui_clock_format(buf, sizeof buf);
            lv_label_set_text(label, buf);
            return;
        }
    }
    LV_LOG_WARN("ui_clock: no free label slot");
}

// --- Stack and transitions ---------------------------------------------------------
// All screens are full-size containers on one LVGL screen (s_base); only the top one
// is not hidden. Slides are done here rather than with lv_screen_load_anim() so a
// pop during a running push (fast double input) cannot leave a stale screen behind.

static lv_obj_t *s_base;
static struct {
    lv_obj_t *in;
    lv_obj_t *out;
    bool delete_out;
} s_trans;

static void anim_x_cb(void *var, int32_t v)
{
    lv_obj_set_x(var, v);
}

static void anim_y_cb(void *var, int32_t v)
{
    lv_obj_set_y(var, v);
}

static void anim_stop(lv_obj_t *obj)
{
    lv_anim_delete(obj, anim_x_cb);
    lv_anim_delete(obj, anim_y_cb);
    lv_obj_set_pos(obj, 0, 0);
}

/** Jump the running transition (if any) to its end state. */
static void trans_finish(void)
{
    lv_obj_t *in = s_trans.in;
    lv_obj_t *out = s_trans.out;
    const bool delete_out = s_trans.delete_out;
    memset(&s_trans, 0, sizeof s_trans);
    if (in) {
        anim_stop(in);
    }
    if (out) {
        anim_stop(out);
        if (delete_out) {
            lv_obj_delete(out);
        } else {
            lv_obj_add_flag(out, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void trans_completed_cb(lv_anim_t *a)
{
    (void)a;
    trans_finish();
}

static void slide(lv_obj_t *obj, bool vertical, int32_t from, int32_t to, lv_anim_completed_cb_t done)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, vertical ? anim_y_cb : anim_x_cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, UI_MOTION_NORMAL);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&a, done);
    lv_anim_start(&a);
}

/** Slide in over out. side: where the pushed screen comes from (push, forward) or
 *  where the popped screen goes (pop); the other screen moves the opposite way. */
static void transition(lv_obj_t *in, lv_obj_t *out, ui_slide_t side, bool forward, bool delete_out)
{
    trans_finish();
    lv_obj_remove_flag(in, LV_OBJ_FLAG_HIDDEN);
    lv_display_t *disp = lv_obj_get_display(s_base);
    const bool vertical = side == UI_SLIDE_FROM_TOP || side == UI_SLIDE_FROM_BOTTOM;
    const int32_t len = vertical ? lv_display_get_vertical_resolution(disp) : lv_display_get_horizontal_resolution(disp);
    const int32_t d = side == UI_SLIDE_FROM_RIGHT || side == UI_SLIDE_FROM_BOTTOM ? len : -len;
    s_trans.in = in;
    s_trans.out = out;
    s_trans.delete_out = delete_out;
    slide(out, vertical, 0, forward ? -d : d, NULL);
    slide(in, vertical, forward ? d : -d, 0, trans_completed_cb);
}

static ui_screen_t *create(const screen_def_t *def, const void *args)
{
    ui_screen_t *s = &s_stack[s_depth];
    memset(s, 0, sizeof *s);
    s->def = def;
    if (def->state_size > 0) {
        s->state = lv_malloc_zeroed(def->state_size);
        if (s->state == NULL) {
            return NULL;
        }
    }
    s->root = lv_obj_create(s_base);
    lv_obj_remove_style_all(s->root);
    lv_obj_set_size(s->root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s->root, ui_color(UI_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s->root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s->root, LV_OBJ_FLAG_HIDDEN);
    s_depth++;
    if (def->on_create) {
        def->on_create(s, s->root, args);
    }
    return s;
}

/** Something else (console `lcd bars`, factory test) loaded its own screen with
 *  auto-delete and took the base with it: end every screen without touching LVGL
 *  objects (they are being deleted). ui_nav_init() may be called again later. */
static void base_deleted_cb(lv_event_t *e)
{
    (void)e;
    memset(&s_trans, 0, sizeof s_trans);
    while (s_depth > 0) {
        ui_screen_t *s = &s_stack[--s_depth];
        set_visible(s, false);
        if (s->def->on_destroy) {
            s->def->on_destroy(s);
        }
        for (int i = 0; i < SCREEN_TIMERS; i++) {
            if (s->timers[i]) {
                lv_timer_delete(s->timers[i]);
            }
        }
        lv_free(s->state);
        memset(s, 0, sizeof *s);
    }
    s_base = NULL;
    edge_update();
    notify_listener();
    LV_LOG_WARN("ui_nav: base screen deleted, navigation stopped");
}

esp_err_t ui_nav_init(const screen_def_t *home, const void *args)
{
    if (home == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_depth != 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_base == NULL) {
        s_base = lv_obj_create(NULL);
        lv_obj_remove_flag(s_base, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(s_base, base_deleted_cb, LV_EVENT_DELETE, NULL);
    }
    if (s_edge == NULL) {
        edge_create();
    }
    if (s_clock_timer == NULL) {
        // Minute tick for clock labels; re-armed to each minute boundary (see ms_to_next_minute).
        s_clock_timer = lv_timer_create(clock_timer_cb, ms_to_next_minute(), NULL);
        if (!s_active) {
            lv_timer_pause(s_clock_timer);
        }
    }
    ui_screen_t *s = create(home, args);
    if (s == NULL) {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_remove_flag(s->root, LV_OBJ_FLAG_HIDDEN);
    // Fade from the boot screen, which is deleted afterwards.
    lv_screen_load_anim(s_base, LV_SCR_LOAD_ANIM_FADE_IN, UI_MOTION_NORMAL, 0, true);
    update_visibility();
    return ESP_OK;
}

esp_err_t ui_nav_push(const screen_def_t *def, const void *args)
{
    return ui_nav_push_slide(def, args, UI_SLIDE_FROM_RIGHT);
}

esp_err_t ui_nav_push_slide(const screen_def_t *def, const void *args, ui_slide_t from)
{
    if (def == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_depth == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_depth >= UI_NAV_MAX_DEPTH) {
        LV_LOG_WARN("ui_nav: stack full, cannot push %s", def->id);
        return ESP_ERR_NO_MEM;
    }
    lv_obj_t *below = s_stack[s_depth - 1].root;
    ui_screen_t *s = create(def, args);
    if (s == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s->slide = from;
    update_visibility();
    transition(s->root, below, from, true, false);
    return ESP_OK;
}

esp_err_t ui_nav_push_id(const char *id, const void *args)
{
    const screen_def_t *def = ui_nav_find(id);
    return def ? ui_nav_push(def, args) : ESP_ERR_NOT_FOUND;
}

/** A swipe closed the screen and the finger is still down: drop that press, so no
 *  event (release, press lost) reaches a widget of a screen whose state on_destroy
 *  is about to free, and ignore the touch until it ends. */
static void drop_pressed_inputs(void)
{
    for (lv_indev_t *i = lv_indev_get_next(NULL); i; i = lv_indev_get_next(i)) {
        if (lv_indev_get_state(i) == LV_INDEV_STATE_PRESSED) {
            lv_indev_reset(i, NULL);
            lv_indev_wait_release(i);
        }
    }
}

/** Destroy every screen above depth (>= 1) and slide the new top in. */
static void pop_to(size_t depth)
{
    trans_finish();
    drop_pressed_inputs();
    lv_obj_t *out = s_stack[s_depth - 1].root;
    const ui_slide_t side = s_stack[s_depth - 1].slide;
    destroy(&s_stack[s_depth - 1], false); // root deleted when the slide ends
    s_depth--;
    while (s_depth > depth) {
        destroy(&s_stack[s_depth - 1], true); // hidden, not animating
        s_depth--;
    }
    update_visibility();
    transition(s_stack[s_depth - 1].root, out, side, false, true);
}

bool ui_nav_pop(void)
{
    if (s_depth <= 1) {
        return false;
    }
    pop_to(s_depth - 1);
    return true;
}

bool ui_nav_back(void)
{
    if (ui_alert_handle_back()) {
        return true;
    }
    ui_screen_t *top = ui_nav_top();
    if (top && top->def->on_back && top->def->on_back(top)) {
        return true;
    }
    return ui_nav_pop();
}

void ui_nav_home(void)
{
    if (s_depth > 1) {
        pop_to(1);
    }
}

static int home_swipe_index(lv_dir_t dir)
{
    switch (dir) {
    case LV_DIR_LEFT:
        return 0;
    case LV_DIR_RIGHT:
        return 1;
    case LV_DIR_TOP:
        return 2;
    case LV_DIR_BOTTOM:
        return 3;
    default:
        return -1;
    }
}

void ui_nav_set_home_swipe(lv_dir_t dir, const screen_def_t *def)
{
    const int i = home_swipe_index(dir);
    if (i >= 0) {
        s_home_swipe[i] = def;
    }
}

bool ui_nav_home_swipe(lv_dir_t dir)
{
    static const ui_slide_t FROM[] = {UI_SLIDE_FROM_RIGHT, UI_SLIDE_FROM_LEFT, UI_SLIDE_FROM_BOTTOM, UI_SLIDE_FROM_TOP};
    const int i = home_swipe_index(dir);
    if (i < 0 || s_home_swipe[i] == NULL || s_depth != 1 || s_covered) {
        return false;
    }
    // The panel follows the finger: a swipe to the left brings it in from the right.
    return ui_nav_push_slide(s_home_swipe[i], NULL, FROM[i]) == ESP_OK;
}

void ui_nav_set_active(bool active)
{
    if (active == s_active) {
        return;
    }
    s_active = active;
    if (s_clock_timer) {
        if (active) {
            clock_refresh();
            lv_timer_set_period(s_clock_timer, ms_to_next_minute());
            lv_timer_reset(s_clock_timer);
            lv_timer_resume(s_clock_timer);
        } else {
            lv_timer_pause(s_clock_timer);
        }
    }
    update_visibility();
}

bool ui_nav_is_active(void)
{
    return s_active;
}

void ui_nav_set_listener(void (*cb)(void *ctx), void *ctx)
{
    s_listener_ctx = ctx;
    s_listener = cb;
}

void ui_nav_set_covered(bool covered)
{
    s_covered = covered;
    update_visibility();
}

bool ui_nav_top_is_fullscreen(void)
{
    const ui_screen_t *top = ui_nav_top();
    return top && (top->def->flags & UI_SCREEN_FULLSCREEN);
}

size_t ui_nav_depth(void)
{
    return s_depth;
}

ui_screen_t *ui_nav_top(void)
{
    return s_depth ? &s_stack[s_depth - 1] : NULL;
}

ui_screen_t *ui_nav_at(size_t index)
{
    return index < s_depth ? &s_stack[index] : NULL;
}

esp_err_t ui_nav_register(const screen_def_t *def)
{
    if (def == NULL || def->id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (ui_nav_find(def->id)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_registry_n >= REGISTRY_MAX) {
        return ESP_ERR_NO_MEM;
    }
    s_registry[s_registry_n++] = def;
    return ESP_OK;
}

const screen_def_t *ui_nav_find(const char *id)
{
    for (size_t i = 0; id && i < s_registry_n; i++) {
        if (strcmp(s_registry[i]->id, id) == 0) {
            return s_registry[i];
        }
    }
    return NULL;
}

// --- Screen instance -------------------------------------------------------------------

const screen_def_t *ui_screen_def(const ui_screen_t *s)
{
    return s->def;
}

lv_obj_t *ui_screen_root(const ui_screen_t *s)
{
    return s->root;
}

void *ui_screen_state(const ui_screen_t *s)
{
    return s->state;
}

bool ui_screen_is_visible(const ui_screen_t *s)
{
    return s->visible;
}

lv_timer_t *ui_screen_timer_create(ui_screen_t *s, lv_timer_cb_t cb, uint32_t period_ms, void *user_data)
{
    for (int i = 0; i < SCREEN_TIMERS; i++) {
        if (s->timers[i] == NULL) {
            lv_timer_t *t = lv_timer_create(cb, period_ms, user_data);
            if (t && !s->visible) {
                lv_timer_pause(t);
            }
            s->timers[i] = t;
            return t;
        }
    }
    return NULL;
}
