// Declarative watch faces (docs/03-firmware-features.md F1, docs/04-ui-ux.md §4a):
// face.json parsed into a wf_decl_face_t model. Pure C, no LVGL, no allocation; the
// renderer (wf_engine.h, wf_decl_register()) turns a model into a face. Faces come
// from the phone, so the parser trusts nothing: unknown keys, wrong types, values out
// of range, unknown bindings, fonts or complications and duplicate keys are errors.
//
// Format (api 1). Every element is centred on (x, y) on the 410x502 screen; a text's
// x is its left/centre/right edge per "align" and its y the line's centre.
//
//   {"id": "s3w.neon", "name": "Neon", "version": "1.0.0", "api": 1,
//    "author": "...", "description": "...",          optional, not shown yet
//    "background": "#000000",                        colour, or an image (not drawn yet)
//    "elements": [ ... ],                            1..WF_DECL_MAX_ELEMENTS
//    "aod": {"elements": [ ... ]}}                   optional; black background,
//                                                    0..WF_DECL_MAX_AOD_ELEMENTS
//
// | type         | fields (* required)                                                   |
// |--------------|-----------------------------------------------------------------------|
// | text         | bind* or text*, x*, y*, font (body_26), color (#FFFFFF),               |
// |              | align (center | left | right), upper (false)                          |
// | arc          | bind* (numeric), r*, x, y (centre), w (8), start (135), end (405),     |
// |              | max (1000), color (accent), track (none), rounded (true)               |
// | hand         | bind* (numeric, 0.1° clockwise from 12), x, y (pivot, centre),         |
// |              | len (150), tail (0), w (4), color (#FFFFFF), image, pivot              |
// | circle       | x*, y*, r*, w (0 = filled, else ring width), color (#FFFFFF)           |
// | rect         | x*, y*, w*, h*, radius (0), color (#FFFFFF)                            |
// | ticks        | r* (outer), count*, x, y (centre), len (10), w (2), color (#9A9AA0),   |
// |              | major (every n-th tick, 0 = none), major_len, major_w, major_color    |
// | image        | image*, x*, y*  (not drawn yet)                                        |
// | complication | slot* (id), x*, y*, style (circle | line), default (none)             |
//
// Colours: "#RRGGBB" or "accent" (the user's accent colour). Fonts: digits_160,
// digits_96, digits_96_light (digits and ": . , - + %" only), title_32, body_26,
// caption_22. Bindings: wf_bind.h. Complication ids: wf_comp.h. Arc angles are
// degrees clockwise from 3 o'clock (LVGL), end - start in 1..360; the arc fills
// value / max of the way. Images ("background" file, "image", hand "image" +
// "pivot") are checked but not drawn yet: hands fall back to the vector hand, the
// background to black. Complications are not allowed in "aod". Without "aod" the AOD
// variant is the time in digits_96_light, dim grey, centred.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wf_bind.h"
#include "wf_comp.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WF_DECL_API              1
#define WF_DECL_JSON_MAX         16384 // bytes of face.json
#define WF_DECL_MAX_ELEMENTS     32
#define WF_DECL_MAX_AOD_ELEMENTS 8
#define WF_DECL_MAX_SLOTS        4  // = WF_MAX_SLOTS
#define WF_DECL_ID_MAX           48 // WATCH_FACE setting: 47 characters
#define WF_DECL_NAME_MAX         32
#define WF_DECL_TEXT_MAX         32
#define WF_DECL_ASSET_MAX        32
#define WF_DECL_SLOT_ID_MAX      16

#define WF_DECL_COLOR_ACCENT 0x01000000u // the theme accent
#define WF_DECL_COLOR_NONE   0x02000000u // arc track: none

typedef enum {
    WF_DECL_TEXT,
    WF_DECL_ARC,
    WF_DECL_HAND,
    WF_DECL_CIRCLE,
    WF_DECL_RECT,
    WF_DECL_TICKS,
    WF_DECL_IMAGE,
    WF_DECL_COMPLICATION,
    WF_DECL_TYPE_COUNT,
} wf_decl_type_t;

typedef enum {
    WF_DECL_FONT_DIGITS_160,
    WF_DECL_FONT_DIGITS_96,
    WF_DECL_FONT_DIGITS_96_LIGHT,
    WF_DECL_FONT_TITLE,
    WF_DECL_FONT_BODY,
    WF_DECL_FONT_CAPTION,
    WF_DECL_FONT_COUNT,
} wf_decl_font_t;

typedef enum {
    WF_DECL_ALIGN_CENTER,
    WF_DECL_ALIGN_LEFT,
    WF_DECL_ALIGN_RIGHT,
} wf_decl_align_t;

typedef struct {
    uint8_t type;  // wf_decl_type_t
    uint8_t font;  // wf_decl_font_t (text)
    uint8_t align; // wf_decl_align_t (text)
    uint8_t slot;  // complication: index into wf_decl_face_t.slots
    bool upper;    // text: upper-case
    bool has_bind;
    bool rounded;  // arc ends
    bool line;     // complication style "line"
    uint8_t comp;  // complication default (wf_comp_t)
    int16_t x, y;
    int16_t w, h;  // rect size; line width of arc, hand, ticks; circle ring width
    int16_t r;     // arc, circle, ticks radius; rect corner radius
    int16_t len, tail;
    int16_t start, end;
    int16_t count, major;
    int16_t major_len, major_w;
    int16_t pivot_x, pivot_y; // hand image
    int32_t max;
    uint32_t color, track, major_color; // 0xRRGGBB or WF_DECL_COLOR_*
    wf_bind_t bind;
    char text[WF_DECL_TEXT_MAX];   // text without bind
    char image[WF_DECL_ASSET_MAX]; // image, hand image
} wf_decl_elem_t;

typedef struct {
    char id[WF_DECL_SLOT_ID_MAX];
    int16_t x, y;
    bool line; // style "line" (else circle)
    uint8_t def; // wf_comp_t
} wf_decl_slot_t;

typedef struct {
    char id[WF_DECL_ID_MAX];
    char name[WF_DECL_NAME_MAX];
    char version[16];
    uint32_t background; // 0xRRGGBB
    char background_image[WF_DECL_ASSET_MAX];
    uint8_t elem_n, aod_n, slot_n;
    uint32_t deps, aod_deps; // wf_data_mask_t of the bindings
    wf_decl_slot_t slots[WF_DECL_MAX_SLOTS];
    wf_decl_elem_t elems[WF_DECL_MAX_ELEMENTS];
    wf_decl_elem_t aod[WF_DECL_MAX_AOD_ELEMENTS];
} wf_decl_face_t;

typedef struct {
    int line, col; // where the parser stopped in face.json (1-based)
    char msg[128]; // "elements[2].font: unknown font 'huge'", "elements: expected ',' or ']'"
} wf_decl_err_t;

/** Parse face.json (at most WF_DECL_JSON_MAX bytes). On failure out is undefined and
 *  err (may be NULL) says why. */
bool wf_decl_parse(const char *json, size_t len, wf_decl_face_t *out, wf_decl_err_t *err);

/** Font name -> wf_decl_font_t; WF_DECL_FONT_COUNT if unknown. */
wf_decl_font_t wf_decl_font_find(const char *name);

#ifdef __cplusplus
}
#endif
