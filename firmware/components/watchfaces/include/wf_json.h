// Small strict JSON pull parser for face.json (docs/03-firmware-features.md F1). Pure C,
// no allocation, nesting limited to WF_JSON_MAX_DEPTH: the input comes from the phone
// or the filesystem and is untrusted.
//
// The caller walks the document in order:
//
//   wf_json_t j;
//   wf_json_init(&j, text, len);
//   char key[32];
//   if (wf_json_obj_begin(&j)) {
//       while (wf_json_obj_next(&j, key, sizeof key)) {
//           if (strcmp(key, "x") == 0) wf_json_int(&j, &x);
//           else wf_json_skip(&j);
//       }
//   }
//   ok = wf_json_finish(&j); // no error and nothing but whitespace left
//
// The first error sticks: every later call returns false, and wf_json_error() gives
// the message and wf_json_error_pos() the byte offset. Callers may also stop the walk
// with their own error (wf_json_fail()).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WF_JSON_MAX_DEPTH 16

typedef enum {
    WF_JSON_NONE, // end of input or error
    WF_JSON_OBJECT,
    WF_JSON_ARRAY,
    WF_JSON_STRING,
    WF_JSON_NUMBER,
    WF_JSON_BOOL,
    WF_JSON_NULL,
} wf_json_type_t;

typedef struct {
    const char *start;
    const char *p;
    const char *end;
    const char *err;    // static message, NULL while fine
    size_t err_pos;     // byte offset of the error
    uint8_t depth;
    uint32_t first;     // bit d: no member read yet at depth d
} wf_json_t;

void wf_json_init(wf_json_t *j, const char *text, size_t len);

/** Type of the next value (whitespace skipped), without consuming it. */
wf_json_type_t wf_json_peek(wf_json_t *j);

/** Consume '{'. */
bool wf_json_obj_begin(wf_json_t *j);
/** Next member: reads its key (and the ':') into key. False at the closing '}'
 *  (consumed) or on error. A key longer than len - 1 is an error. */
bool wf_json_obj_next(wf_json_t *j, char *key, size_t len);

/** Consume '['. */
bool wf_json_arr_begin(wf_json_t *j);
/** True if another element follows (read it next); false at the closing ']'
 *  (consumed) or on error. */
bool wf_json_arr_next(wf_json_t *j);

/** String value, unescaped to UTF-8 (\uXXXX included). Longer than len - 1 or a NUL
 *  character is an error. */
bool wf_json_string(wf_json_t *j, char *buf, size_t len);
/** Integer value: a JSON number without fraction or exponent in int32 range. */
bool wf_json_int(wf_json_t *j, int32_t *out);
bool wf_json_bool(wf_json_t *j, bool *out);
/** Skip any value. */
bool wf_json_skip(wf_json_t *j);

/** True if no error happened and only whitespace is left. */
bool wf_json_finish(wf_json_t *j);

/** Stop with an error at the current position (first error wins). Returns false. */
bool wf_json_fail(wf_json_t *j, const char *msg);

bool wf_json_ok(const wf_json_t *j);
const char *wf_json_error(const wf_json_t *j);
size_t wf_json_error_pos(const wf_json_t *j);
/** 1-based line and column of byte offset pos. */
void wf_json_line_col(const wf_json_t *j, size_t pos, int *line, int *col);

#ifdef __cplusplus
}
#endif
