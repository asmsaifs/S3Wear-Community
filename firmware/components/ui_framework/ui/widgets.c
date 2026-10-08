// Standard widgets. Shared lv_style_t objects (not local styles) keep per-row memory
// and restyle cost low; nothing here changes styles while a list scrolls (docs/02 §6).
#include "ui_widgets.h"

#include <stdio.h>

#include "ui_nav.h"
#include "ui_theme.h"

#define ROW_MIN_H       88
#define LIST_PAD_BOTTOM 64 // keep the last row above the rounded bottom corners
#define DOT             10
#define DOT_ACTIVE_W    24

static bool s_ready;
static lv_style_t s_list;
static lv_style_t s_row;
static lv_style_t s_row_pressed;
static lv_style_t s_transp;

static void styles_init(void)
{
    if (s_ready) {
        return;
    }
    s_ready = true;
    lv_style_init(&s_list);
    lv_style_set_bg_opa(&s_list, LV_OPA_TRANSP);
    lv_style_set_border_width(&s_list, 0);
    lv_style_set_radius(&s_list, 0);
    lv_style_set_pad_hor(&s_list, UI_SAFE_INSET);
    lv_style_set_pad_top(&s_list, UI_SAFE_INSET);
    lv_style_set_pad_bottom(&s_list, LIST_PAD_BOTTOM);
    lv_style_set_pad_row(&s_list, UI_SPACE_S);

    lv_style_init(&s_row);
    lv_style_set_bg_color(&s_row, ui_color(UI_COLOR_SURFACE));
    lv_style_set_bg_opa(&s_row, LV_OPA_COVER);
    lv_style_set_radius(&s_row, UI_RADIUS_CARD);
    lv_style_set_border_width(&s_row, 0);
    lv_style_set_pad_hor(&s_row, UI_SPACE_L);
    lv_style_set_pad_ver(&s_row, UI_SPACE_M);
    lv_style_set_pad_column(&s_row, UI_SPACE_M);
    lv_style_set_pad_row(&s_row, UI_SPACE_S);
    lv_style_set_min_height(&s_row, ROW_MIN_H);
    lv_style_set_width(&s_row, LV_PCT(100));
    lv_style_set_height(&s_row, LV_SIZE_CONTENT);

    lv_style_init(&s_row_pressed);
    lv_style_set_bg_color(&s_row_pressed, ui_color(UI_COLOR_SURFACE_HI));

    lv_style_init(&s_transp);
    lv_style_set_bg_opa(&s_transp, LV_OPA_TRANSP);
    lv_style_set_border_width(&s_transp, 0);
    lv_style_set_pad_all(&s_transp, 0);
    lv_style_set_radius(&s_transp, 0);
}

static lv_obj_t *plain(lv_obj_t *parent)
{
    styles_init();
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_add_style(o, &s_transp, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text ? text : "");
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, ui_color(color), 0);
    return l;
}

/** One line, full width, cut with "..." (s3w_label_fit needs a fixed height). */
static void single_line(lv_obj_t *l, const lv_font_t *font)
{
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_set_height(l, lv_font_get_line_height(font));
    s3w_label_fit(l);
}

// --- Lists ----------------------------------------------------------------------------

lv_obj_t *s3w_list_create(lv_obj_t *parent)
{
    styles_init();
    lv_obj_t *list = lv_obj_create(parent);
    lv_obj_remove_style_all(list);
    lv_obj_add_style(list, &s_list, 0);
    lv_obj_set_size(list, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    // Stays clickable: a drag that starts between rows must still find the list.
    return list;
}

lv_obj_t *s3w_list_add_section(lv_obj_t *list, const char *title)
{
    lv_obj_t *l = label(list, title, UI_FONT_CAPTION, UI_COLOR_TEXT_DIM);
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_set_style_pad_left(l, UI_SPACE_L, 0);
    lv_obj_set_style_pad_top(l, UI_SPACE_M, 0);
    return l;
}

static lv_obj_t *row_base(lv_obj_t *list)
{
    styles_init();
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_add_style(row, &s_row, 0);
    lv_obj_add_style(row, &s_row_pressed, LV_STATE_PRESSED);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return row;
}

static void row_texts(lv_obj_t *row, const char *icon, const char *title, const char *subtitle)
{
    if (icon) {
        lv_obj_t *i = lv_label_create(row);
        lv_label_set_text(i, icon);
        lv_obj_set_style_text_font(i, UI_FONT_BODY, 0);
        lv_obj_set_style_text_color(i, ui_theme_accent_color(), 0);
        lv_obj_set_width(i, 32);
        lv_obj_set_style_text_align(i, LV_TEXT_ALIGN_CENTER, 0);
    }
    lv_obj_t *col = plain(row);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    single_line(label(col, title, UI_FONT_BODY, UI_COLOR_TEXT), UI_FONT_BODY);
    if (subtitle) {
        single_line(label(col, subtitle, UI_FONT_CAPTION, UI_COLOR_TEXT_DIM), UI_FONT_CAPTION);
    }
}

lv_obj_t *s3w_list_add_row(lv_obj_t *list, const char *icon, const char *title, const char *subtitle,
                           const char *trailing)
{
    lv_obj_t *row = row_base(list);
    row_texts(row, icon, title, subtitle);
    if (trailing) {
        label(row, trailing, UI_FONT_CAPTION, UI_COLOR_TEXT_DIM);
    }
    return row;
}

static void toggle_row_clicked(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_user_data(e);
    if (lv_obj_has_state(sw, LV_STATE_CHECKED)) {
        lv_obj_remove_state(sw, LV_STATE_CHECKED);
    } else {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    lv_obj_send_event(sw, LV_EVENT_VALUE_CHANGED, NULL);
}

lv_obj_t *s3w_toggle_row(lv_obj_t *list, const char *icon, const char *title, bool on)
{
    lv_obj_t *row = row_base(list);
    row_texts(row, icon, title, NULL);
    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_size(sw, 64, 36);
    lv_obj_remove_flag(sw, LV_OBJ_FLAG_CLICKABLE); // the whole row is the target
    lv_obj_set_style_bg_color(sw, ui_color(UI_COLOR_SURFACE_HI), LV_PART_MAIN);
    if (on) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(row, toggle_row_clicked, LV_EVENT_CLICKED, sw);
    return sw;
}

typedef struct {
    lv_obj_t *value;
    const char *unit; // static string from the caller
} slider_row_t;

static void slider_row_update(lv_obj_t *slider, slider_row_t *r)
{
    lv_label_set_text_fmt(r->value, "%" LV_PRId32 "%s", lv_slider_get_value(slider), r->unit ? r->unit : "");
}

static void slider_row_event(lv_event_t *e)
{
    slider_row_t *r = lv_event_get_user_data(e);
    if (lv_event_get_code(e) == LV_EVENT_DELETE) {
        lv_free(r);
    } else {
        slider_row_update(lv_event_get_target(e), r);
    }
}

lv_obj_t *s3w_slider_row(lv_obj_t *list, const char *title, int32_t min, int32_t max, int32_t value,
                         const char *unit)
{
    lv_obj_t *row = row_base(list);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_bottom(row, UI_SPACE_L, 0);
    lv_obj_t *t = label(row, title, UI_FONT_BODY, UI_COLOR_TEXT);
    lv_obj_set_flex_grow(t, 1);
    slider_row_t *r = lv_malloc(sizeof *r);
    LV_ASSERT_MALLOC(r);
    r->value = label(row, "", UI_FONT_CAPTION, UI_COLOR_TEXT_DIM);
    r->unit = unit;

    lv_obj_t *slider = lv_slider_create(row);
    lv_obj_set_width(slider, LV_PCT(100));
    lv_obj_set_height(slider, 12);
    // Extra touch area: the visible bar is thin, the target must stay >= 64 px.
    lv_obj_set_ext_click_area(slider, (UI_TOUCH_MIN - 12) / 2);
    lv_obj_set_style_margin_top(slider, UI_SPACE_M, 0);
    lv_obj_set_style_bg_color(slider, ui_color(UI_COLOR_SURFACE_HI), LV_PART_MAIN);
    lv_slider_set_range(slider, min, max);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, slider_row_event, LV_EVENT_VALUE_CHANGED, r);
    lv_obj_add_event_cb(slider, slider_row_event, LV_EVENT_DELETE, r);
    slider_row_update(slider, r);
    return slider;
}

// --- Buttons and containers ---------------------------------------------------------------

lv_obj_t *s3w_button_create(lv_obj_t *parent, s3w_button_kind_t kind, const char *text)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_height(btn, UI_TOUCH_MIN);
    lv_obj_set_style_min_width(btn, 160, 0);
    if (kind == S3W_BUTTON_SECONDARY) {
        lv_obj_set_style_bg_color(btn, ui_color(UI_COLOR_SURFACE_HI), 0);
    } else if (kind == S3W_BUTTON_DANGER) {
        lv_obj_set_style_bg_color(btn, ui_color(UI_COLOR_DANGER), 0);
    }
    lv_obj_t *l = label(btn, text, UI_FONT_BODY, UI_COLOR_TEXT);
    lv_obj_center(l);
    return btn;
}

lv_obj_t *s3w_card_create(lv_obj_t *parent)
{
    styles_init();
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_add_style(card, &s_row, 0);
    lv_obj_set_style_pad_all(card, UI_SPACE_L, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return card;
}

lv_obj_t *s3w_header_create(lv_obj_t *parent, const char *title)
{
    lv_obj_t *h = plain(parent);
    lv_obj_set_size(h, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_bottom(h, UI_SPACE_S, 0);
    lv_obj_set_flex_flow(h, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(h, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *clock = label(h, "", UI_FONT_CAPTION, UI_COLOR_TEXT);
    ui_clock_bind_label(clock);
    lv_obj_t *t = label(h, title, UI_FONT_TITLE, UI_COLOR_TEXT);
    lv_obj_set_style_max_width(t, LV_PCT(100), 0);
    s3w_label_fit(t); // wraps (no height limit) like before, never rewritten in place
    return h;
}

lv_obj_t *s3w_empty_state_create(lv_obj_t *parent, const char *icon, const char *title, const char *hint)
{
    lv_obj_t *e = plain(parent);
    lv_obj_set_size(e, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(e, UI_SPACE_XL, 0);
    lv_obj_set_style_pad_row(e, UI_SPACE_M, 0);
    lv_obj_set_flex_flow(e, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(e, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if (icon) {
        label(e, icon, UI_FONT_TITLE, UI_COLOR_TEXT_DIM);
    }
    lv_obj_t *t = label(e, title, UI_FONT_BODY, UI_COLOR_TEXT);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    if (hint) {
        lv_obj_t *h = label(e, hint, UI_FONT_CAPTION, UI_COLOR_TEXT_DIM);
        lv_obj_set_width(h, LV_PCT(90));
        lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
    }
    return e;
}

// --- Indicators -------------------------------------------------------------------------

lv_obj_t *s3w_ring_create(lv_obj_t *parent, int32_t diameter, int32_t thickness, uint32_t color_hex)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, diameter, diameter);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 0);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    const lv_color_t c = lv_color_hex(color_hex);
    lv_obj_set_style_pad_all(arc, 0, 0);
    lv_obj_set_style_arc_width(arc, thickness, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, thickness, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_mix(c, lv_color_black(), LV_OPA_20), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, c, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    return arc;
}

void s3w_ring_set_value(lv_obj_t *ring, int32_t percent)
{
    lv_arc_set_value(ring, LV_CLAMP(0, percent, 100));
}

lv_obj_t *s3w_page_dots_create(lv_obj_t *parent, uint8_t count)
{
    lv_obj_t *d = plain(parent);
    lv_obj_set_size(d, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(d, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(d, UI_SPACE_S, 0);
    for (uint8_t i = 0; i < count; i++) {
        lv_obj_t *dot = lv_obj_create(d);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, DOT, DOT);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(dot, ui_color(UI_COLOR_SURFACE_HI), 0);
    }
    s3w_page_dots_set_active(d, 0);
    return d;
}

void s3w_page_dots_set_active(lv_obj_t *dots, uint8_t index)
{
    const uint32_t n = lv_obj_get_child_count(dots);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *dot = lv_obj_get_child(dots, (int32_t)i);
        const bool active = i == index;
        lv_obj_set_width(dot, active ? DOT_ACTIVE_W : DOT);
        lv_obj_set_style_bg_color(dot, ui_color(active ? UI_COLOR_TEXT : UI_COLOR_SURFACE_HI), 0);
    }
}

lv_obj_t *s3w_qr_create(lv_obj_t *parent, const char *data, int32_t size)
{
    lv_obj_t *box = plain(parent);
    lv_obj_set_size(box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(box, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, UI_SPACE_M, 0);
    lv_obj_set_style_pad_all(box, UI_SPACE_M, 0); // quiet zone
    lv_obj_t *qr = lv_qrcode_create(box);
    lv_qrcode_set_size(qr, size);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    if (data) {
        lv_qrcode_update(qr, data, (uint32_t)lv_strlen(data));
    }
    return box;
}

// --- Input -------------------------------------------------------------------------------

typedef struct {
    int32_t min;
    int32_t step;
} picker_t;

static void picker_deleted(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

lv_obj_t *s3w_picker_create(lv_obj_t *parent, int32_t min, int32_t max, int32_t step, int32_t value, bool wrap)
{
    if (step <= 0 || max < min) {
        return NULL;
    }
    const int32_t n = (max - min) / step + 1;
    // Up to 4 digits + '\n' per option.
    char *opts = lv_malloc((size_t)n * 6 + 1);
    picker_t *p = lv_malloc(sizeof *p);
    if (opts == NULL || p == NULL) {
        lv_free(opts);
        lv_free(p);
        return NULL;
    }
    size_t pos = 0;
    for (int32_t i = 0; i < n; i++) {
        pos += (size_t)lv_snprintf(opts + pos, 7, i + 1 < n ? "%02" LV_PRId32 "\n" : "%02" LV_PRId32,
                                   min + i * step);
    }
    p->min = min;
    p->step = step;

    lv_obj_t *r = lv_roller_create(parent);
    lv_roller_set_options(r, opts, wrap ? LV_ROLLER_MODE_INFINITE : LV_ROLLER_MODE_NORMAL);
    lv_free(opts); // the roller keeps its own copy
    lv_roller_set_visible_row_count(r, 3);
    lv_obj_set_width(r, 128);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(r, 0, LV_PART_MAIN);
    lv_obj_set_style_text_font(r, UI_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(r, ui_color(UI_COLOR_TEXT_DIM), LV_PART_MAIN);
    lv_obj_set_style_text_align(r, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(r, UI_SPACE_L, LV_PART_MAIN);
    lv_obj_set_style_text_font(r, UI_FONT_TITLE, LV_PART_SELECTED);
    lv_obj_set_style_text_color(r, ui_color(UI_COLOR_TEXT), LV_PART_SELECTED);
    lv_obj_set_style_bg_color(r, ui_color(UI_COLOR_SURFACE_HI), LV_PART_SELECTED);
    lv_obj_set_style_radius(r, UI_RADIUS_CARD, LV_PART_SELECTED);
    lv_obj_add_event_cb(r, picker_deleted, LV_EVENT_DELETE, p);
    lv_obj_set_user_data(r, p);
    lv_roller_set_selected(r, (uint32_t)(LV_CLAMP(min, value, max) - min) / (uint32_t)step, LV_ANIM_OFF);
    return r;
}

int32_t s3w_picker_get_value(lv_obj_t *picker)
{
    const picker_t *p = lv_obj_get_user_data(picker);
    return p->min + (int32_t)lv_roller_get_selected(picker) * p->step;
}

typedef struct {
    void (*cb)(bool confirmed, void *ctx);
    void *ctx;
} dialog_t;

static void dialog_btn_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_current_target(e);
    lv_obj_t *dlg = lv_obj_get_parent(lv_obj_get_parent(btn));
    dialog_t d = *(dialog_t *)lv_obj_get_user_data(dlg);
    const bool confirmed = (bool)(intptr_t)lv_event_get_user_data(e);
    lv_obj_delete_async(dlg); // not inside its own event
    lv_obj_add_flag(dlg, LV_OBJ_FLAG_HIDDEN);
    if (d.cb) {
        d.cb(confirmed, d.ctx);
    }
}

static void dialog_deleted(lv_event_t *e)
{
    lv_free(lv_obj_get_user_data(lv_event_get_target(e)));
}

lv_obj_t *s3w_dialog_show(lv_obj_t *parent, const char *title, const char *body, const char *confirm,
                          bool danger, void (*cb)(bool confirmed, void *ctx), void *ctx)
{
    dialog_t *d = lv_malloc(sizeof *d);
    if (d == NULL) {
        return NULL;
    }
    d->cb = cb;
    d->ctx = ctx;
    lv_obj_t *dlg = plain(parent);
    lv_obj_add_flag(dlg, LV_OBJ_FLAG_CLICKABLE); // block touches to what is below
    lv_obj_add_flag(dlg, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(dlg, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(dlg, ui_color(UI_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(dlg, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(dlg, UI_SAFE_INSET + UI_SPACE_M, 0);
    lv_obj_set_style_pad_row(dlg, UI_SPACE_M, 0);
    lv_obj_set_flex_flow(dlg, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(dlg, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_user_data(dlg, d);
    lv_obj_add_event_cb(dlg, dialog_deleted, LV_EVENT_DELETE, NULL);
    lv_obj_t *t = label(dlg, title, UI_FONT_TITLE, UI_COLOR_TEXT);
    lv_obj_set_width(t, LV_PCT(100));
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    if (body) {
        lv_obj_t *b = label(dlg, body, UI_FONT_BODY, UI_COLOR_TEXT_DIM);
        lv_obj_set_width(b, LV_PCT(100));
        lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, 0);
    }
    lv_obj_t *btns = plain(dlg);
    lv_obj_set_size(btns, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(btns, UI_SPACE_M, 0);
    lv_obj_set_style_pad_row(btns, UI_SPACE_M, 0);
    lv_obj_set_flex_flow(btns, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btns, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *ok = s3w_button_create(btns, danger ? S3W_BUTTON_DANGER : S3W_BUTTON_PRIMARY, confirm ? confirm : "OK");
    lv_obj_set_width(ok, 300);
    lv_obj_add_event_cb(ok, dialog_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)true);
    lv_obj_t *cancel = s3w_button_create(btns, S3W_BUTTON_SECONDARY, "Cancel");
    lv_obj_set_width(cancel, 300);
    lv_obj_add_event_cb(cancel, dialog_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)false);
    return dlg;
}
