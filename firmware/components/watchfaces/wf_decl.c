// face.json parser (wf_decl.h). Pure C, no allocation.
#include "wf_decl.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "wf_json.h"

#define SCREEN_CX 205 // 410x502 screen
#define SCREEN_CY 251
#define WHITE     0xFFFFFF
#define DIM       0x9A9AA0 // UI_COLOR_TEXT_DIM
#define BIND_MAX  48

typedef enum {
    F_TYPE,
    F_BIND,
    F_TEXT,
    F_X,
    F_Y,
    F_W,
    F_H,
    F_R,
    F_FONT,
    F_COLOR,
    F_ALIGN,
    F_UPPER,
    F_START,
    F_END,
    F_MAX,
    F_TRACK,
    F_ROUNDED,
    F_LEN,
    F_TAIL,
    F_RADIUS,
    F_COUNT,
    F_MAJOR,
    F_MAJOR_LEN,
    F_MAJOR_W,
    F_MAJOR_COLOR,
    F_IMAGE,
    F_PIVOT,
    F_SLOT,
    F_STYLE,
    F_DEFAULT,
    F_N,
} field_t;

static const char *const FIELD[F_N] = {
    "type",  "bind", "text",    "x",   "y",      "w",     "h",         "r",       "font",        "color",
    "align", "upper", "start",  "end", "max",    "track", "rounded",   "len",     "tail",        "radius",
    "count", "major", "major_len", "major_w", "major_color", "image", "pivot", "slot", "style", "default",
};

#define B(f) (1u << (f))

static const struct {
    const char *name;
    uint32_t allowed; // besides "type"
    uint32_t required;
} TYPES[WF_DECL_TYPE_COUNT] = {
    [WF_DECL_TEXT] = {"text", B(F_BIND) | B(F_TEXT) | B(F_X) | B(F_Y) | B(F_FONT) | B(F_COLOR) | B(F_ALIGN) | B(F_UPPER),
                      B(F_X) | B(F_Y)},
    [WF_DECL_ARC] = {"arc",
                     B(F_BIND) | B(F_X) | B(F_Y) | B(F_R) | B(F_W) | B(F_START) | B(F_END) | B(F_MAX) | B(F_COLOR) |
                         B(F_TRACK) | B(F_ROUNDED),
                     B(F_BIND) | B(F_R)},
    [WF_DECL_HAND] = {"hand",
                      B(F_BIND) | B(F_X) | B(F_Y) | B(F_LEN) | B(F_TAIL) | B(F_W) | B(F_COLOR) | B(F_IMAGE) | B(F_PIVOT),
                      B(F_BIND)},
    [WF_DECL_CIRCLE] = {"circle", B(F_X) | B(F_Y) | B(F_R) | B(F_W) | B(F_COLOR), B(F_X) | B(F_Y) | B(F_R)},
    [WF_DECL_RECT] = {"rect", B(F_X) | B(F_Y) | B(F_W) | B(F_H) | B(F_RADIUS) | B(F_COLOR),
                      B(F_X) | B(F_Y) | B(F_W) | B(F_H)},
    [WF_DECL_TICKS] = {"ticks",
                       B(F_X) | B(F_Y) | B(F_R) | B(F_COUNT) | B(F_LEN) | B(F_W) | B(F_COLOR) | B(F_MAJOR) |
                           B(F_MAJOR_LEN) | B(F_MAJOR_W) | B(F_MAJOR_COLOR),
                       B(F_R) | B(F_COUNT)},
    [WF_DECL_IMAGE] = {"image", B(F_IMAGE) | B(F_X) | B(F_Y), B(F_IMAGE) | B(F_X) | B(F_Y)},
    [WF_DECL_COMPLICATION] = {"complication", B(F_SLOT) | B(F_X) | B(F_Y) | B(F_STYLE) | B(F_DEFAULT),
                              B(F_SLOT) | B(F_X) | B(F_Y)},
};

/** Integer field ranges (type-specific minimums are checked after parsing). */
static const struct {
    uint8_t field;
    int32_t lo, hi;
} RANGES[] = {
    {F_X, -205, 615},     {F_Y, -251, 753},  {F_W, 0, 502},     {F_H, 1, 502},       {F_R, 1, 300},
    {F_START, -360, 720}, {F_END, -360, 720}, {F_MAX, 1, 1000000}, {F_LEN, 1, 300},  {F_TAIL, 0, 200},
    {F_RADIUS, 0, 300},   {F_COUNT, 1, 120}, {F_MAJOR, 0, 120}, {F_MAJOR_LEN, 1, 300}, {F_MAJOR_W, 1, 100},
};

static const char *const FONTS[WF_DECL_FONT_COUNT] = {
    [WF_DECL_FONT_DIGITS_160] = "digits_160", [WF_DECL_FONT_DIGITS_96] = "digits_96",
    [WF_DECL_FONT_DIGITS_96_LIGHT] = "digits_96_light", [WF_DECL_FONT_TITLE] = "title_32",
    [WF_DECL_FONT_BODY] = "body_26", [WF_DECL_FONT_CAPTION] = "caption_22",
};

typedef struct {
    wf_json_t j;
    wf_decl_face_t *f;
    wf_decl_err_t *err;
    bool msg_set;
    const char *value; // start of the value being read: semantic errors point here
} parser_t;

/** Semantic error: message with the element path; stops the walk. Returns false. */
static bool bad(parser_t *p, const char *fmt, ...)
{
    if (!p->msg_set) {
        p->msg_set = true;
        if (p->err) {
            va_list ap;
            va_start(ap, fmt);
            vsnprintf(p->err->msg, sizeof p->err->msg, fmt, ap);
            va_end(ap);
        }
        if (p->value && wf_json_ok(&p->j)) {
            p->j.p = p->value;
        }
    }
    return wf_json_fail(&p->j, "invalid");
}

wf_decl_font_t wf_decl_font_find(const char *name)
{
    for (int i = 0; i < WF_DECL_FONT_COUNT; i++) {
        if (strcmp(name, FONTS[i]) == 0) {
            return (wf_decl_font_t)i;
        }
    }
    return WF_DECL_FONT_COUNT;
}

static bool valid_chars(const char *s, const char *extra)
{
    for (; *s; s++) {
        const char c = *s;
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || strchr(extra, c))) {
            return false;
        }
    }
    return true;
}

/** Asset file in the face directory: [A-Za-z0-9._-], no leading '.', .png or .bin. */
static bool valid_asset(const char *s)
{
    const size_t n = strlen(s);
    if (n < 5 || s[0] == '.') {
        return false;
    }
    for (const char *c = s; *c; c++) {
        if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '.' ||
              *c == '_' || *c == '-')) {
            return false;
        }
    }
    return strcmp(s + n - 4, ".png") == 0 || strcmp(s + n - 4, ".bin") == 0;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/** "#RRGGBB" -> 0xRRGGBB. */
static bool parse_hex_color(const char *s, uint32_t *out)
{
    if (s[0] != '#' || strlen(s) != 7) {
        return false;
    }
    uint32_t v = 0;
    for (int i = 1; i < 7; i++) {
        const int d = hex_digit(s[i]);
        if (d < 0) {
            return false;
        }
        v = v << 4 | (uint32_t)d;
    }
    *out = v;
    return true;
}

static bool get_color(parser_t *p, const char *path, const char *key, bool allow_none, uint32_t *out)
{
    char s[16];
    if (!wf_json_string(&p->j, s, sizeof s)) {
        return false;
    }
    if (strcmp(s, "accent") == 0) {
        *out = WF_DECL_COLOR_ACCENT;
        return true;
    }
    if (allow_none && strcmp(s, "none") == 0) {
        *out = WF_DECL_COLOR_NONE;
        return true;
    }
    if (parse_hex_color(s, out)) {
        return true;
    }
    return bad(p, "%s.%s: '%s' is not a colour (\"#RRGGBB\"%s or \"accent\")", path, key, s,
               allow_none ? ", \"none\"" : "");
}

static bool get_int(parser_t *p, const char *path, field_t field, int32_t *out)
{
    if (!wf_json_int(&p->j, out)) {
        return false;
    }
    for (size_t i = 0; i < sizeof RANGES / sizeof RANGES[0]; i++) {
        if (RANGES[i].field == field && (*out < RANGES[i].lo || *out > RANGES[i].hi)) {
            return bad(p, "%s.%s: %ld is out of range (%ld..%ld)", path, FIELD[field], (long)*out, (long)RANGES[i].lo,
                       (long)RANGES[i].hi);
        }
    }
    return true;
}

static field_t field_find(const char *key)
{
    for (int i = 0; i < F_N; i++) {
        if (strcmp(key, FIELD[i]) == 0) {
            return (field_t)i;
        }
    }
    return F_N;
}

static void set_defaults(wf_decl_elem_t *e, wf_decl_type_t type)
{
    memset(e, 0, sizeof *e);
    e->type = (uint8_t)type;
    e->x = SCREEN_CX;
    e->y = SCREEN_CY;
    e->color = WHITE;
    e->font = WF_DECL_FONT_BODY;
    e->align = WF_DECL_ALIGN_CENTER;
    e->track = WF_DECL_COLOR_NONE;
    switch (type) {
    case WF_DECL_ARC:
        e->w = 8;
        e->start = 135;
        e->end = 405;
        e->max = 1000;
        e->color = WF_DECL_COLOR_ACCENT;
        e->rounded = true;
        break;
    case WF_DECL_HAND:
        e->len = 150;
        e->w = 4;
        break;
    case WF_DECL_TICKS:
        e->len = 10;
        e->w = 2;
        e->color = DIM;
        e->major_color = WHITE;
        break;
    default:
        break;
    }
}

/** One field of element e (the key is read; the value is next). */
static bool parse_field(parser_t *p, const char *path, wf_decl_elem_t *e, field_t field)
{
    wf_json_t *j = &p->j;
    int32_t v = 0;
    switch (field) {
    case F_BIND: {
        char name[BIND_MAX];
        if (!wf_json_string(j, name, sizeof name)) {
            return false;
        }
        if (!wf_bind_parse(name, &e->bind)) {
            return bad(p, "%s.bind: unknown binding '%s'", path, name);
        }
        if (e->type == WF_DECL_TEXT && !wf_bind_has_text(&e->bind)) {
            return bad(p, "%s.bind: '%s' has no text form", path, name);
        }
        if (e->type != WF_DECL_TEXT && !wf_bind_has_value(&e->bind)) {
            return bad(p, "%s.bind: '%s' has no numeric form", path, name);
        }
        e->has_bind = true;
        return true;
    }
    case F_TEXT:
        return wf_json_string(j, e->text, sizeof e->text);
    case F_FONT: {
        char name[24];
        if (!wf_json_string(j, name, sizeof name)) {
            return false;
        }
        const wf_decl_font_t font = wf_decl_font_find(name);
        if (font == WF_DECL_FONT_COUNT) {
            return bad(p, "%s.font: unknown font '%s'", path, name);
        }
        e->font = (uint8_t)font;
        return true;
    }
    case F_COLOR:
        return get_color(p, path, "color", false, &e->color);
    case F_TRACK:
        return get_color(p, path, "track", true, &e->track);
    case F_MAJOR_COLOR:
        return get_color(p, path, "major_color", false, &e->major_color);
    case F_ALIGN: {
        char s[8];
        if (!wf_json_string(j, s, sizeof s)) {
            return false;
        }
        static const char *const ALIGN[] = {"center", "left", "right"};
        for (uint8_t i = 0; i < 3; i++) {
            if (strcmp(s, ALIGN[i]) == 0) {
                e->align = i;
                return true;
            }
        }
        return bad(p, "%s.align: '%s' is not center, left or right", path, s);
    }
    case F_UPPER:
        return wf_json_bool(j, &e->upper);
    case F_ROUNDED:
        return wf_json_bool(j, &e->rounded);
    case F_IMAGE:
        if (!wf_json_string(j, e->image, sizeof e->image)) {
            return false;
        }
        return valid_asset(e->image) ? true : bad(p, "%s.image: '%s' is not a .png or .bin file name", path, e->image);
    case F_PIVOT: {
        int32_t xy[2];
        int n = 0;
        if (!wf_json_arr_begin(j)) {
            return false;
        }
        while (wf_json_arr_next(j)) {
            if (n == 2 || !wf_json_int(j, &xy[n]) || xy[n] < 0 || xy[n] > 1000) {
                return wf_json_ok(j) ? bad(p, "%s.pivot: expected [x, y], 0..1000", path) : false;
            }
            n++;
        }
        if (!wf_json_ok(j)) {
            return false;
        }
        if (n != 2) {
            return bad(p, "%s.pivot: expected [x, y], 0..1000", path);
        }
        e->pivot_x = (int16_t)xy[0];
        e->pivot_y = (int16_t)xy[1];
        return true;
    }
    case F_SLOT: {
        char id[WF_DECL_SLOT_ID_MAX];
        if (!wf_json_string(j, id, sizeof id)) {
            return false;
        }
        if (id[0] == '\0' || !valid_chars(id, "_-")) {
            return bad(p, "%s.slot: '%s' is not a slot id ([a-z0-9_-])", path, id);
        }
        wf_decl_face_t *f = p->f;
        for (uint8_t i = 0; i < f->slot_n; i++) {
            if (strcmp(f->slots[i].id, id) == 0) {
                return bad(p, "%s.slot: '%s' is used twice", path, id);
            }
        }
        if (f->slot_n >= WF_DECL_MAX_SLOTS) {
            return bad(p, "%s.slot: more than %d complications", path, WF_DECL_MAX_SLOTS);
        }
        e->slot = f->slot_n++;
        strcpy(f->slots[e->slot].id, id);
        return true;
    }
    case F_STYLE:
    case F_DEFAULT: {
        char s[24];
        if (!wf_json_string(j, s, sizeof s)) {
            return false;
        }
        if (field == F_STYLE) {
            if (strcmp(s, "circle") != 0 && strcmp(s, "line") != 0) {
                return bad(p, "%s.style: '%s' is not circle or line", path, s);
            }
            e->line = strcmp(s, "line") == 0;
            return true;
        }
        const wf_comp_t c = wf_comp_find(s);
        if (c == WF_COMP_COUNT) {
            return bad(p, "%s.default: unknown complication '%s'", path, s);
        }
        e->comp = (uint8_t)c;
        return true;
    }
    default:
        break;
    }
    if (!get_int(p, path, field, &v)) {
        return false;
    }
    switch (field) {
    case F_X: e->x = (int16_t)v; break;
    case F_Y: e->y = (int16_t)v; break;
    case F_W: e->w = (int16_t)v; break;
    case F_H: e->h = (int16_t)v; break;
    case F_R: e->r = (int16_t)v; break;
    case F_RADIUS: e->r = (int16_t)v; break;
    case F_START: e->start = (int16_t)v; break;
    case F_END: e->end = (int16_t)v; break;
    case F_MAX: e->max = v; break;
    case F_LEN: e->len = (int16_t)v; break;
    case F_TAIL: e->tail = (int16_t)v; break;
    case F_COUNT: e->count = (int16_t)v; break;
    case F_MAJOR: e->major = (int16_t)v; break;
    case F_MAJOR_LEN: e->major_len = (int16_t)v; break;
    case F_MAJOR_W: e->major_w = (int16_t)v; break;
    default: break;
    }
    return true;
}

/** Checks that need all fields of e. */
static bool finish_elem(parser_t *p, const char *path, wf_decl_elem_t *e, uint32_t seen)
{
    const wf_decl_type_t type = (wf_decl_type_t)e->type;
    const uint32_t missing = TYPES[type].required & ~seen;
    if (missing) {
        for (int i = 0; i < F_N; i++) {
            if (missing & B(i)) {
                return bad(p, "%s: %s needs \"%s\"", path, TYPES[type].name, FIELD[i]);
            }
        }
    }
    // "w" is a line width (arc, hand, ticks) or the width (rect): at least 1 px.
    if ((type == WF_DECL_ARC || type == WF_DECL_HAND || type == WF_DECL_TICKS || type == WF_DECL_RECT) && e->w < 1) {
        return bad(p, "%s.w: must be at least 1", path);
    }
    switch (type) {
    case WF_DECL_TEXT:
        if (!(seen & (B(F_BIND) | B(F_TEXT)))) {
            return bad(p, "%s: text needs \"bind\" or \"text\"", path);
        }
        if ((seen & B(F_BIND)) && (seen & B(F_TEXT))) {
            return bad(p, "%s: text has both \"bind\" and \"text\"", path);
        }
        break;
    case WF_DECL_ARC:
        if (e->end - e->start < 1 || e->end - e->start > 360) {
            return bad(p, "%s: arc end - start must be 1..360 degrees", path);
        }
        break;
    case WF_DECL_TICKS:
        if (e->major > e->count) {
            return bad(p, "%s.major: larger than count", path);
        }
        if (!(seen & B(F_MAJOR_LEN))) {
            e->major_len = (int16_t)(e->len * 2);
        }
        if (!(seen & B(F_MAJOR_W))) {
            e->major_w = (int16_t)(e->w * 2);
        }
        break;
    case WF_DECL_COMPLICATION: {
        wf_decl_slot_t *s = &p->f->slots[e->slot];
        s->x = e->x;
        s->y = e->y;
        s->line = e->line;
        s->def = e->comp;
        break;
    }
    default:
        break;
    }
    return true;
}

static bool parse_elem(parser_t *p, const char *path, wf_decl_elem_t *e, bool aod)
{
    wf_json_t *j = &p->j;
    p->value = NULL; // errors before the first field: where the walk stopped
    const wf_json_t start = *j;
    char key[24];

    // Pass 1: the type (members may come in any order).
    char type_name[16] = "";
    bool has_type = false;
    if (!wf_json_obj_begin(j)) {
        return wf_json_ok(j) ? false : bad(p, "%s: %s", path, wf_json_error(j));
    }
    while (wf_json_obj_next(j, key, sizeof key)) {
        if (strcmp(key, "type") == 0 && !has_type) {
            has_type = true;
            if (!wf_json_string(j, type_name, sizeof type_name)) {
                return bad(p, "%s.type: %s", path, wf_json_error(j));
            }
        } else if (!wf_json_skip(j)) {
            break;
        }
    }
    if (!wf_json_ok(j)) {
        return bad(p, "%s: %s", path, wf_json_error(j));
    }
    if (!has_type) {
        return bad(p, "%s: missing \"type\"", path);
    }
    wf_decl_type_t type = WF_DECL_TYPE_COUNT;
    for (int i = 0; i < WF_DECL_TYPE_COUNT; i++) {
        if (strcmp(type_name, TYPES[i].name) == 0) {
            type = (wf_decl_type_t)i;
        }
    }
    if (type == WF_DECL_TYPE_COUNT) {
        return bad(p, "%s.type: unknown type '%s'", path, type_name);
    }
    if (aod && type == WF_DECL_COMPLICATION) {
        return bad(p, "%s: complications are not drawn in AOD", path);
    }

    // Pass 2: the fields.
    *j = start;
    set_defaults(e, type);
    uint32_t seen = 0;
    wf_json_obj_begin(j);
    while (wf_json_obj_next(j, key, sizeof key)) {
        const field_t f = field_find(key);
        if (f == F_N || (f != F_TYPE && !(TYPES[type].allowed & B(f)))) {
            return bad(p, "%s.%s: not a field of %s", path, key, TYPES[type].name);
        }
        if (seen & B(f)) {
            return bad(p, "%s.%s: duplicate key", path, key);
        }
        seen |= B(f);
        wf_json_peek(j);
        p->value = j->p;
        if (f == F_TYPE) {
            if (!wf_json_skip(j)) {
                return false;
            }
            continue;
        }
        if (!parse_field(p, path, e, f)) {
            return bad(p, "%s.%s: %s", path, key, wf_json_error(j)); // no-op if bad() already ran
        }
    }
    if (!wf_json_ok(j)) {
        return bad(p, "%s: %s", path, wf_json_error(j));
    }
    p->value = NULL; // element-level checks: the end of the element
    return finish_elem(p, path, e, seen);
}

static bool parse_list(parser_t *p, const char *name, wf_decl_elem_t *elems, uint8_t max, uint8_t *count, bool aod)
{
    wf_json_t *j = &p->j;
    if (!wf_json_arr_begin(j)) {
        return bad(p, "%s: %s", name, wf_json_error(j));
    }
    uint8_t n = 0;
    while (wf_json_arr_next(j)) {
        if (n == max) {
            return bad(p, "%s: more than %d elements", name, max);
        }
        char path[32];
        snprintf(path, sizeof path, "%s[%u]", name, n);
        if (!parse_elem(p, path, &elems[n], aod)) {
            return false;
        }
        n++;
    }
    *count = n;
    return wf_json_ok(j) ? true : bad(p, "%s: %s", name, wf_json_error(j));
}

static bool parse_aod(parser_t *p)
{
    wf_json_t *j = &p->j;
    char key[24];
    bool seen = false;
    if (!wf_json_obj_begin(j)) {
        return bad(p, "aod: %s", wf_json_error(j));
    }
    while (wf_json_obj_next(j, key, sizeof key)) {
        if (strcmp(key, "elements") != 0) {
            return bad(p, "aod.%s: unknown key", key);
        }
        if (seen) {
            return bad(p, "aod.elements: duplicate key");
        }
        seen = true;
        if (!parse_list(p, "aod.elements", p->f->aod, WF_DECL_MAX_AOD_ELEMENTS, &p->f->aod_n, true)) {
            return false;
        }
    }
    return wf_json_ok(j) ? true : bad(p, "aod: %s", wf_json_error(j));
}

typedef enum { T_ID, T_NAME, T_VERSION, T_API, T_AUTHOR, T_DESCRIPTION, T_BACKGROUND, T_ELEMENTS, T_AOD, T_N } top_t;

static const char *const TOP[T_N] = {"id",     "name",       "version",  "api", "author",
                                     "description", "background", "elements", "aod"};

static bool parse_top(parser_t *p, top_t t)
{
    wf_json_t *j = &p->j;
    wf_decl_face_t *f = p->f;
    switch (t) {
    case T_ID:
        if (!wf_json_string(j, f->id, sizeof f->id)) {
            return false;
        }
        if (f->id[0] == '\0' || !valid_chars(f->id, "._-")) {
            return bad(p, "id: '%s' must be 1..%d of [a-z0-9._-]", f->id, WF_DECL_ID_MAX - 1);
        }
        return true;
    case T_NAME:
        if (!wf_json_string(j, f->name, sizeof f->name)) {
            return false;
        }
        return f->name[0] ? true : bad(p, "name: empty");
    case T_VERSION:
        return wf_json_string(j, f->version, sizeof f->version);
    case T_API: {
        int32_t api;
        if (!wf_json_int(j, &api)) {
            return false;
        }
        return api == WF_DECL_API ? true : bad(p, "api: %ld is not supported (this watch: %d)", (long)api, WF_DECL_API);
    }
    case T_AUTHOR:
    case T_DESCRIPTION:
        if (wf_json_peek(j) != WF_JSON_STRING) {
            return bad(p, "%s: expected a string", TOP[t]);
        }
        return wf_json_skip(j);
    case T_BACKGROUND: {
        char s[WF_DECL_ASSET_MAX];
        if (!wf_json_string(j, s, sizeof s)) {
            return false;
        }
        if (s[0] == '#') {
            return parse_hex_color(s, &f->background) ? true : bad(p, "background: '%s' is not \"#RRGGBB\"", s);
        }
        if (!valid_asset(s)) {
            return bad(p, "background: '%s' is neither \"#RRGGBB\" nor a .png or .bin file name", s);
        }
        strcpy(f->background_image, s);
        return true;
    }
    case T_ELEMENTS:
        return parse_list(p, "elements", f->elems, WF_DECL_MAX_ELEMENTS, &f->elem_n, false);
    case T_AOD:
        return parse_aod(p);
    default:
        return false;
    }
}

static uint32_t list_deps(const wf_decl_elem_t *e, uint8_t n)
{
    uint32_t deps = 0;
    for (uint8_t i = 0; i < n; i++) {
        if (e[i].has_bind) {
            deps |= wf_bind_deps(&e[i].bind);
        }
    }
    return deps;
}

bool wf_decl_parse(const char *json, size_t len, wf_decl_face_t *out, wf_decl_err_t *err)
{
    parser_t p = {.f = out, .err = err};
    memset(out, 0, sizeof *out);
    if (err) {
        memset(err, 0, sizeof *err);
    }
    wf_json_init(&p.j, json ? json : "", json ? len : 0);
    if (len > WF_DECL_JSON_MAX) {
        bad(&p, "face.json is larger than %d bytes", WF_DECL_JSON_MAX);
    }

    uint32_t seen = 0;
    char key[24];
    if (wf_json_obj_begin(&p.j)) {
        while (wf_json_obj_next(&p.j, key, sizeof key)) {
            top_t t = T_N;
            for (int i = 0; i < T_N; i++) {
                if (strcmp(key, TOP[i]) == 0) {
                    t = (top_t)i;
                }
            }
            if (t == T_N) {
                bad(&p, "%s: unknown key", key);
                break;
            }
            if (seen & B(t)) {
                bad(&p, "%s: duplicate key", key);
                break;
            }
            seen |= B(t);
            wf_json_peek(&p.j);
            p.value = p.j.p;
            if (!parse_top(&p, t)) {
                bad(&p, "%s: %s", key, wf_json_error(&p.j)); // no-op if bad() already ran
                break;
            }
        }
    }
    if (wf_json_finish(&p.j)) {
        static const top_t REQUIRED[] = {T_ID, T_NAME, T_API, T_ELEMENTS};
        for (size_t i = 0; i < sizeof REQUIRED / sizeof REQUIRED[0]; i++) {
            if (!(seen & B(REQUIRED[i]))) {
                bad(&p, "missing \"%s\"", TOP[REQUIRED[i]]);
                break;
            }
        }
        if (!p.msg_set && out->elem_n == 0) {
            bad(&p, "elements: empty");
        }
    }

    if (!wf_json_ok(&p.j)) {
        if (err) {
            wf_json_line_col(&p.j, wf_json_error_pos(&p.j), &err->line, &err->col);
            if (!p.msg_set) { // syntax error outside any member
                snprintf(err->msg, sizeof err->msg, "%s", wf_json_error(&p.j));
            }
        }
        return false;
    }

    if (!(seen & B(T_AOD))) {
        // Default AOD variant: the time, light and dim (docs/03 F1: < 10 % lit).
        wf_decl_elem_t *e = &out->aod[0];
        set_defaults(e, WF_DECL_TEXT);
        wf_bind_parse("time.hh:mm", &e->bind);
        e->has_bind = true;
        e->font = WF_DECL_FONT_DIGITS_96_LIGHT;
        e->color = DIM;
        out->aod_n = 1;
    }
    out->deps = list_deps(out->elems, out->elem_n);
    out->aod_deps = list_deps(out->aod, out->aod_n);
    return true;
}
