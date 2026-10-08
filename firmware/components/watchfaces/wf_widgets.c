// Widgets shared by the built-in faces: complication slots, clock hands, labels.
#include <ctype.h>

#include "ui_theme.h"
#include "ui_widgets.h"
#include "wf_priv.h"

#define CIRCLE_RING 6 // gauge ring width, px

lv_obj_t *wf_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color_hex)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, ui_color(color_hex), 0);
    lv_label_set_text_static(l, "");
    return l;
}

lv_obj_t *wf_dot_create(lv_obj_t *parent, int32_t cx, int32_t cy, int32_t d, uint32_t color_hex)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(o, d, d);
    lv_obj_set_pos(o, cx - d / 2, cy - d / 2);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, ui_color(color_hex), 0);
    return o;
}

void wf_date_upper(const wf_ctx_t *ctx, const char *pattern, char *buf, size_t len)
{
    if (!ctx->time_valid) {
        if (len > 0) {
            buf[0] = '\0';
        }
        return;
    }
    wf_format_date(pattern, &ctx->tm, buf, len);
    for (char *p = buf; *p; p++) {
        *p = (char)toupper((unsigned char)*p);
    }
}

// --- Complication slots ----------------------------------------------------------------
// Circle: children arc, value, caption. Line: children value, detail.

lv_obj_t *wf_comp_widget_create(lv_obj_t *parent, const wf_slot_def_t *slot)
{
    const bool circle = slot->style == WF_SLOT_CIRCLE;
    const int32_t w = circle ? WF_CIRCLE_D : WF_LINE_W;
    const int32_t h = circle ? WF_CIRCLE_D : WF_LINE_H;
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(c, w, h);
    lv_obj_set_pos(c, slot->x - w / 2, slot->y - h / 2);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(c, ui_color(UI_COLOR_SURFACE), 0);
    lv_obj_set_user_data(c, (void *)(uintptr_t)slot->style);

    if (circle) {
        lv_obj_t *arc = lv_arc_create(c);
        lv_obj_remove_style_all(arc);
        lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(arc, w, h);
        lv_obj_center(arc);
        lv_arc_set_rotation(arc, 270);
        lv_arc_set_bg_angles(arc, 0, 360);
        lv_arc_set_range(arc, 0, 1000);
        lv_obj_set_style_arc_width(arc, CIRCLE_RING, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(arc, LV_OPA_30, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, CIRCLE_RING, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);

        lv_obj_t *value = wf_label(c, UI_FONT_TITLE, UI_COLOR_TEXT);
        lv_obj_align(value, LV_ALIGN_CENTER, 0, -10);
        lv_obj_t *caption = wf_label(c, UI_FONT_CAPTION, UI_COLOR_TEXT_DIM);
        lv_obj_align(caption, LV_ALIGN_CENTER, 0, 22);
    } else {
        lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_hor(c, UI_SPACE_L, 0);
        lv_obj_set_style_pad_column(c, UI_SPACE_M, 0);
        wf_label(c, UI_FONT_BODY, UI_COLOR_TEXT);
        lv_obj_t *detail = wf_label(c, UI_FONT_BODY, UI_COLOR_TEXT_DIM);
        lv_obj_set_style_max_width(detail, WF_LINE_W - 2 * UI_SPACE_L - 100, 0);
        s3w_label_fit(detail);
    }
    return c;
}

void wf_comp_widget_set(lv_obj_t *widget, const wf_comp_view_t *v)
{
    const lv_color_t color = ui_color(v->known ? v->color : UI_COLOR_TEXT_DIM);
    if ((wf_slot_style_t)(uintptr_t)lv_obj_get_user_data(widget) == WF_SLOT_CIRCLE) {
        lv_obj_t *arc = lv_obj_get_child(widget, 0);
        lv_obj_set_style_arc_color(arc, color, LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(arc, v->ratio >= 0 ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_INDICATOR);
        lv_arc_set_value(arc, v->ratio >= 0 ? v->ratio : 0);
        lv_label_set_text(lv_obj_get_child(widget, 1), v->value);
        lv_obj_set_style_text_color(lv_obj_get_child(widget, 1), ui_color(v->known ? UI_COLOR_TEXT : UI_COLOR_TEXT_DIM),
                                    0);
        lv_label_set_text(lv_obj_get_child(widget, 2), v->label);
    } else {
        lv_obj_t *value = lv_obj_get_child(widget, 0);
        lv_label_set_text(value, v->value);
        lv_obj_set_style_text_color(value, color, 0);
        s3w_label_set_fit_text(lv_obj_get_child(widget, 1), v->detail[0] ? v->detail : v->label);
    }
}

// --- Hands -----------------------------------------------------------------------------

typedef struct {
    lv_point_precise_t p[2];
} hand_pts_t;

static void hand_deleted(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

lv_obj_t *wf_hand_create(lv_obj_t *parent, int32_t width, uint32_t color_hex)
{
    hand_pts_t *pts = lv_malloc_zeroed(sizeof *pts);
    if (pts == NULL) {
        return NULL;
    }
    lv_obj_t *l = lv_line_create(parent);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_line_width(l, width, 0);
    lv_obj_set_style_line_color(l, ui_color(color_hex), 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_add_event_cb(l, hand_deleted, LV_EVENT_DELETE, pts);
    lv_obj_set_user_data(l, pts);
    lv_line_set_points(l, pts->p, 2);
    return l;
}

void wf_hand_set(lv_obj_t *hand, int32_t cx, int32_t cy, int32_t angle_x10, int32_t len, int32_t tail)
{
    hand_pts_t *pts = lv_obj_get_user_data(hand);
    if (pts == NULL) {
        return;
    }
    const int16_t deg = (int16_t)((angle_x10 / 10) % 360);
    const int32_t s = lv_trigo_sin(deg);      // * 32768
    const int32_t c = lv_trigo_sin(deg + 90); // cos
    const int32_t tip_x = cx + (len * s >> LV_TRIGO_SHIFT);
    const int32_t tip_y = cy - (len * c >> LV_TRIGO_SHIFT);
    const int32_t tail_x = cx - (tail * s >> LV_TRIGO_SHIFT);
    const int32_t tail_y = cy + (tail * c >> LV_TRIGO_SHIFT);
    const int32_t pad = lv_obj_get_style_line_width(hand, 0);
    const int32_t x0 = LV_MIN(tip_x, tail_x) - pad;
    const int32_t y0 = LV_MIN(tip_y, tail_y) - pad;
    pts->p[0] = (lv_point_precise_t){tail_x - x0, tail_y - y0};
    pts->p[1] = (lv_point_precise_t){tip_x - x0, tip_y - y0};
    lv_obj_set_pos(hand, x0, y0);
    lv_obj_set_size(hand, LV_ABS(tip_x - tail_x) + 2 * pad + 1, LV_ABS(tip_y - tail_y) + 2 * pad + 1);
    lv_line_set_points(hand, pts->p, 2); // invalidates
}
