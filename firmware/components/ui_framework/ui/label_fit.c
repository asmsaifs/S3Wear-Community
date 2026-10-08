// Labels cut with "..." to fit their box (ui_widgets.h s3w_label_fit). Replaces LVGL's
// LV_LABEL_LONG_MODE_DOTS: that mode rewrites the label's own text buffer (it puts the
// characters under the dots back, measures, and writes the dots again) and the draw task reads
// that same buffer from the render thread. On the watch a label drawn in more than one band
// could be measured on the full text and walked on the cut one, and lv_draw_label spun forever
// on the '\0' (task watchdog on "swdraw", P6-01). Here the label is in WRAP mode and only ever
// gets a finished string through lv_label_set_text() on the UI task.
#include <string.h>

#include "ui_widgets.h"

#define DOTS "..."

typedef struct {
    char *full;  // the whole text, lv_malloc'd
    char *shown; // what this code last put in the label (NULL: nothing yet)
    bool busy;   // setting the cut text (its own SIZE_CHANGED)
} fit_t;

static void fit_event(lv_event_t *e);

static fit_t *fit_of(lv_obj_t *l)
{
    const uint32_t n = lv_obj_get_event_count(l);
    for (uint32_t i = 0; i < n; i++) {
        lv_event_dsc_t *d = lv_obj_get_event_dsc(l, i);
        if (lv_event_dsc_get_cb(d) == fit_event) {
            return lv_event_dsc_get_user_data(d);
        }
    }
    return NULL;
}

static int32_t resolve(int32_t v, int32_t parent)
{
    return LV_COORD_IS_PCT(v) ? parent * LV_COORD_GET_PCT(v) / 100 : v;
}

// The box the text must fit: the content size, or the max size while sized to content.
// LV_COORD_MAX = no limit that way.
static void box_of(lv_obj_t *l, int32_t *w, int32_t *h)
{
    lv_obj_t *parent = lv_obj_get_parent(l);
    const int32_t pad_w = lv_obj_get_style_pad_left(l, 0) + lv_obj_get_style_pad_right(l, 0);
    const int32_t pad_h = lv_obj_get_style_pad_top(l, 0) + lv_obj_get_style_pad_bottom(l, 0);
    if (lv_obj_get_style_width(l, 0) != LV_SIZE_CONTENT || lv_obj_get_style_flex_grow(l, 0) > 0) {
        *w = lv_obj_get_content_width(l);
    } else {
        const int32_t mw = lv_obj_get_style_max_width(l, 0);
        *w = mw == LV_COORD_MAX ? LV_COORD_MAX : resolve(mw, lv_obj_get_content_width(parent)) - pad_w;
    }
    if (lv_obj_get_style_height(l, 0) != LV_SIZE_CONTENT) {
        *h = lv_obj_get_content_height(l);
    } else {
        const int32_t mh = lv_obj_get_style_max_height(l, 0);
        *h = mh == LV_COORD_MAX ? LV_COORD_MAX : resolve(mh, lv_obj_get_content_height(parent)) - pad_h;
    }
}

static bool fits(const char *s, const lv_font_t *font, int32_t ls, int32_t lsp, int32_t w, int32_t h)
{
    lv_point_t sz;
    lv_text_get_size(&sz, s, font, ls, lsp, w, LV_TEXT_FLAG_NONE);
    return sz.y <= h;
}

static void set_if_changed(lv_obj_t *l, fit_t *f, const char *s)
{
    if (strcmp(lv_label_get_text(l), s) != 0) {
        f->busy = true;
        lv_label_set_text(l, s);
        f->busy = false;
    }
    char *copy = lv_strdup(s);
    if (copy) {
        lv_free(f->shown);
        f->shown = copy;
    }
}

static void adopt(fit_t *f, const char *text)
{
    char *copy = lv_strdup(text);
    if (copy) {
        lv_free(f->full);
        f->full = copy;
    }
}

// The longest prefix (whole code points) that fits with "..." after it; the whole text if it fits.
static void apply(lv_obj_t *l, fit_t *f)
{
    int32_t w, h;
    box_of(l, &w, &h);
    const lv_font_t *font = lv_obj_get_style_text_font(l, 0);
    const int32_t ls = lv_obj_get_style_text_letter_space(l, 0);
    const int32_t lsp = lv_obj_get_style_text_line_space(l, 0);
    if (w <= 0 || h <= 0 || w == LV_COORD_MAX || h == LV_COORD_MAX || fits(f->full, font, ls, lsp, w, h)) {
        set_if_changed(l, f, f->full); // no box yet (layout pending), no limit, or it fits
        return;
    }
    const size_t len = strlen(f->full);
    char *buf = lv_malloc(len + sizeof DOTS);
    if (!buf) {
        set_if_changed(l, f, f->full);
        return;
    }
    // Binary search over byte lengths, snapped back to code point starts.
    size_t lo = 0, hi = len; // lo always fits (an empty prefix gives "..."), hi does not
    while (hi - lo > 1) {
        size_t mid = lo + (hi - lo) / 2;
        while (mid > lo && ((unsigned char)f->full[mid] & 0xC0) == 0x80) {
            mid--;
        }
        if (mid == lo) {
            // Only continuation bytes between lo and the middle: try the next code point start.
            mid = lo + 1;
            while (mid < hi && ((unsigned char)f->full[mid] & 0xC0) == 0x80) {
                mid++;
            }
            if (mid >= hi) {
                break;
            }
        }
        memcpy(buf, f->full, mid);
        memcpy(buf + mid, DOTS, sizeof DOTS);
        if (fits(buf, font, ls, lsp, w, h)) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    while (lo > 0 && f->full[lo - 1] == ' ') {
        lo--; // "word..." not "word ..."
    }
    memcpy(buf, f->full, lo);
    memcpy(buf + lo, DOTS, sizeof DOTS);
    set_if_changed(l, f, buf);
    lv_free(buf);
}

static void fit_event(lv_event_t *e)
{
    lv_obj_t *l = lv_event_get_current_target(e);
    fit_t *f = lv_event_get_user_data(e);
    switch (lv_event_get_code(e)) {
    case LV_EVENT_SIZE_CHANGED:
    case LV_EVENT_STYLE_CHANGED:
        if (!f->busy) {
            // Text set with lv_label_set_text() behind our back becomes the full text.
            if (f->shown && strcmp(lv_label_get_text(l), f->shown) != 0) {
                adopt(f, lv_label_get_text(l));
            }
            apply(l, f);
        }
        break;
    case LV_EVENT_DELETE:
        lv_free(f->full);
        lv_free(f->shown);
        lv_free(f);
        break;
    default:
        break;
    }
}

void s3w_label_set_fit_text(lv_obj_t *label, const char *text)
{
    fit_t *f = fit_of(label);
    if (!f) {
        f = lv_malloc_zeroed(sizeof *f);
        if (!f) {
            lv_label_set_text(label, text ? text : "");
            return;
        }
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP); // CLIP would turn wrapping off
        lv_obj_add_event_cb(label, fit_event, LV_EVENT_ALL, f);
    }
    const char *t = text ? text : "";
    if (!f->full || strcmp(f->full, t) != 0) {
        adopt(f, t);
        if (!f->full) {
            return;
        }
    }
    apply(label, f);
}

void s3w_label_fit(lv_obj_t *label)
{
    fit_t *f = fit_of(label);
    // Already fitted and unchanged: the label holds the cut text, keep the full one.
    const bool keep = f && f->full && f->shown && strcmp(lv_label_get_text(label), f->shown) == 0;
    char *text = lv_strdup(keep ? f->full : lv_label_get_text(label));
    s3w_label_set_fit_text(label, text);
    lv_free(text);
}
