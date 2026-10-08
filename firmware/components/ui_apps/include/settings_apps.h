// On-watch Settings app (docs/03-firmware-features.md F18, docs/04-ui-ux.md §4e, P3-10):
// id "settings" (the root page) with "settings.page", "settings.choice" and
// "settings.time" for the rest of the tree (settings_tree.h). Portable (LVGL +
// ui_framework): the simulator builds it too. UI task only, like ui_nav.h.
//
// The screens call no service: values and actions go through a backend (app_main:
// svc_settings, svc_power; simulator: an in-memory store). A page re-reads its values
// when it becomes visible again (back from a choice or time screen).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "settings_tree.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Text for the About page (empty string = unknown, shown as "--"). */
typedef struct {
    char version[32];
    char idf[32];
    char storage[32]; // "12.4 of 28 MB free"
} settings_about_t;

typedef struct {
    /** Current value; BOOL settings as 0/1. */
    int32_t (*get_int)(s3w_setting_t id, void *ctx);
    esp_err_t (*set_int)(s3w_setting_t id, int32_t value, void *ctx);
    /** Copy the string value into buf (NUL-terminated). */
    void (*get_str)(s3w_setting_t id, char *buf, size_t len, void *ctx);
    esp_err_t (*set_str)(s3w_setting_t id, const char *value, void *ctx);
    /** An action was confirmed. ESP_ERR_NOT_SUPPORTED: the feature does not exist yet (toast). */
    esp_err_t (*action)(settings_action_t action, void *ctx);
    void (*about)(settings_about_t *out, void *ctx);
    void *ctx;
} settings_app_backend_t;

/** Register the screens (shell_init() does this). */
void settings_apps_init(void);

/** Install the backend (copied). Without one every row shows its minimum and changes
 *  say "Not available". */
void settings_apps_set_backend(const settings_app_backend_t *backend);

#ifdef __cplusplus
}
#endif
