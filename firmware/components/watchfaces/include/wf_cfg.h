// Face customisation string (the FACE_CONFIG setting, P3-04): which complication each
// slot shows and the accent colour, for the faces the user changed. Pure C, no LVGL
// (host test test_wf_cfg); the engine maps it to faces (wf_config_load/save).
//
//   <face>:<key>=<value>[,<key>=<value>...][;<face>:...]
//   analog:left=moon,color=ff9f0a;s3w.neon:ring=battery
//
// Keys are slot ids (value: complication id) and "color" (value: 6 lower-case hex
// digits). Every token is 1..47 characters of [a-z0-9._-], like face and slot ids.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WF_CFG_TOKEN_MAX 48 // with NUL; face ids are the longest (WF_DECL_ID_MAX)
#define WF_CFG_COLOR_KEY "color"

/** One item; return false to reject it (counted by wf_cfg_parse). */
typedef bool (*wf_cfg_item_cb_t)(const char *face, const char *key, const char *value, void *ctx);

/** Call cb for every well-formed item in s (NULL or "" = none). A malformed face
 *  entry (bad face token or no ':') is skipped up to the next ';', a malformed item
 *  up to the next ',' or ';'. Returns the number of skipped or rejected items. */
int wf_cfg_parse(const char *s, wf_cfg_item_cb_t cb, void *ctx);

/** Builds a string item by item; buf always stays NUL-terminated and holds only
 *  complete items. */
typedef struct {
    char *buf;
    size_t cap;
    size_t len;
    size_t need; // length the whole string would have
    char face[WF_CFG_TOKEN_MAX];
} wf_cfg_writer_t;

void wf_cfg_writer_init(wf_cfg_writer_t *w, char *buf, size_t cap);

/** Append one item (starts a new face entry if face differs from the previous
 *  item's). False if a token is invalid (nothing written) or the item does not fit
 *  (w->need still grows, so the caller can report the size it would take). */
bool wf_cfg_put(wf_cfg_writer_t *w, const char *face, const char *key, const char *value);

/** "ff9f0a" for 0xFF9F0A (buf >= 7). */
void wf_cfg_color_str(uint32_t rgb, char *buf);
/** Parse 6 hex digits; false if s is not exactly that. */
bool wf_cfg_color_parse(const char *s, uint32_t *rgb);

#ifdef __cplusplus
}
#endif
