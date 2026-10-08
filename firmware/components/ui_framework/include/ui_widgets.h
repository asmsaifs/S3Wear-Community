// Standard widgets (docs/04-ui-ux.md §5). Thin builders over LVGL widgets with the
// S3Wear tokens applied; the returned objects are plain LVGL objects. UI task only.
//
// Toasts are ui_toast_show() (ui_overlay.h). s3w_keyboard_t9 comes later (P2 priority).
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- Lists ----------------------------------------------------------------------------

/** Full-size vertical scroll container inset from the glass corners. Put an
 *  s3w_header first so the title scrolls away with the content. */
lv_obj_t *s3w_list_create(lv_obj_t *parent);

/** Small dim section title inside a list. */
lv_obj_t *s3w_list_add_section(lv_obj_t *list, const char *title);

/** Tappable row: icon (LV_SYMBOL_* or NULL), title, optional subtitle and trailing
 *  value. Listen for LV_EVENT_CLICKED on the returned row. */
lv_obj_t *s3w_list_add_row(lv_obj_t *list, const char *icon, const char *title, const char *subtitle,
                           const char *trailing);

/** Row with a switch; tapping anywhere on the row toggles it. Returns the switch:
 *  listen for LV_EVENT_VALUE_CHANGED, read with lv_obj_has_state(sw, LV_STATE_CHECKED). */
lv_obj_t *s3w_toggle_row(lv_obj_t *list, const char *icon, const char *title, bool on);

/** Row with a title, the value (+ unit, may be NULL) and a slider under them.
 *  Returns the slider: listen for LV_EVENT_VALUE_CHANGED. */
lv_obj_t *s3w_slider_row(lv_obj_t *list, const char *title, int32_t min, int32_t max, int32_t value,
                         const char *unit);

// --- Buttons and containers ---------------------------------------------------------------

typedef enum {
    S3W_BUTTON_PRIMARY,   // accent fill
    S3W_BUTTON_SECONDARY, // surface fill
    S3W_BUTTON_DANGER,    // red fill
} s3w_button_kind_t;

/** Pill button, at least 64 px high. Listen for LV_EVENT_CLICKED. */
lv_obj_t *s3w_button_create(lv_obj_t *parent, s3w_button_kind_t kind, const char *text);

/** Rounded surface card sized to content, full width in a list. */
lv_obj_t *s3w_card_create(lv_obj_t *parent);

/** Title with the clock above it, centred. */
lv_obj_t *s3w_header_create(lv_obj_t *parent, const char *title);

/** Centred icon, title and hint for "nothing here". */
lv_obj_t *s3w_empty_state_create(lv_obj_t *parent, const char *icon, const char *title, const char *hint);

// --- Labels --------------------------------------------------------------------------------

/**
 * Cut the label's text with "..." to fit its box, from now on (and again when its size or style
 * changes). Use this instead of LV_LABEL_LONG_MODE_DOTS, which rewrites the label's text in place
 * while the render thread may be drawing it (an endless loop in lv_draw_label on the watch,
 * P6-01). The box is the label's content size; a width or height of LV_SIZE_CONTENT uses the max
 * width / max height instead (no limit: nothing is cut that way). The label is put in
 * LV_LABEL_LONG_MODE_WRAP. Set later text with s3w_label_set_fit_text(), not lv_label_set_text().
 */
void s3w_label_fit(lv_obj_t *label);

/** New full text for a label cut with "..." (s3w_label_fit() is implied). */
void s3w_label_set_fit_text(lv_obj_t *label, const char *text);

// --- Indicators -------------------------------------------------------------------------

/** Activity-style ring: lv_arc 0..100 with a dim track in the same colour. */
lv_obj_t *s3w_ring_create(lv_obj_t *parent, int32_t diameter, int32_t thickness, uint32_t color_hex);
void s3w_ring_set_value(lv_obj_t *ring, int32_t percent);

/** Page indicator: count dots, the active one wider. */
lv_obj_t *s3w_page_dots_create(lv_obj_t *parent, uint8_t count);
void s3w_page_dots_set_active(lv_obj_t *dots, uint8_t index);

/** QR code on a white quiet zone (scannable on AMOLED). size = code side in px. */
lv_obj_t *s3w_qr_create(lv_obj_t *parent, const char *data, int32_t size);

// --- Input -------------------------------------------------------------------------------

/** Rolling numeric picker min..max by step, two-digit zero padded (time, timer).
 *  Listen for LV_EVENT_VALUE_CHANGED on the returned roller. */
lv_obj_t *s3w_picker_create(lv_obj_t *parent, int32_t min, int32_t max, int32_t step, int32_t value, bool wrap);
int32_t s3w_picker_get_value(lv_obj_t *picker);

/** Confirm dialog covering parent (normally the screen root). cb runs once with
 *  confirmed = true/false, then the dialog deletes itself. */
lv_obj_t *s3w_dialog_show(lv_obj_t *parent, const char *title, const char *body, const char *confirm,
                          bool danger, void (*cb)(bool confirmed, void *ctx), void *ctx);

#ifdef __cplusplus
}
#endif
