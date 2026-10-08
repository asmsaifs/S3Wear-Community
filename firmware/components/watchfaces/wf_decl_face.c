// Declarative faces: a parsed face.json (wf_decl.h) drawn by the engine as a face, and
// the loaders for the embedded samples and installed faces (wf_engine.h).
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_theme.h"
#include "wf_priv.h"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

#define TEXT_W 410 // text elements: full-width label, aligned inside

_Static_assert(WF_DECL_MAX_SLOTS == WF_MAX_SLOTS, "slot limits differ");

typedef struct {
    wf_face_def_t def;
    wf_slot_def_t slots[WF_MAX_SLOTS];
    wf_decl_face_t m;
} decl_face_t;

typedef struct {
    lv_obj_t *obj[WF_DECL_MAX_ELEMENTS]; // per element; NULL if nothing is drawn
} decl_state_t;

static const wf_decl_face_t *model(const wf_face_t *f)
{
    return &((const decl_face_t *)wf_face_def(f)->user)->m;
}

static void elements(const wf_face_t *f, const wf_decl_elem_t **e, uint8_t *n)
{
    const wf_decl_face_t *m = model(f);
    *e = wf_face_aod(f) ? m->aod : m->elems;
    *n = wf_face_aod(f) ? m->aod_n : m->elem_n;
}

static const lv_font_t *font(uint8_t f)
{
    switch (f) {
    case WF_DECL_FONT_DIGITS_160:
        return ui_font_tabular(UI_FONT_DISPLAY_BOLD);
    case WF_DECL_FONT_DIGITS_96:
        return ui_font_tabular(UI_FONT_DISPLAY);
    case WF_DECL_FONT_DIGITS_96_LIGHT:
        return ui_font_tabular(UI_FONT_DISPLAY_LIGHT);
    case WF_DECL_FONT_TITLE:
        return UI_FONT_TITLE;
    case WF_DECL_FONT_CAPTION:
        return UI_FONT_CAPTION;
    default:
        return UI_FONT_BODY;
    }
}

static lv_color_t s_accent; // the face's colour while create() runs (UI task only)

static lv_color_t color(uint32_t c)
{
    return c == WF_DECL_COLOR_ACCENT ? s_accent : ui_color(c);
}

static bool uses_accent(const wf_decl_elem_t *e, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++) {
        if (e[i].color == WF_DECL_COLOR_ACCENT || e[i].track == WF_DECL_COLOR_ACCENT ||
            e[i].major_color == WF_DECL_COLOR_ACCENT) {
            return true;
        }
    }
    return false;
}

static lv_obj_t *plain_obj(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *text_create(lv_obj_t *root, const wf_decl_elem_t *e)
{
    const lv_font_t *fnt = font(e->font);
    lv_obj_t *l = wf_label(root, fnt, UI_COLOR_TEXT);
    lv_obj_set_style_text_color(l, color(e->color), 0);
    lv_obj_set_width(l, TEXT_W);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
    static const lv_text_align_t ALIGN[] = {LV_TEXT_ALIGN_CENTER, LV_TEXT_ALIGN_LEFT, LV_TEXT_ALIGN_RIGHT};
    static const int32_t OFFSET[] = {TEXT_W / 2, 0, TEXT_W};
    lv_obj_set_style_text_align(l, ALIGN[e->align], 0);
    lv_obj_set_pos(l, e->x - OFFSET[e->align], e->y - lv_font_get_line_height(fnt) / 2);
    if (!e->has_bind) {
        char buf[WF_DECL_TEXT_MAX];
        snprintf(buf, sizeof buf, "%s", e->text);
        for (char *p = buf; e->upper && *p; p++) {
            *p = (char)toupper((unsigned char)*p);
        }
        lv_label_set_text(l, buf);
    }
    return l;
}

static lv_obj_t *arc_create(lv_obj_t *root, const wf_decl_elem_t *e)
{
    lv_obj_t *arc = lv_arc_create(root);
    lv_obj_remove_style_all(arc);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(arc, 2 * e->r, 2 * e->r);
    lv_obj_set_pos(arc, e->x - e->r, e->y - e->r);
    lv_arc_set_rotation(arc, e->start);
    lv_arc_set_bg_angles(arc, 0, (lv_value_precise_t)(e->end - e->start));
    lv_arc_set_range(arc, 0, e->max);
    lv_arc_set_value(arc, 0);
    lv_obj_set_style_arc_width(arc, e->w, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, e->w, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, e->rounded, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, e->rounded, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, color(e->color), LV_PART_INDICATOR);
    if (e->track == WF_DECL_COLOR_NONE) {
        lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    } else {
        lv_obj_set_style_arc_color(arc, color(e->track), LV_PART_MAIN);
    }
    return arc;
}

static lv_obj_t *circle_create(lv_obj_t *root, const wf_decl_elem_t *e)
{
    lv_obj_t *o = plain_obj(root);
    lv_obj_set_size(o, 2 * e->r, 2 * e->r);
    lv_obj_set_pos(o, e->x - e->r, e->y - e->r);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    if (e->w == 0) {
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(o, color(e->color), 0);
    } else {
        lv_obj_set_style_border_width(o, e->w, 0);
        lv_obj_set_style_border_color(o, color(e->color), 0);
    }
    return o;
}

static lv_obj_t *rect_create(lv_obj_t *root, const wf_decl_elem_t *e)
{
    lv_obj_t *o = plain_obj(root);
    lv_obj_set_size(o, e->w, e->h);
    lv_obj_set_pos(o, e->x - e->w / 2, e->y - e->h / 2);
    lv_obj_set_style_radius(o, e->r, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, color(e->color), 0);
    return o;
}

/** Tick marks around a circle, as the Analog Classic dial (one lv_scale). */
static lv_obj_t *ticks_create(lv_obj_t *root, const wf_decl_elem_t *e)
{
    lv_obj_t *scale = lv_scale_create(root);
    lv_obj_remove_style_all(scale);
    lv_obj_remove_flag(scale, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(scale, 2 * e->r, 2 * e->r);
    lv_obj_set_pos(scale, e->x - e->r, e->y - e->r);
    lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_total_tick_count(scale, (uint32_t)e->count);
    lv_scale_set_major_tick_every(scale, (uint32_t)e->major); // 0: none
    lv_scale_set_label_show(scale, false);
    lv_scale_set_range(scale, 0, e->count > 1 ? e->count - 1 : 1);
    lv_scale_set_angle_range(scale, (uint32_t)(360 - 360 / e->count)); // last tick not on the first
    lv_scale_set_rotation(scale, 270);                                  // first tick at 12
    lv_obj_set_style_length(scale, e->len, LV_PART_ITEMS);
    lv_obj_set_style_line_width(scale, e->w, LV_PART_ITEMS);
    lv_obj_set_style_line_color(scale, color(e->color), LV_PART_ITEMS);
    lv_obj_set_style_length(scale, e->major_len, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(scale, e->major_w, LV_PART_INDICATOR);
    lv_obj_set_style_line_color(scale, color(e->major_color), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(scale, 0, LV_PART_MAIN);
    return scale;
}

static lv_obj_t *elem_create(lv_obj_t *root, const wf_decl_elem_t *e)
{
    switch (e->type) {
    case WF_DECL_TEXT:
        return text_create(root, e);
    case WF_DECL_ARC:
        return arc_create(root, e);
    case WF_DECL_HAND: {
        // Image hands are not drawn yet: the vector hand stands in.
        lv_obj_t *h = wf_hand_create(root, e->w, UI_COLOR_TEXT);
        if (h) {
            lv_obj_set_style_line_color(h, color(e->color), 0);
            lv_obj_add_flag(h, LV_OBJ_FLAG_HIDDEN); // until update() has an angle
        }
        return h;
    }
    case WF_DECL_CIRCLE:
        return circle_create(root, e);
    case WF_DECL_RECT:
        return rect_create(root, e);
    case WF_DECL_TICKS:
        return ticks_create(root, e);
    default:
        return NULL; // image (not drawn yet); complication (a slot, drawn by the engine)
    }
}

static void create(wf_face_t *f, lv_obj_t *root)
{
    decl_state_t *st = wf_face_state(f);
    const wf_decl_elem_t *e;
    uint8_t n;
    elements(f, &e, &n);
    s_accent = wf_face_accent(f);
    if (!wf_face_aod(f)) {
        // Background images are not drawn yet: the colour (black by default) stays.
        lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(root, ui_color(model(f)->background), 0);
    }
    for (uint8_t i = 0; i < n; i++) {
        st->obj[i] = elem_create(root, &e[i]);
    }
}

static void update(wf_face_t *f, uint32_t changed)
{
    const wf_decl_face_t *m = model(f);
    if (!((wf_face_aod(f) ? m->aod_deps : m->deps) & changed)) {
        return;
    }
    decl_state_t *st = wf_face_state(f);
    const wf_ctx_t *ctx = wf_face_ctx(f);
    const wf_decl_elem_t *e;
    uint8_t n;
    elements(f, &e, &n);
    for (uint8_t i = 0; i < n; i++, e++) {
        lv_obj_t *o = st->obj[i];
        if (o == NULL || !e->has_bind || !(wf_bind_deps(&e->bind) & changed)) {
            continue;
        }
        int32_t v = 0;
        switch (e->type) {
        case WF_DECL_TEXT: {
            char buf[48];
            wf_bind_text(&e->bind, ctx, buf, sizeof buf);
            for (char *p = buf; e->upper && *p; p++) {
                *p = (char)toupper((unsigned char)*p);
            }
            if (strcmp(lv_label_get_text(o), buf) != 0) {
                lv_label_set_text(o, buf);
            }
            break;
        }
        case WF_DECL_ARC:
            if (!wf_bind_value(&e->bind, ctx, &v)) {
                v = 0; // unknown: empty arc
            }
            lv_arc_set_value(o, LV_CLAMP(0, v, e->max));
            break;
        case WF_DECL_HAND:
            if (wf_bind_value(&e->bind, ctx, &v)) {
                wf_hand_set(o, e->x, e->y, v, e->len, e->tail);
                lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); // time unknown: no hands
            }
            break;
        default:
            break;
        }
    }
}

// --- Registration --------------------------------------------------------------------------

/** Zeroed; > 4 KB, so PSRAM on the watch (the model ~6 KB, a face.json buffer 16 KB). */
static void *big_calloc(size_t size)
{
#ifdef ESP_PLATFORM
    return heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM);
#else
    return calloc(1, size);
#endif
}

/** wf_decl_register(); with want_id, the face's id must be that. */
static esp_err_t decl_register(const char *json, size_t len, const char *want_id, wf_decl_err_t *err)
{
    decl_face_t *d = big_calloc(sizeof *d);
    if (d == NULL) {
        if (err) {
            snprintf(err->msg, sizeof err->msg, "out of memory");
        }
        return ESP_ERR_NO_MEM;
    }
    if (!wf_decl_parse(json, len, &d->m, err)) {
        free(d);
        return ESP_ERR_INVALID_ARG;
    }
    const wf_decl_face_t *m = &d->m;
    if (want_id && strcmp(m->id, want_id) != 0) {
        if (err) {
            snprintf(err->msg, sizeof err->msg, "id '%s' differs from its directory name", m->id);
        }
        free(d);
        return ESP_ERR_INVALID_ARG;
    }
    for (uint8_t i = 0; i < m->slot_n; i++) {
        d->slots[i] = (wf_slot_def_t){
            .id = m->slots[i].id,
            .x = m->slots[i].x,
            .y = m->slots[i].y,
            .style = m->slots[i].line ? WF_SLOT_LINE : WF_SLOT_CIRCLE,
            .def = (wf_comp_t)m->slots[i].def,
        };
    }
    d->def = (wf_face_def_t){
        .id = m->id,
        .name = m->name,
        .flags = ((m->deps & WF_DATA_SECOND) ? WF_FACE_SECONDS : 0) |
                 (uses_accent(m->elems, m->elem_n) ? WF_FACE_COLOR : 0),
        .slots = d->slots,
        .slot_count = m->slot_n,
        .state_size = sizeof(decl_state_t),
        .create = create,
        .update = update,
        .user = d,
    };
    const esp_err_t e = wf_register(&d->def);
    if (e != ESP_OK) {
        if (err) {
            memset(err, 0, sizeof *err);
            snprintf(err->msg, sizeof err->msg, e == ESP_ERR_INVALID_STATE ? "id '%s' is already used" : "no room for '%s'",
                     m->id);
        }
        free(d);
    }
    return e;
}

esp_err_t wf_decl_register(const char *json, size_t len, wf_decl_err_t *err)
{
    return decl_register(json, len, NULL, err);
}

void wf_decl_register_samples(void)
{
    for (size_t i = 0; i < wf_sample_count; i++) {
        wf_decl_err_t err;
        if (wf_decl_register(wf_samples[i].json, wf_samples[i].len, &err) != ESP_OK) {
            LV_LOG_ERROR("%s:%d:%d: %s", wf_samples[i].path, err.line, err.col, err.msg);
        }
    }
}

/** Read path (at most WF_DECL_JSON_MAX bytes) and register it; checks the id. */
static esp_err_t load_file(const char *path, const char *dir_name, wf_decl_err_t *err)
{
    memset(err, 0, sizeof *err);
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        snprintf(err->msg, sizeof err->msg, "cannot open");
        return ESP_ERR_NOT_FOUND;
    }
    char *buf = big_calloc(WF_DECL_JSON_MAX + 1);
    if (buf == NULL) {
        fclose(fp);
        snprintf(err->msg, sizeof err->msg, "out of memory");
        return ESP_ERR_NO_MEM;
    }
    const size_t len = fread(buf, 1, WF_DECL_JSON_MAX + 1, fp);
    fclose(fp);
    // wf_decl_parse() rejects more than WF_DECL_JSON_MAX bytes.
    const esp_err_t e = decl_register(buf, len, dir_name, err);
    free(buf);
    return e;
}

int wf_decl_load_dir(const char *dir, wf_decl_report_t report, void *ctx)
{
    DIR *d = opendir(dir);
    if (d == NULL) {
        return 0;
    }
    int n = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        char path[256];
        wf_decl_err_t err;
        esp_err_t e;
        if (snprintf(path, sizeof path, "%s/%s/face.json", dir, ent->d_name) >= (int)sizeof path) {
            memset(&err, 0, sizeof err);
            snprintf(err.msg, sizeof err.msg, "path too long");
            e = ESP_ERR_INVALID_SIZE;
        } else {
            e = load_file(path, ent->d_name, &err);
        }
        n += e == ESP_OK;
        if (report) {
            report(path, e, &err, ctx);
        }
    }
    closedir(d);
    return n;
}
