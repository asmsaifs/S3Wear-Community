// Toast, banner and full-screen alert on lv_layer_top().
#include "ui_overlay.h"

#include <string.h>

#include "ui_internal.h"
#include "ui_nav.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define TOP_Y       UI_SAFE_INSET
#define BANNER_H    112
#define ALERT_BTN_W 300

// --- Shared ----------------------------------------------------------------------

static void opa_cb(void *obj, int32_t v)
{
    lv_obj_set_style_opa(obj, (lv_opa_t)v, 0);
}

static void fade(lv_obj_t *obj, lv_opa_t from, lv_opa_t to, uint32_t delay, lv_anim_completed_cb_t done)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, opa_cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, UI_MOTION_FAST);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_completed_cb(&a, done);
    lv_anim_start(&a);
}

static void delete_on_done(lv_anim_t *a)
{
    lv_obj_delete(a->var);
}

static lv_obj_t *pill(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_style_bg_color(o, ui_color(UI_COLOR_SURFACE_HI), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

// --- Toast -------------------------------------------------------------------------

static lv_obj_t *s_toast;
static uint32_t s_toast_ms;

static void toast_fade_out_start(lv_anim_t *a)
{
    // From here on the toast belongs to its fade-out; a new toast leaves it alone.
    if (s_toast == a->var) {
        s_toast = NULL;
    }
}

/** Fade-in done: hold, then fade out and delete. Chained rather than started
 *  together because LVGL drops an animation when another one starts on the same
 *  object and property. */
static void toast_shown(lv_anim_t *in)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, in->var);
    lv_anim_set_exec_cb(&a, opa_cb);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&a, UI_MOTION_FAST);
    lv_anim_set_delay(&a, s_toast_ms);
    lv_anim_set_early_apply(&a, false); // stay opaque during the delay
    lv_anim_set_start_cb(&a, toast_fade_out_start);
    lv_anim_set_completed_cb(&a, delete_on_done);
    lv_anim_start(&a);
}

static void toast_remove_now(void)
{
    if (s_toast) {
        lv_anim_delete(s_toast, NULL);
        lv_obj_delete(s_toast);
        s_toast = NULL;
    }
}

void ui_toast_show(const char *text, uint32_t ms)
{
    toast_remove_now();
    s_toast_ms = ms ? ms : UI_TOAST_MS_DEFAULT;
    s_toast = pill(lv_layer_top());
    lv_obj_set_style_radius(s_toast, UI_RADIUS_BUTTON, 0);
    lv_obj_set_style_pad_hor(s_toast, UI_SPACE_L, 0);
    lv_obj_set_style_pad_ver(s_toast, UI_SPACE_M, 0);
    lv_obj_set_size(s_toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *label = lv_label_create(s_toast);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_style_text_font(label, UI_FONT_CAPTION, 0);
    lv_obj_set_style_text_color(label, ui_color(UI_COLOR_TEXT), 0);
    lv_obj_set_style_max_width(label, 300, 0);
    lv_obj_align(s_toast, LV_ALIGN_TOP_MID, 0, TOP_Y);
    fade(s_toast, LV_OPA_TRANSP, LV_OPA_COVER, 0, toast_shown);
}

// --- Banner ------------------------------------------------------------------------

static lv_obj_t *s_banner;
static lv_obj_t *s_banner_col; // the text column of s_banner
static lv_timer_t *s_banner_timer;
static uint32_t s_banner_id;
static uint32_t s_banner_ms;
static void (*s_banner_tap)(void *ctx);
static void *s_banner_ctx;

static void banner_close(void)
{
    if (s_banner_timer) {
        lv_timer_delete(s_banner_timer);
        s_banner_timer = NULL;
    }
    if (s_banner) {
        lv_obj_t *b = s_banner;
        s_banner = NULL;
        s_banner_col = NULL;
        lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
        fade(b, LV_OPA_COVER, LV_OPA_TRANSP, 0, delete_on_done);
    }
}

static void banner_timeout_cb(lv_timer_t *t)
{
    (void)t;
    s_banner_timer = NULL; // auto-deleted (repeat count 1)
    banner_close();
}

void ui_banner_dismiss(void)
{
    banner_close();
}

static void banner_click_cb(lv_event_t *e)
{
    (void)e;
    void (*tap)(void *) = s_banner_tap;
    void *ctx = s_banner_ctx;
    banner_close();
    if (tap) {
        tap(ctx);
    }
}

// Phone-rendered text, scaled to the column's width; the column cuts the rows below it.
static void banner_text_image(lv_obj_t *col, const lv_image_dsc_t *d)
{
    lv_obj_update_layout(s_banner);
    const int32_t w = lv_obj_get_content_width(col);
    const int32_t scale = d->header.w > 0 ? w * LV_SCALE_NONE / d->header.w : LV_SCALE_NONE;
    lv_obj_t *img = lv_image_create(col);
    lv_image_set_src(img, d);
    lv_image_set_inner_align(img, LV_IMAGE_ALIGN_TOP_LEFT);
    lv_image_set_pivot(img, 0, 0);
    lv_image_set_scale(img, (uint32_t)scale);
    lv_obj_set_size(img, w, LV_MIN(d->header.h * scale / LV_SCALE_NONE, lv_obj_get_content_height(col)));
    lv_obj_set_style_image_recolor(img, ui_color(UI_COLOR_TEXT), 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
}

static void banner_restart_timer(void)
{
    if (s_banner_timer) {
        lv_timer_delete(s_banner_timer);
    }
    s_banner_timer = lv_timer_create(banner_timeout_cb, s_banner_ms, NULL);
    lv_timer_set_repeat_count(s_banner_timer, 1);
}

bool ui_banner_set_text_image(uint32_t id, const lv_image_dsc_t *text_image)
{
    if (!s_banner_col || id != s_banner_id || !text_image) {
        return false;
    }
    lv_obj_clean(s_banner_col);
    banner_text_image(s_banner_col, text_image);
    banner_restart_timer();
    return true;
}

uint32_t ui_banner_show(const ui_banner_t *banner)
{
    if (banner == NULL || ui_alert_is_active() || ui_nav_top_is_fullscreen()) {
        return 0;
    }
    if (s_banner_timer) {
        lv_timer_delete(s_banner_timer);
        s_banner_timer = NULL;
    }
    if (s_banner) {
        lv_anim_delete(s_banner, NULL);
        lv_obj_delete(s_banner);
    }
    s_banner_tap = banner->on_tap;
    s_banner_ctx = banner->ctx;

    lv_obj_t *b = pill(lv_layer_top());
    s_banner = b;
    lv_obj_set_style_radius(b, UI_RADIUS_CARD, 0);
    lv_obj_set_size(b, lv_display_get_horizontal_resolution(lv_obj_get_display(b)) - 2 * UI_SAFE_INSET, BANNER_H);
    lv_obj_set_style_pad_all(b, UI_SPACE_M, 0);
    lv_obj_set_style_pad_column(b, UI_SPACE_M, 0);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, TOP_Y);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, banner_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    if (banner->image) {
        lv_obj_t *img = lv_image_create(b);
        lv_image_set_src(img, banner->image);
    } else if (banner->icon) {
        lv_obj_t *icon = lv_label_create(b);
        lv_label_set_text(icon, banner->icon);
        lv_obj_set_style_text_font(icon, UI_FONT_TITLE, 0);
        lv_obj_set_style_text_color(icon, ui_theme_accent_color(), 0);
    }
    lv_obj_t *col = lv_obj_create(b);
    s_banner_col = col;
    lv_obj_remove_style_all(col);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_height(col, banner->text_image ? LV_PCT(100) : LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    s_banner_ms = banner->ms ? banner->ms : UI_BANNER_MS_DEFAULT;
    if (++s_banner_id == 0) {
        s_banner_id = 1;
    }
    if (banner->text_image) {
        banner_text_image(col, banner->text_image);
        fade(b, LV_OPA_TRANSP, LV_OPA_COVER, 0, NULL);
        banner_restart_timer();
        return s_banner_id;
    }
    lv_obj_t *title = lv_label_create(col);
    lv_obj_set_size(title, LV_PCT(100), lv_font_get_line_height(UI_FONT_BODY));
    lv_obj_set_style_text_font(title, UI_FONT_BODY, 0);
    s3w_label_set_fit_text(title, banner->title ? banner->title : "");
    lv_obj_set_style_text_color(title, ui_color(UI_COLOR_TEXT), 0);
    if (banner->body) {
        lv_obj_t *body = lv_label_create(col);
        lv_obj_set_size(body, LV_PCT(100), lv_font_get_line_height(UI_FONT_CAPTION));
        lv_obj_set_style_text_font(body, UI_FONT_CAPTION, 0);
        s3w_label_set_fit_text(body, banner->body);
        lv_obj_set_style_text_color(body, ui_color(UI_COLOR_TEXT_DIM), 0);
    }

    fade(b, LV_OPA_TRANSP, LV_OPA_COVER, 0, NULL);
    banner_restart_timer();
    return s_banner_id;
}

// --- Alert -------------------------------------------------------------------------

typedef struct {
    bool used;
    char *icon, *title, *body, *primary, *secondary;
    uint32_t accent;
    void (*on_result)(int button, void *ctx);
    void *ctx;
    ui_alert_prio_t prio;
    bool back_dismisses;
} alert_copy_t;

static lv_obj_t *s_alert;
static alert_copy_t s_cur;
static alert_copy_t s_waiting;

static char *dup(const char *s)
{
    return s ? lv_strdup(s) : NULL;
}

static void copy_free(alert_copy_t *c)
{
    lv_free(c->icon);
    lv_free(c->title);
    lv_free(c->body);
    lv_free(c->primary);
    lv_free(c->secondary);
    memset(c, 0, sizeof *c);
}

static void copy_make(alert_copy_t *c, const ui_alert_t *a)
{
    copy_free(c);
    *c = (alert_copy_t){
        .used = true,
        .icon = dup(a->icon),
        .title = dup(a->title),
        .body = dup(a->body),
        .primary = dup(a->primary),
        .secondary = dup(a->secondary),
        .accent = a->accent,
        .on_result = a->on_result,
        .ctx = a->ctx,
        .prio = a->prio,
        .back_dismisses = a->back_dismisses,
    };
}

static void alert_build(void);

/** Hide now, delete on the next timer run: this can be called from the alert's own
 *  button event, where deleting synchronously is not allowed. */
static void alert_remove(void)
{
    if (s_alert) {
        lv_obj_add_flag(s_alert, LV_OBJ_FLAG_HIDDEN);
        lv_obj_delete_async(s_alert);
        s_alert = NULL;
    }
}

/** Close the current alert; show the waiting one if any; then report the result. */
static void alert_close(int button, bool report)
{
    if (!s_cur.used) {
        return;
    }
    void (*cb)(int, void *) = report ? s_cur.on_result : NULL;
    void *ctx = s_cur.ctx;
    copy_free(&s_cur);
    alert_remove();
    if (s_waiting.used) {
        s_cur = s_waiting;
        memset(&s_waiting, 0, sizeof s_waiting);
        alert_build();
    } else {
        ui_nav_set_covered(false);
    }
    if (cb) {
        cb(button, ctx);
    }
}

static void alert_btn_cb(lv_event_t *e)
{
    alert_close((int)(intptr_t)lv_event_get_user_data(e), true);
}

static lv_obj_t *alert_button(lv_obj_t *parent, const char *text, lv_color_t bg, int index)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_width(btn, ALERT_BTN_W);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_add_event_cb(btn, alert_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)index);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, UI_FONT_BODY, 0);
    lv_obj_center(label);
    return btn;
}

static void alert_build(void)
{
    const lv_color_t accent = s_cur.accent ? lv_color_hex(s_cur.accent) : ui_theme_accent_color();
    lv_obj_t *a = lv_obj_create(lv_layer_top());
    s_alert = a;
    lv_obj_remove_style_all(a);
    lv_obj_set_size(a, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(a, ui_color(UI_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(a, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(a, UI_SAFE_INSET + UI_SPACE_M, 0);
    lv_obj_set_style_pad_ver(a, UI_SPACE_XL + UI_SPACE_L, 0);
    lv_obj_set_style_pad_row(a, UI_SPACE_M, 0);
    lv_obj_add_flag(a, LV_OBJ_FLAG_CLICKABLE); // swallow touches meant for the screen below
    lv_obj_remove_flag(a, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(a, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(a, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    if (s_cur.icon) {
        lv_obj_t *icon = lv_label_create(a);
        lv_label_set_text(icon, s_cur.icon);
        lv_obj_set_style_text_font(icon, UI_FONT_TITLE, 0);
        lv_obj_set_style_text_color(icon, accent, 0);
    }
    lv_obj_t *title = lv_label_create(a);
    lv_label_set_text(title, s_cur.title ? s_cur.title : "");
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(title, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(title, ui_color(UI_COLOR_TEXT), 0);
    if (s_cur.body) {
        lv_obj_t *body = lv_label_create(a);
        lv_label_set_text(body, s_cur.body);
        lv_obj_set_width(body, LV_PCT(100));
        lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(body, UI_FONT_BODY, 0);
        lv_obj_set_style_text_color(body, ui_color(UI_COLOR_TEXT_DIM), 0);
    }
    lv_obj_t *spacer = lv_obj_create(a);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_size(spacer, 1, UI_SPACE_M);
    if (s_cur.primary) {
        alert_button(a, s_cur.primary, accent, 0);
    }
    if (s_cur.secondary) {
        alert_button(a, s_cur.secondary, ui_color(UI_COLOR_SURFACE_HI), 1);
    }
    ui_nav_set_covered(true);
}

void ui_alert_show(const ui_alert_t *alert)
{
    if (alert == NULL) {
        return;
    }
    if (s_cur.used && alert->prio <= s_cur.prio) {
        copy_make(&s_waiting, alert);
        return;
    }
    if (s_cur.used) {
        // Preempted: the lower alert waits (replacing an older waiting one).
        copy_free(&s_waiting);
        s_waiting = s_cur;
        memset(&s_cur, 0, sizeof s_cur);
        alert_remove();
    }
    banner_close();
    copy_make(&s_cur, alert);
    alert_build();
}

void ui_alert_dismiss(void)
{
    alert_close(-1, false);
}

void ui_alert_cancel(void (*on_result)(int button, void *ctx), void *ctx)
{
    if (s_waiting.used && s_waiting.on_result == on_result && s_waiting.ctx == ctx) {
        copy_free(&s_waiting);
    }
    if (s_cur.used && s_cur.on_result == on_result && s_cur.ctx == ctx) {
        alert_close(-1, false);
    }
}

bool ui_alert_is_active(void)
{
    return s_cur.used;
}

bool ui_alert_handle_back(void)
{
    if (!s_cur.used) {
        return false;
    }
    if (s_cur.back_dismisses) {
        alert_close(-1, true);
    }
    return true; // a non-dismissable alert still swallows BACK
}

void ui_overlay_clear(void)
{
    toast_remove_now();
    if (s_banner_timer) {
        lv_timer_delete(s_banner_timer);
        s_banner_timer = NULL;
    }
    if (s_banner) {
        lv_anim_delete(s_banner, NULL);
        lv_obj_delete(s_banner);
        s_banner = NULL;
        s_banner_col = NULL;
    }
    copy_free(&s_waiting);
    if (s_cur.used) {
        alert_close(-1, false);
    }
}
