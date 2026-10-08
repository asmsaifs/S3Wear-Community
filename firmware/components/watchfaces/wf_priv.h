// watchfaces internals: built-in faces and the widgets they share.
#pragma once

#include "wf_engine.h"

#define WF_CENTER_X 205 // 410x502 screen
#define WF_CENTER_Y 251
#define WF_CIRCLE_D 120 // WF_SLOT_CIRCLE diameter
#define WF_LINE_W   330 // WF_SLOT_LINE width
#define WF_LINE_H   64

extern const wf_face_def_t wf_face_digital;
extern const wf_face_def_t wf_face_analog;
extern const wf_face_def_t wf_face_modular;
extern const wf_face_def_t wf_face_minimal;

/** Sample declarative faces, embedded from samples/<id>/face.json by sources.cmake
 *  (generated wf_samples.c). */
typedef struct {
    const char *path; // for error messages
    const char *json;
    size_t len;
} wf_sample_t;
extern const wf_sample_t wf_samples[];
extern const size_t wf_sample_count;

/** Register the samples (wf_init()). A sample that does not load is a build bug:
 *  the host test test_wf_decl parses them. */
void wf_decl_register_samples(void);

/** A static copy of a face (not AOD) under parent, for the picker: built once, not
 *  updated, slots not clickable. NULL if out of memory. */
wf_face_t *wf_preview_create(lv_obj_t *parent, const wf_face_def_t *def);
void wf_preview_delete(wf_face_t *f);

/** Tell the listener (wf_set_listener) about a change the user made. */
void wf_notify(wf_change_t what);

/** Plain label (no click, no style but font and colour). */
lv_obj_t *wf_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color_hex);

/** Complication widget for a slot (clickable container centred on the slot). */
lv_obj_t *wf_comp_widget_create(lv_obj_t *parent, const wf_slot_def_t *slot);
void wf_comp_widget_set(lv_obj_t *widget, const wf_comp_view_t *view);

/** Clock hand: a rounded line from tail (behind the pivot) to the tip. The object
 *  is sized to the hand, so moving it redraws only the old and new hand areas. */
lv_obj_t *wf_hand_create(lv_obj_t *parent, int32_t width, uint32_t color_hex);
/** angle in 0.1° clockwise from 12 o'clock, pivot (cx, cy). */
void wf_hand_set(lv_obj_t *hand, int32_t cx, int32_t cy, int32_t angle_x10, int32_t len, int32_t tail);

/** Filled circle centred at (cx, cy). */
lv_obj_t *wf_dot_create(lv_obj_t *parent, int32_t cx, int32_t cy, int32_t d, uint32_t color_hex);

/** "SAT 3 OCT" style: date pattern, upper-cased. */
void wf_date_upper(const wf_ctx_t *ctx, const char *pattern, char *buf, size_t len);
