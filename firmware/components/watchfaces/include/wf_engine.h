// Watch face engine (docs/03-firmware-features.md F1, docs/04-ui-ux.md §3): the home
// screen hosts the active face, feeds it time and data, draws its complication slots
// and switches it to its AOD variant. UI task only (LVGL), like ui_framework.
//
// A native face is a wf_face_def_t: create() builds the face (or its AOD variant) under
// a full-screen root; update() refreshes it after data changed. The engine adds the
// complication widgets for the face's slots (not in AOD), keeps them current and opens
// the complication's app on tap. Faces never poll: updates come from the minute clock
// (ui_clock listener), a 1 s timer only for WF_FACE_SECONDS faces while the screen is
// on and not in AOD, and wf_data_changed().
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"
#include "ui_nav.h"
#include "wf_bind.h"
#include "wf_comp.h"
#include "wf_data.h"
#include "wf_decl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WF_MAX_FACES 16
#define WF_MAX_SLOTS 4

typedef struct wf_face wf_face_t;

typedef enum {
    WF_SLOT_CIRCLE, // 112 px round gauge: value, caption, ring
    WF_SLOT_LINE,   // one line up to 330 px wide: value and detail
} wf_slot_style_t;

typedef struct {
    const char *id;        // stable name: FaceConfig, face.json "slot", console
    int16_t x, y;          // centre on the 410x502 screen
    wf_slot_style_t style;
    wf_comp_t def;         // default complication (WF_COMP_NONE = empty)
} wf_slot_def_t;

enum {
    WF_FACE_SECONDS = 1u << 0, // update() gets WF_DATA_SECOND once a second (screen on, not AOD)
    WF_FACE_COLOR = 1u << 1,   // draws with wf_face_accent(): customize offers a colour
};

typedef struct {
    const char *id;   // stable: the WATCH_FACE setting, FaceConfig
    const char *name; // shown in the picker
    uint32_t flags;
    const wf_slot_def_t *slots;
    uint8_t slot_count; // <= WF_MAX_SLOTS
    size_t state_size;  // zeroed per build, wf_face_state()
    /** Build under root (full screen, black): the AOD variant if wf_face_aod(f). AOD
     *  variants keep under 10 % of the pixels lit: thin digits, no seconds, no fills. */
    void (*create)(wf_face_t *f, lv_obj_t *root);
    /** Refresh after the wf_data_mask_t bits in changed (WF_DATA_ALL right after
     *  create). Read time and data with wf_face_ctx(). */
    void (*update)(wf_face_t *f, uint32_t changed);
    const void *user; // for create/update (declarative faces: their model)
} wf_face_def_t;

const wf_face_def_t *wf_face_def(const wf_face_t *f);
bool wf_face_aod(const wf_face_t *f);
void *wf_face_state(const wf_face_t *f);
const wf_ctx_t *wf_face_ctx(const wf_face_t *f);
/** The face's colour (customize) or, if none was chosen, the theme accent. */
lv_color_t wf_face_accent(const wf_face_t *f);
/** The face's colour or def if none was chosen (faces whose default is not the accent). */
lv_color_t wf_face_accent_or(const wf_face_t *f, lv_color_t def);

// --- Registry ----------------------------------------------------------------------------

/** Add a face (built-ins come from wf_init()). ESP_ERR_INVALID_STATE if the id exists. */
esp_err_t wf_register(const wf_face_def_t *def);
size_t wf_count(void);
const wf_face_def_t *wf_at(size_t index);
const wf_face_def_t *wf_find(const char *id);

// --- Engine ------------------------------------------------------------------------------

/** The home screen (id "face"); wf_init() installs it with ui_set_home(). */
extern const screen_def_t wf_home_screen;

#define WF_DEFAULT_FACE "digital"

/** Register the 4 built-in faces and the sample declarative faces, make the face
 *  screen the home screen, register the picker and customize screens and listen to
 *  the minute clock. Call once on the UI task before ui_start(). */
void wf_init(void);

/** Show face id (rebuilt at once if the home screen exists). ESP_ERR_NOT_FOUND if
 *  unknown (the active face stays). Same face: no-op. */
esp_err_t wf_set_active(const char *id);
const wf_face_def_t *wf_active(void);

/** AOD variant on/off (svc_power's AOD state). Rebuilds the face. */
void wf_set_aod(bool aod);
bool wf_is_aod(void);

/** Complication in slot of face, in memory (the customize screen saves through the
 *  listener). WF_COMP_COUNT or the slot's default = back to the default. */
esp_err_t wf_set_slot(const char *face_id, uint8_t slot, wf_comp_t comp);
wf_comp_t wf_get_slot(const wf_face_def_t *face, uint8_t slot);

#define WF_COLOR_DEFAULT 0xFF000000u // no colour chosen: the theme accent

/** Face colour 0xRRGGBB or WF_COLOR_DEFAULT, in memory like wf_set_slot(). */
esp_err_t wf_set_color(const char *face_id, uint32_t rgb);
uint32_t wf_get_color(const wf_face_def_t *face);

/** Colours the customize screen offers (after "Default"). */
typedef struct {
    const char *name;
    uint32_t rgb;
} wf_color_t;
extern const wf_color_t wf_colors[];
extern const size_t wf_color_count;

// --- Saved configuration (FACE_CONFIG setting, wf_cfg.h) -----------------------------------

/** Replace every face's slots and colour with cfg (NULL/"" = all defaults). Items for
 *  unknown faces, slots, complications or bad colours are skipped; returns how many.
 *  Call after the faces are registered (installed faces too). Rebuilds the active
 *  face if its configuration changed. */
int wf_config_load(const char *cfg);

/** Every non-default slot and colour as a wf_cfg string. ESP_ERR_INVALID_SIZE if it
 *  does not fit in len (buf then holds the items that fit). */
esp_err_t wf_config_save(char *buf, size_t len);

typedef enum {
    WF_CHANGE_ACTIVE, // the picker chose another face: save wf_active()->id
    WF_CHANGE_CONFIG, // customize changed a slot or colour: save wf_config_save()
} wf_change_t;

/** cb runs on the UI task when the user changed something in the picker or the
 *  customize screen (not for API calls such as wf_set_active()). One listener;
 *  app_main saves the settings. */
void wf_set_listener(void (*cb)(wf_change_t what, void *ctx), void *ctx);

// --- Picker and customize (docs/04 §4a) ------------------------------------------------------

/** Face picker (id "face.picker", no args): long-press on the face opens it. Swipe
 *  left/right between scaled previews, tap one to wear it, "Customize" for its slots
 *  and colour. */
extern const screen_def_t wf_picker_screen;
/** Customize (id "face.customize", args: const char *face id; NULL = the active face). */
extern const screen_def_t wf_customize_screen;

// --- Declarative faces (face.json, wf_decl.h) ----------------------------------------------

/** Parse face.json and register the face. ESP_ERR_INVALID_ARG if it does not parse
 *  (err says why), ESP_ERR_INVALID_STATE if the id is taken, ESP_ERR_NO_MEM if the
 *  registry or the heap is full. The model (~6 KB) lives in PSRAM; faces stay
 *  registered until reboot. */
esp_err_t wf_decl_register(const char *json, size_t len, wf_decl_err_t *err);

/** Called for every face directory wf_decl_load_dir() tried. */
typedef void (*wf_decl_report_t)(const char *path, esp_err_t result, const wf_decl_err_t *err, void *ctx);

/** Register every <dir>/<id>/face.json (installed faces: /flash/faces). The directory
 *  name must equal the face's id. Reads files: call at boot, not from the running UI
 *  task. Returns the number of faces registered (0 if dir does not exist). */
int wf_decl_load_dir(const char *dir, wf_decl_report_t report, void *ctx);

/** Data for faces and complications. Edit the fields, then call wf_data_changed()
 *  with what changed: visible faces update at once (no redraw when nothing they
 *  show changed). */
wf_data_t *wf_data_edit(void);
const wf_data_t *wf_data_get(void);
void wf_data_changed(uint32_t mask);

#ifdef __cplusplus
}
#endif
