// Media app and the Media tile's data (docs/03 F9, docs/04 §4h, P6-01). Portable (LVGL +
// ui_framework): the simulator builds it too. UI task only, like ui_nav.h.
//
// The screen calls no service: commands and the artwork go through a backend (app_main:
// svc_media; simulator: sim_media.c). The session comes in with media_apps_set_state().
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "lvgl.h"
#include "media_state.h"
#include "media_text.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Why a command was not carried out (toast). */
typedef enum {
    MEDIA_FAIL_OFFLINE,     // "Phone not connected"
    MEDIA_FAIL_NO_ANSWER,   // "Phone did not answer"
    MEDIA_FAIL_UNSUPPORTED, // "Update the phone app"
    MEDIA_FAIL_REFUSED,     // "Phone could not do that"
} media_fail_t;

typedef struct {
    void (*command)(media_cmd_t cmd, uint32_t value, void *ctx); // failures come back as media_apps_command_failed()
    /** Copy the artwork for hash (MEDIA_ART_BYTES, RGB565); false if it is not here yet. */
    bool (*artwork)(uint32_t hash, uint16_t *out, size_t cap, void *ctx);
    /** Copy the phone-drawn title + artist (A8, *w bytes per row); false if not complete yet. */
    bool (*text_bitmap)(uint32_t id, uint8_t *a8, size_t cap, uint16_t *w, uint16_t *h, void *ctx);
    /** Monotonic ms of the same clock as media_state_t.position_at_ms. */
    int64_t (*now_ms)(void *ctx);
    void *ctx;
} media_backend_t;

/** Register the screen (shell_init() does this). */
void media_apps_init(void);

/** Install the backend (copied). Without one, commands say "Phone not connected". */
void media_apps_set_backend(const media_backend_t *backend);

/** The session changed (copied): refresh the Media app (id "media") if open. */
void media_apps_set_state(const media_state_t *st, bool connected);

/** New artwork may be available from the backend: refresh the open app. */
void media_apps_artwork_changed(void);

/** A command failed: toast why. */
void media_apps_command_failed(media_fail_t why);

/** The session as last set (for the Media tile); *connected may be NULL. */
const media_state_t *media_apps_state(bool *connected);

/**
 * The phone-drawn title + artist of the session (MediaState.text_bitmap_id) into an A8 image
 * descriptor of MEDIA_TEXT_A8 bytes (media_apps_text_dsc_alloc); false: none or not complete.
 * The labels show while it is false.
 */
bool media_apps_text_image(lv_image_dsc_t *dsc);

/** An A8 descriptor large enough for media_apps_text_image(), lv_malloc'd (free with lv_free). */
lv_image_dsc_t *media_apps_text_dsc_alloc(void);

#ifdef __cplusplus
}
#endif
