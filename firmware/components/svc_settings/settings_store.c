#include "settings_store.h"

#include <string.h>

// NVS keys are at most 15 characters; checked at compile time for every entry.
#define KEY_CHECK_INT_(id, key, def, min, max) \
    _Static_assert(sizeof(key) - 1 <= S3W_SETTINGS_KEY_MAX, "settings key too long: " key);
#define KEY_CHECK_BOOL_(id, key, def) \
    _Static_assert(sizeof(key) - 1 <= S3W_SETTINGS_KEY_MAX, "settings key too long: " key);
#define KEY_CHECK_STR_(id, key, def, maxlen)                                                   \
    _Static_assert(sizeof(key) - 1 <= S3W_SETTINGS_KEY_MAX, "settings key too long: " key);    \
    _Static_assert(sizeof(def) - 1 <= (maxlen), "settings default longer than max_len: " key);
S3W_SETTINGS_SCHEMA(KEY_CHECK_INT_, KEY_CHECK_BOOL_, KEY_CHECK_STR_)

// String offsets: each STR entry starts after the previous ones in the pool.
#define OFF_INT_(id, key, def, min, max)
#define OFF_BOOL_(id, key, def)
#define OFF_STR_(id, key, def, maxlen) OFF_##id, OFF_NUL_##id = OFF_##id + (maxlen),
// OFF_x is x's first byte, OFF_NUL_x its terminator; the next entry follows at OFF_NUL_x + 1.
enum { OFF_START_ = -1, S3W_SETTINGS_SCHEMA(OFF_INT_, OFF_BOOL_, OFF_STR_) };

#define INFO_INT_(id, key, def, min, max) \
    [S3W_SETTING_##id] = {key, S3W_SETTING_TYPE_INT, (def), (min), (max), NULL, 0},
#define INFO_BOOL_(id, key, def) [S3W_SETTING_##id] = {key, S3W_SETTING_TYPE_BOOL, (def) ? 1 : 0, 0, 1, NULL, 0},
#define INFO_STR_(id, key, def, maxlen) \
    [S3W_SETTING_##id] = {key, S3W_SETTING_TYPE_STR, 0, 0, (maxlen), def, (uint16_t)OFF_##id},

static const s3w_setting_info_t s_info[S3W_SETTING_COUNT] = {
    S3W_SETTINGS_SCHEMA(INFO_INT_, INFO_BOOL_, INFO_STR_)
};

static bool valid_id(s3w_setting_t id)
{
    return (unsigned)id < S3W_SETTING_COUNT;
}

static void mark(settings_store_t *s, s3w_setting_t id)
{
    s->dirty[id / 32] |= 1u << (id % 32);
}

static void unmark(settings_store_t *s, s3w_setting_t id)
{
    s->dirty[id / 32] &= ~(1u << (id % 32));
}

const s3w_setting_info_t *settings_info(s3w_setting_t id)
{
    return valid_id(id) ? &s_info[id] : NULL;
}

esp_err_t settings_find(const char *key, s3w_setting_t *out)
{
    for (int i = 0; key && i < S3W_SETTING_COUNT; i++) {
        if (strcmp(s_info[i].key, key) == 0) {
            *out = (s3w_setting_t)i;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

void settings_store_defaults(settings_store_t *s)
{
    memset(s, 0, sizeof *s);
    for (int i = 0; i < S3W_SETTING_COUNT; i++) {
        const s3w_setting_info_t *in = &s_info[i];
        if (in->type == S3W_SETTING_TYPE_STR) {
            strcpy(&s->str[in->str_off], in->def_str);
        } else {
            s->num[i] = in->def;
        }
    }
}

int settings_store_load(settings_store_t *s, const settings_backend_t *be)
{
    settings_store_defaults(s);
    int invalid = 0;
    for (int i = 0; i < S3W_SETTING_COUNT; i++) {
        const s3w_setting_info_t *in = &s_info[i];
        esp_err_t err;
        if (in->type == S3W_SETTING_TYPE_STR) {
            // Straight into the value's slot (max + 1 bytes), no pool-sized stack buffer.
            char *dst = &s->str[in->str_off];
            size_t len = (size_t)in->max + 1;
            err = be->get_str(be->ctx, in->key, dst, &len);
            if (err == ESP_OK && strnlen(dst, (size_t)in->max + 1) <= (size_t)in->max) {
                continue;
            }
            strcpy(dst, in->def_str); // the backend may have written part of it
        } else {
            int32_t v;
            err = be->get_i32(be->ctx, in->key, &v);
            if (err == ESP_OK && v >= in->min && v <= in->max) {
                s->num[i] = v;
                continue;
            }
        }
        if (err != ESP_ERR_NOT_FOUND) {
            // Wrong type, too long, out of range or unreadable: default, rewritten on next flush.
            invalid++;
            mark(s, (s3w_setting_t)i);
        }
    }
    return invalid;
}

static esp_err_t set_num(settings_store_t *s, s3w_setting_t id, s3w_setting_type_t type, int32_t v, bool *changed)
{
    if (changed) {
        *changed = false;
    }
    if (!valid_id(id) || s_info[id].type != type || v < s_info[id].min || v > s_info[id].max) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s->num[id] != v) {
        s->num[id] = v;
        mark(s, id);
        if (changed) {
            *changed = true;
        }
    }
    return ESP_OK;
}

esp_err_t settings_store_set_int(settings_store_t *s, s3w_setting_t id, int32_t v, bool *changed)
{
    return set_num(s, id, S3W_SETTING_TYPE_INT, v, changed);
}

esp_err_t settings_store_set_bool(settings_store_t *s, s3w_setting_t id, bool v, bool *changed)
{
    return set_num(s, id, S3W_SETTING_TYPE_BOOL, v ? 1 : 0, changed);
}

esp_err_t settings_store_set_str(settings_store_t *s, s3w_setting_t id, const char *v, bool *changed)
{
    if (changed) {
        *changed = false;
    }
    if (!valid_id(id) || s_info[id].type != S3W_SETTING_TYPE_STR || !v) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(v) > (size_t)s_info[id].max) {
        return ESP_ERR_INVALID_SIZE;
    }
    char *cur = &s->str[s_info[id].str_off];
    if (strcmp(cur, v) != 0) {
        strcpy(cur, v);
        mark(s, id);
        if (changed) {
            *changed = true;
        }
    }
    return ESP_OK;
}

int32_t settings_store_get_int(const settings_store_t *s, s3w_setting_t id)
{
    return valid_id(id) && s_info[id].type == S3W_SETTING_TYPE_INT ? s->num[id] : 0;
}

bool settings_store_get_bool(const settings_store_t *s, s3w_setting_t id)
{
    return valid_id(id) && s_info[id].type == S3W_SETTING_TYPE_BOOL && s->num[id] != 0;
}

const char *settings_store_get_str(const settings_store_t *s, s3w_setting_t id)
{
    return valid_id(id) && s_info[id].type == S3W_SETTING_TYPE_STR ? &s->str[s_info[id].str_off] : "";
}

bool settings_store_is_dirty(const settings_store_t *s, s3w_setting_t id)
{
    return valid_id(id) && (s->dirty[id / 32] & (1u << (id % 32)));
}

bool settings_store_any_dirty(const settings_store_t *s)
{
    for (int w = 0; w < S3W_SETTINGS_DIRTY_WORDS; w++) {
        if (s->dirty[w]) {
            return true;
        }
    }
    return false;
}

esp_err_t settings_store_flush(settings_store_t *s, const settings_backend_t *be)
{
    if (!settings_store_any_dirty(s)) {
        return ESP_OK;
    }
    uint32_t written[S3W_SETTINGS_DIRTY_WORDS] = {0};
    esp_err_t first = ESP_OK;
    for (int i = 0; i < S3W_SETTING_COUNT; i++) {
        const s3w_setting_t id = (s3w_setting_t)i;
        if (!settings_store_is_dirty(s, id)) {
            continue;
        }
        const s3w_setting_info_t *in = &s_info[i];
        const esp_err_t err = in->type == S3W_SETTING_TYPE_STR ? be->set_str(be->ctx, in->key, &s->str[in->str_off])
                                                               : be->set_i32(be->ctx, in->key, s->num[i]);
        if (err == ESP_OK) {
            unmark(s, id);
            written[i / 32] |= 1u << (i % 32);
        } else if (first == ESP_OK) {
            first = err;
        }
    }
    const esp_err_t err = be->commit(be->ctx);
    if (err != ESP_OK) {
        // Nothing is known to be durable: everything written stays dirty.
        for (int w = 0; w < S3W_SETTINGS_DIRTY_WORDS; w++) {
            s->dirty[w] |= written[w];
        }
        return first != ESP_OK ? first : err;
    }
    return first;
}
