// Settings schema: the one place every persisted setting is declared (CLAUDE.md).
// svc_settings builds its tables, the NVS keys and the typed API from this list.
//
//   INT (ID, nvs_key, default, min, max)
//   BOOL(ID, nvs_key, default)
//   STR (ID, nvs_key, default, max_len)      max_len excludes the terminating NUL
//
// Rules: nvs_key <= 15 chars and never reused for a different meaning; never change
// a key's type (add a new key instead); append new entries anywhere — ids are not
// persisted, keys are. Out-of-range stored values fall back to the default on load.
#pragma once

#define S3W_SETTINGS_SCHEMA(INT, BOOL, STR)                                  \
    /* Display */                                                            \
    INT (DISPLAY_BRIGHTNESS, "disp_bright", 60, 5, 100)   /* % */            \
    INT (SCREEN_TIMEOUT_S,   "disp_timeout", 10, 5, 300)  /* s, idle->off */ \
    BOOL(AOD,                "disp_aod",     false)                          \
    BOOL(RAISE_TO_WAKE,      "wake_raise",   true)                           \
    INT (RAISE_SENSITIVITY,  "wake_raise_sens", 1, 0, 2)  /* low/mid/high */ \
    BOOL(WAKE_ON_TAP,        "wake_tap",     true)                           \
    BOOL(WAKE_ON_NOTIFY,     "wake_notify",  true)                           \
    STR (WATCH_FACE,         "face",         "digital", 47) /* face id */    \
    STR (FACE_CONFIG,        "face_cfg",     "", 511) /* wf_cfg.h */         \
    BOOL(LAUNCHER_GRID,      "launch_grid",  false) /* honeycomb */      \
    /* Sound */                                                              \
    INT (VOLUME_MEDIA,       "vol_media",    60, 0, 100)  /* % */            \
    INT (VOLUME_SYSTEM,      "vol_system",   50, 0, 100)  /* % */            \
    INT (VOLUME_ALARM,       "vol_alarm",    80, 10, 100) /* %, never mute */\
    BOOL(SILENT,             "silent",       false)                          \
    INT (HAPTICS_LEVEL,      "haptics",      2, 0, 3)     /* off..strong */  \
    BOOL(CALL_RING,          "call_ring",    true) /* ring for calls, P6-04 */\
    /* Connections */                                                        \
    BOOL(BLUETOOTH,          "bt_on",        true) /* svc_ble follows it */  \
    BOOL(WIFI,               "wifi_on",      false) /* svc_wifi follows it */\
    INT (WIFI_IDLE_OFF,      "wifi_idle_min", 10, 0, 120) /* min unused->off; 0=never */\
    /* Battery */                                                            \
    BOOL(BATTERY_SAVER,      "pwr_saver",    false)                          \
    /* Notifications */                                                      \
    BOOL(DND,                "dnd",          false) /* by hand (svc_modes) */\
    INT (DND_DAYS,           "dnd_days",     0, 0, 127)   /* bit0=Sun; 0=off */\
    INT (DND_START,          "dnd_start",    1320, 0, 1439) /* min of day */ \
    INT (DND_END,            "dnd_end",      420, 0, 1439)  /* min of day */ \
    BOOL(SLEEP_MODE,         "sleep_mode",   false) /* by hand */            \
    INT (SLEEP_DAYS,         "sleep_days",   0, 0, 127)   /* bit0=Sun; 0=off */\
    INT (SLEEP_START,        "sleep_start",  1380, 0, 1439)                  \
    INT (SLEEP_END,          "sleep_end",    420, 0, 1439)                   \
    /* Health profile and goals (svc_activity, P5-01) */                     \
    INT (HEIGHT_CM,          "hl_height",    170, 100, 250)                  \
    INT (WEIGHT_KG,          "hl_weight",    70, 30, 250)                    \
    INT (SEX,                "hl_sex",       0, 0, 2) /* 0 unset, 1 m, 2 f */\
    INT (BIRTH_YEAR,         "hl_birth",     1990, 1900, 2100)               \
    INT (STEP_GOAL,          "hl_step_goal", 10000, 1000, 100000)            \
    INT (ACTIVE_GOAL_MIN,    "hl_active_goal", 30, 5, 600)                   \
    /* Language & region */                                                  \
    BOOL(TIME_24H,           "time_24h",     true)                           \
    STR (WORLD_CLOCKS,       "world",        "", 95)     /* city ids, csv */\
    STR (TIMEZONE,           "tz",           "UTC0", 63)  /* POSIX TZ */     \
    STR (LANGUAGE,           "lang",         "en", 7)     /* BCP 47 */       \
    /* Accessibility */                                                      \
    BOOL(LARGE_TEXT,         "a11y_large",   false)                          \
    BOOL(HIGH_CONTRAST,      "a11y_contrast", false)                         \
    /* Developer options */                                                  \
    BOOL(DEV_CONSOLE,        "dev_console",  true)                           \
    BOOL(DEV_UNSIGNED_APPS,  "dev_unsigned", false)                          \
    BOOL(DEV_FPS_OVERLAY,    "dev_fps",      false)

#define S3W_SETTING_ID_INT_(id, key, def, min, max) S3W_SETTING_##id,
#define S3W_SETTING_ID_BOOL_(id, key, def)          S3W_SETTING_##id,
#define S3W_SETTING_ID_STR_(id, key, def, maxlen)   S3W_SETTING_##id,

/** Setting ids (S3W_SETTING_DISPLAY_BRIGHTNESS, ...). Runtime only, never persisted. */
typedef enum {
    S3W_SETTINGS_SCHEMA(S3W_SETTING_ID_INT_, S3W_SETTING_ID_BOOL_, S3W_SETTING_ID_STR_)
    S3W_SETTING_COUNT
} s3w_setting_t;
