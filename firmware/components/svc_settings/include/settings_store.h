// Settings store: RAM copy of every setting in settings_schema.h, validation, and
// load/flush against a key-value backend (NVS on target, a map in host tests).
// Pure C and host-tested. Not thread-safe: svc_settings wraps it in a mutex.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "settings_schema.h"

#ifdef __cplusplus
extern "C" {
#endif

#define S3W_SETTINGS_KEY_MAX 15 // NVS_KEY_NAME_MAX_SIZE - 1

typedef enum {
    S3W_SETTING_TYPE_INT,
    S3W_SETTING_TYPE_BOOL,
    S3W_SETTING_TYPE_STR,
} s3w_setting_type_t;

typedef struct {
    const char *key;
    s3w_setting_type_t type;
    int32_t def;          // INT/BOOL
    int32_t min, max;     // INT: range; BOOL: 0..1; STR: 0..max_len
    const char *def_str;  // STR only
    uint16_t str_off;     // STR only: offset of the value in settings_store_t.str
} s3w_setting_info_t;

// Bytes for all string values, each with its NUL.
#define S3W_SETTING_POOL_INT_(id, key, def, min, max)
#define S3W_SETTING_POOL_BOOL_(id, key, def)
#define S3W_SETTING_POOL_STR_(id, key, def, maxlen) +((maxlen) + 1)
#define S3W_SETTINGS_STR_POOL \
    (0 S3W_SETTINGS_SCHEMA(S3W_SETTING_POOL_INT_, S3W_SETTING_POOL_BOOL_, S3W_SETTING_POOL_STR_))

#define S3W_SETTINGS_DIRTY_WORDS ((S3W_SETTING_COUNT + 31) / 32)

/** Plain data (no pointers), so a copy is a consistent snapshot to flush outside the lock. */
typedef struct {
    int32_t num[S3W_SETTING_COUNT];  // INT and BOOL values (STR entries unused)
    char str[S3W_SETTINGS_STR_POOL];
    uint32_t dirty[S3W_SETTINGS_DIRTY_WORDS]; // changed in RAM, not yet written to the backend
} settings_store_t;

/**
 * Key-value backend. get_* return ESP_ERR_NOT_FOUND for a missing key; get_str gets
 * the buffer size in *len and returns ESP_ERR_INVALID_SIZE if the value does not fit.
 * commit makes preceding writes durable. erase_all removes every settings key.
 */
typedef struct {
    esp_err_t (*get_i32)(void *ctx, const char *key, int32_t *out);
    esp_err_t (*set_i32)(void *ctx, const char *key, int32_t v);
    esp_err_t (*get_str)(void *ctx, const char *key, char *out, size_t *len);
    esp_err_t (*set_str)(void *ctx, const char *key, const char *v);
    esp_err_t (*commit)(void *ctx);
    esp_err_t (*erase_all)(void *ctx);
    void *ctx;
} settings_backend_t;

const s3w_setting_info_t *settings_info(s3w_setting_t id);

/** ESP_ERR_NOT_FOUND if no setting has this NVS key. */
esp_err_t settings_find(const char *key, s3w_setting_t *out);

/** All values to their defaults, nothing dirty. */
void settings_store_defaults(settings_store_t *s);

/**
 * Defaults, then every stored key that is present, of the right type and in range.
 * Missing or invalid keys keep the default (invalid ones are marked dirty so the next
 * flush repairs them). Returns the number of keys that fell back for being invalid.
 */
int settings_store_load(settings_store_t *s, const settings_backend_t *be);

/**
 * Set a value. ESP_ERR_INVALID_ARG for an unknown id, the wrong type, an out-of-range
 * int or NULL string; ESP_ERR_INVALID_SIZE for a string longer than max_len.
 * *changed (may be NULL) is false when the value was already equal: nothing is marked dirty.
 */
esp_err_t settings_store_set_int(settings_store_t *s, s3w_setting_t id, int32_t v, bool *changed);
esp_err_t settings_store_set_bool(settings_store_t *s, s3w_setting_t id, bool v, bool *changed);
esp_err_t settings_store_set_str(settings_store_t *s, s3w_setting_t id, const char *v, bool *changed);

/** Wrong type or unknown id returns 0 / false / "". */
int32_t settings_store_get_int(const settings_store_t *s, s3w_setting_t id);
bool settings_store_get_bool(const settings_store_t *s, s3w_setting_t id);
const char *settings_store_get_str(const settings_store_t *s, s3w_setting_t id);

bool settings_store_is_dirty(const settings_store_t *s, s3w_setting_t id);
bool settings_store_any_dirty(const settings_store_t *s);

/**
 * Write every dirty key, then commit. Keys written successfully are cleared; failed
 * ones stay dirty. Returns the first error (ESP_OK if all were written and committed).
 */
esp_err_t settings_store_flush(settings_store_t *s, const settings_backend_t *be);

#ifdef __cplusplus
}
#endif
