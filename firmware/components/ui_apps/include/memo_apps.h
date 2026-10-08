// Voice memos app (docs/03 F16, docs/04 §4m, P7-01). Portable (LVGL + ui_framework): the simulator
// builds it too. UI task only, like ui_nav.h.
//
// The screens call no service: recording, playback, deleting and syncing go through a backend
// (app_main: svc_memo; simulator: sim_memo.c). The list, the recorder / player state and how a
// recording or playback ended come in with memo_apps_set_list(), memo_apps_set_state() and
// memo_apps_result(), and are kept outside the screens.
#pragma once

#include <stdint.h>
#include "memo_store.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MEMO_UI_IDLE,
    MEMO_UI_RECORDING,
    MEMO_UI_PLAYING,
} memo_ui_mode_t;

typedef struct {
    uint8_t mode;        // memo_ui_mode_t
    uint8_t level;       // recording: mic level 0..100
    uint32_t elapsed_ms;
    uint32_t total_ms;   // recording: the most it can take; playing: the memo's length
    char name[MEMO_NAME_MAX];
} memo_ui_state_t;

/** How a recording or playback ended (the toasts in memo_apps.c). */
typedef enum {
    MEMO_UI_SAVED,        // "Memo saved"
    MEMO_UI_SAVED_FULL,   // "Memo saved: limit reached" (length or storage)
    MEMO_UI_TOO_SHORT,    // "Too short: not saved"
    MEMO_UI_NO_SPACE,     // "Not enough storage"
    MEMO_UI_MIC_FAILED,   // "Microphone not available"
    MEMO_UI_STORAGE_ERROR,// "Could not save the memo"
    MEMO_UI_PLAYED,       // (no toast)
    MEMO_UI_PLAY_FAILED,  // "Could not play the memo"
} memo_ui_result_t;

typedef struct {
    void (*record)(void *ctx);                   // start a new memo; the state follows
    void (*stop)(void *ctx);                     // stop recording (kept) or playing
    void (*play)(const char *name, void *ctx);
    void (*remove)(const char *name, void *ctx); // delete; the list follows
    void (*sync)(void *ctx);                     // send what the phone lacks (the app opened)
    void *ctx;
} memo_backend_t;

/** Register the screens (shell_init() does this). */
void memo_apps_init(void);

/** Install the backend (copied). Without one, Record says "Microphone not available". */
void memo_apps_set_backend(const memo_backend_t *backend);

/** The memos (copied, newest first) and the one going to the phone ("" or NULL: none). */
void memo_apps_set_list(const memo_list_t *list, const char *sending);

/** The recorder / player state changed or moved on (copied). */
void memo_apps_set_state(const memo_ui_state_t *state);

/** A recording or playback ended: the toast, and the recorder screen closes. */
void memo_apps_result(memo_ui_result_t result);

#ifdef __cplusplus
}
#endif
