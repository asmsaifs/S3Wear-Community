// Settings app tree (docs/03-firmware-features.md F18, P3-10): the pages and rows of
// the on-watch Settings app as data. Pure logic, no LVGL: built by firmware/host_test,
// which checks every row against the settings schema. settings_apps.c draws it.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "settings_schema.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SETTINGS_PAGE,   // opens a page of children
    SETTINGS_TOGGLE, // BOOL setting
    SETTINGS_SLIDER, // INT setting min..max, committed when released
    SETTINGS_CHOICE, // INT setting, one of opts
    SETTINGS_TIME,   // INT setting, minute of day, hour and minute pickers
    SETTINGS_ZONE,   // STR setting TIMEZONE, one of the world clock cities
    SETTINGS_ACTION, // confirm, then the backend's action()
    SETTINGS_INFO,   // read only text
    SETTINGS_APP,    // opens the screen with id `arg`
    SETTINGS_SOON,   // the feature comes with a later phase: a toast
} settings_kind_t;

typedef enum {
    SETTINGS_ACT_RESTART,
    SETTINGS_ACT_POWER_OFF,
    SETTINGS_ACT_FACTORY_RESET,
} settings_action_t;

typedef enum {
    SETTINGS_INFO_TEXT, // `arg`
    SETTINGS_INFO_VERSION,
    SETTINGS_INFO_IDF,
    SETTINGS_INFO_STORAGE,
} settings_info_t;

typedef struct {
    const char *label;
    int32_t value;
} settings_opt_t;

typedef struct settings_node settings_node_t;

struct settings_node {
    settings_kind_t kind;
    const char *title;
    const char *icon; // LV_SYMBOL_* (top level rows only) or NULL
    s3w_setting_t setting;
    int32_t min, max;   // SLIDER
    const char *unit;   // SLIDER, may be NULL
    const settings_opt_t *opts; // CHOICE
    uint8_t n_opts;
    const settings_node_t *children; // PAGE
    uint8_t n_children;
    const char *arg;    // APP: screen id; INFO_TEXT: the text; ACTION: dialog body (may be NULL)
    settings_action_t action; // ACTION
    bool danger;        // ACTION: red confirm button
    settings_info_t info;     // INFO
};

/** The top page ("Settings"). */
const settings_node_t *settings_tree_root(void);

/** Label of the option with this value, NULL if none (the setting holds something else). */
const char *settings_opt_label(const settings_node_t *node, int32_t value);

/** Position of the option with this value, -1 if none. */
int settings_opt_index(const settings_node_t *node, int32_t value);

#ifdef __cplusplus
}
#endif
