// JSON pull parser (wf_json.h). Pure C, RFC 8259 grammar, no allocation.
#include "wf_json.h"

#include <string.h>

void wf_json_init(wf_json_t *j, const char *text, size_t len)
{
    memset(j, 0, sizeof *j);
    j->start = text;
    j->p = text;
    j->end = text + len;
}

bool wf_json_fail(wf_json_t *j, const char *msg)
{
    if (j->err == NULL) {
        j->err = msg;
        j->err_pos = (size_t)(j->p - j->start);
    }
    return false;
}

bool wf_json_ok(const wf_json_t *j)
{
    return j->err == NULL;
}

const char *wf_json_error(const wf_json_t *j)
{
    return j->err;
}

size_t wf_json_error_pos(const wf_json_t *j)
{
    return j->err_pos;
}

void wf_json_line_col(const wf_json_t *j, size_t pos, int *line, int *col)
{
    int l = 1, c = 1;
    for (const char *p = j->start; p < j->start + pos && p < j->end; p++) {
        if (*p == '\n') {
            l++;
            c = 1;
        } else {
            c++;
        }
    }
    *line = l;
    *col = c;
}

static void skip_ws(wf_json_t *j)
{
    while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r')) {
        j->p++;
    }
}

wf_json_type_t wf_json_peek(wf_json_t *j)
{
    if (j->err) {
        return WF_JSON_NONE;
    }
    skip_ws(j);
    if (j->p >= j->end) {
        return WF_JSON_NONE;
    }
    switch (*j->p) {
    case '{':
        return WF_JSON_OBJECT;
    case '[':
        return WF_JSON_ARRAY;
    case '"':
        return WF_JSON_STRING;
    case 't':
    case 'f':
        return WF_JSON_BOOL;
    case 'n':
        return WF_JSON_NULL;
    default:
        return (*j->p == '-' || (*j->p >= '0' && *j->p <= '9')) ? WF_JSON_NUMBER : WF_JSON_NONE;
    }
}

static bool expect_value(wf_json_t *j, wf_json_type_t want, const char *msg)
{
    const wf_json_type_t t = wf_json_peek(j);
    if (t == want) {
        return true;
    }
    if (j->err) {
        return false;
    }
    return wf_json_fail(j, t == WF_JSON_NONE && j->p >= j->end ? "unexpected end of input" : msg);
}

/** Consume the '{' or '[' at j->p. */
static bool open(wf_json_t *j)
{
    if (j->depth >= WF_JSON_MAX_DEPTH) {
        return wf_json_fail(j, "nested too deep");
    }
    j->p++;
    j->depth++;
    j->first |= 1u << j->depth;
    return true;
}

/** Before the next member of the open container: ',' unless it is the first. False
 *  at the closing bracket (consumed). */
static bool next_member(wf_json_t *j, char close, const char *msg)
{
    if (j->err) {
        return false;
    }
    skip_ws(j);
    if (j->p >= j->end) {
        return wf_json_fail(j, "unexpected end of input");
    }
    const uint32_t bit = 1u << j->depth;
    if (*j->p == close) {
        j->p++;
        j->first &= ~bit;
        j->depth--;
        return false;
    }
    if (j->first & bit) {
        j->first &= ~bit;
        return true;
    }
    if (*j->p != ',') {
        return wf_json_fail(j, msg);
    }
    j->p++;
    skip_ws(j);
    if (j->p < j->end && *j->p == close) {
        return wf_json_fail(j, "trailing comma");
    }
    return true;
}

bool wf_json_obj_begin(wf_json_t *j)
{
    return expect_value(j, WF_JSON_OBJECT, "expected an object") && open(j);
}

bool wf_json_arr_begin(wf_json_t *j)
{
    return expect_value(j, WF_JSON_ARRAY, "expected an array") && open(j);
}

bool wf_json_arr_next(wf_json_t *j)
{
    return next_member(j, ']', "expected ',' or ']'");
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static bool read_u16(wf_json_t *j, uint32_t *out)
{
    if (j->end - j->p < 4) {
        return wf_json_fail(j, "bad \\u escape");
    }
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        const int d = hex_digit(j->p[i]);
        if (d < 0) {
            return wf_json_fail(j, "bad \\u escape");
        }
        v = v << 4 | (uint32_t)d;
    }
    j->p += 4;
    *out = v;
    return true;
}

/** Append code point cp as UTF-8; buf may be NULL (skip). */
static bool put_utf8(wf_json_t *j, uint32_t cp, char *buf, size_t len, size_t *n)
{
    char tmp[4];
    size_t k;
    if (cp < 0x80) {
        tmp[0] = (char)cp;
        k = 1;
    } else if (cp < 0x800) {
        tmp[0] = (char)(0xC0 | cp >> 6);
        tmp[1] = (char)(0x80 | (cp & 0x3F));
        k = 2;
    } else if (cp < 0x10000) {
        tmp[0] = (char)(0xE0 | cp >> 12);
        tmp[1] = (char)(0x80 | (cp >> 6 & 0x3F));
        tmp[2] = (char)(0x80 | (cp & 0x3F));
        k = 3;
    } else {
        tmp[0] = (char)(0xF0 | cp >> 18);
        tmp[1] = (char)(0x80 | (cp >> 12 & 0x3F));
        tmp[2] = (char)(0x80 | (cp >> 6 & 0x3F));
        tmp[3] = (char)(0x80 | (cp & 0x3F));
        k = 4;
    }
    if (buf) {
        if (*n + k >= len) {
            return wf_json_fail(j, "string too long");
        }
        memcpy(buf + *n, tmp, k);
    }
    *n += k;
    return true;
}

/** String at j->p into buf (NULL = skip). */
static bool read_string(wf_json_t *j, char *buf, size_t len)
{
    if (!expect_value(j, WF_JSON_STRING, "expected a string")) {
        return false;
    }
    j->p++; // '"'
    size_t n = 0;
    while (j->p < j->end) {
        const unsigned char c = (unsigned char)*j->p;
        if (c == '"') {
            j->p++;
            if (buf) {
                buf[n] = '\0';
            }
            return true;
        }
        if (c < 0x20) {
            return wf_json_fail(j, "control character in string");
        }
        if (c != '\\') {
            if (buf) {
                if (n + 1 >= len) {
                    return wf_json_fail(j, "string too long");
                }
                buf[n] = (char)c;
            }
            n++;
            j->p++;
            continue;
        }
        if (++j->p >= j->end) {
            break;
        }
        const char e = *j->p++;
        uint32_t cp;
        switch (e) {
        case '"':
        case '\\':
        case '/':
            cp = (uint32_t)e;
            break;
        case 'b':
            cp = '\b';
            break;
        case 'f':
            cp = '\f';
            break;
        case 'n':
            cp = '\n';
            break;
        case 'r':
            cp = '\r';
            break;
        case 't':
            cp = '\t';
            break;
        case 'u':
            if (!read_u16(j, &cp)) {
                return false;
            }
            if (cp >= 0xD800 && cp < 0xDC00) { // high surrogate: a low one must follow
                uint32_t lo;
                if (j->end - j->p < 2 || j->p[0] != '\\' || j->p[1] != 'u') {
                    return wf_json_fail(j, "unpaired surrogate");
                }
                j->p += 2;
                if (!read_u16(j, &lo)) {
                    return false;
                }
                if (lo < 0xDC00 || lo > 0xDFFF) {
                    return wf_json_fail(j, "unpaired surrogate");
                }
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                return wf_json_fail(j, "unpaired surrogate");
            }
            if (cp == 0) {
                return wf_json_fail(j, "NUL in string");
            }
            break;
        default:
            j->p--;
            return wf_json_fail(j, "bad escape");
        }
        if (!put_utf8(j, cp, buf, len, &n)) {
            return false;
        }
    }
    return wf_json_fail(j, "unterminated string");
}

bool wf_json_obj_next(wf_json_t *j, char *key, size_t len)
{
    if (!next_member(j, '}', "expected ',' or '}'")) {
        return false;
    }
    if (wf_json_peek(j) != WF_JSON_STRING) {
        return wf_json_fail(j, "expected a key");
    }
    if (!read_string(j, key, len)) {
        return false;
    }
    skip_ws(j);
    if (j->p >= j->end || *j->p != ':') {
        return wf_json_fail(j, "expected ':'");
    }
    j->p++;
    return true;
}

bool wf_json_string(wf_json_t *j, char *buf, size_t len)
{
    if (len == 0) {
        return wf_json_fail(j, "string too long");
    }
    return read_string(j, buf, len);
}

/** Scan a number at j->p (grammar only). *is_int: no fraction or exponent. */
static bool scan_number(wf_json_t *j, const char **end, bool *is_int)
{
    const char *p = j->p;
    *is_int = true;
    if (p < j->end && *p == '-') {
        p++;
    }
    if (p >= j->end || *p < '0' || *p > '9') {
        return wf_json_fail(j, "bad number");
    }
    if (*p == '0') {
        p++;
    } else {
        while (p < j->end && *p >= '0' && *p <= '9') {
            p++;
        }
    }
    if (p < j->end && *p == '.') {
        *is_int = false;
        p++;
        if (p >= j->end || *p < '0' || *p > '9') {
            return wf_json_fail(j, "bad number");
        }
        while (p < j->end && *p >= '0' && *p <= '9') {
            p++;
        }
    }
    if (p < j->end && (*p == 'e' || *p == 'E')) {
        *is_int = false;
        p++;
        if (p < j->end && (*p == '+' || *p == '-')) {
            p++;
        }
        if (p >= j->end || *p < '0' || *p > '9') {
            return wf_json_fail(j, "bad number");
        }
        while (p < j->end && *p >= '0' && *p <= '9') {
            p++;
        }
    }
    *end = p;
    return true;
}

bool wf_json_int(wf_json_t *j, int32_t *out)
{
    if (!expect_value(j, WF_JSON_NUMBER, "expected an integer")) {
        return false;
    }
    const char *end;
    bool is_int;
    if (!scan_number(j, &end, &is_int)) {
        return false;
    }
    if (!is_int) {
        return wf_json_fail(j, "expected an integer");
    }
    const char *p = j->p;
    const bool neg = *p == '-';
    p += neg;
    int64_t v = 0;
    for (; p < end; p++) {
        v = v * 10 + (*p - '0');
        if (v > (int64_t)INT32_MAX + 1) {
            return wf_json_fail(j, "integer out of range");
        }
    }
    v = neg ? -v : v;
    if (v > INT32_MAX) {
        return wf_json_fail(j, "integer out of range");
    }
    *out = (int32_t)v;
    j->p = end;
    return true;
}

static bool literal(wf_json_t *j, const char *word)
{
    const size_t n = strlen(word);
    if ((size_t)(j->end - j->p) < n || memcmp(j->p, word, n) != 0) {
        return wf_json_fail(j, "bad literal");
    }
    j->p += n;
    return true;
}

bool wf_json_bool(wf_json_t *j, bool *out)
{
    if (!expect_value(j, WF_JSON_BOOL, "expected true or false")) {
        return false;
    }
    *out = *j->p == 't';
    return literal(j, *out ? "true" : "false");
}

bool wf_json_skip(wf_json_t *j)
{
    switch (wf_json_peek(j)) {
    case WF_JSON_OBJECT: {
        if (!wf_json_obj_begin(j)) {
            return false;
        }
        for (;;) {
            if (!next_member(j, '}', "expected ',' or '}'")) {
                return wf_json_ok(j);
            }
            if (wf_json_peek(j) != WF_JSON_STRING) {
                return wf_json_fail(j, "expected a key");
            }
            if (!read_string(j, NULL, 0)) { // key, not kept
                return false;
            }
            skip_ws(j);
            if (j->p >= j->end || *j->p != ':') {
                return wf_json_fail(j, "expected ':'");
            }
            j->p++;
            if (!wf_json_skip(j)) {
                return false;
            }
        }
    }
    case WF_JSON_ARRAY:
        if (!wf_json_arr_begin(j)) {
            return false;
        }
        while (wf_json_arr_next(j)) {
            if (!wf_json_skip(j)) {
                return false;
            }
        }
        return wf_json_ok(j);
    case WF_JSON_STRING:
        return read_string(j, NULL, 0);
    case WF_JSON_NUMBER: {
        const char *end;
        bool is_int;
        if (!scan_number(j, &end, &is_int)) {
            return false;
        }
        j->p = end;
        return true;
    }
    case WF_JSON_BOOL: {
        bool b;
        return wf_json_bool(j, &b);
    }
    case WF_JSON_NULL:
        return literal(j, "null");
    default:
        if (j->err) {
            return false;
        }
        return wf_json_fail(j, j->p >= j->end ? "unexpected end of input" : "expected a value");
    }
}

bool wf_json_finish(wf_json_t *j)
{
    if (j->err) {
        return false;
    }
    skip_ws(j);
    if (j->p != j->end) {
        return wf_json_fail(j, "unexpected data after the end");
    }
    return true;
}
