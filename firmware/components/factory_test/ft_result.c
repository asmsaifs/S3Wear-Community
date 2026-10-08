#include "ft_result.h"

#include <stdarg.h>
#include <stdio.h>

const char *ft_status_str(ft_status_t s)
{
    switch (s) {
    case FT_PENDING:
        return "PENDING";
    case FT_RUNNING:
        return "RUNNING";
    case FT_PASS:
        return "PASS";
    case FT_FAIL:
        return "FAIL";
    case FT_SKIP:
        return "SKIP";
    default:
        return "?";
    }
}

bool ft_overall_pass(const ft_result_t *r, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (r[i].status != FT_PASS && r[i].status != FT_SKIP) {
            return false;
        }
    }
    return true;
}

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
} out_t;

static void put_c(out_t *o, char c)
{
    if (o->len + 1 < o->cap) {
        o->buf[o->len] = c;
    }
    o->len++;
}

static void put_s(out_t *o, const char *s)
{
    while (*s) {
        put_c(o, *s++);
    }
}

static void put_json_str(out_t *o, const char *s)
{
    put_c(o, '"');
    for (; s && *s; s++) {
        const unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            put_c(o, '\\');
            put_c(o, (char)c);
        } else if (c < 0x20) {
            char esc[8];
            snprintf(esc, sizeof esc, "\\u%04x", c);
            put_s(o, esc);
        } else {
            put_c(o, (char)c);
        }
    }
    put_c(o, '"');
}

size_t ft_json_write(char *buf, size_t cap, const char *fw_version, const ft_result_t *r, size_t n)
{
    out_t o = {buf, cap, 0};
    size_t skipped = 0;
    for (size_t i = 0; i < n; i++) {
        skipped += r[i].status == FT_SKIP;
    }
    char num[24];
    put_s(&o, "{\"factory_test\":{\"fw\":");
    put_json_str(&o, fw_version);
    put_s(&o, ",\"pass\":");
    put_s(&o, ft_overall_pass(r, n) ? "true" : "false");
    snprintf(num, sizeof num, "%u", (unsigned)skipped);
    put_s(&o, ",\"skipped\":");
    put_s(&o, num);
    put_s(&o, ",\"results\":[");
    for (size_t i = 0; i < n; i++) {
        put_s(&o, i ? ",{\"name\":" : "{\"name\":");
        put_json_str(&o, r[i].name);
        put_s(&o, ",\"status\":");
        put_json_str(&o, ft_status_str(r[i].status));
        put_s(&o, ",\"detail\":");
        put_json_str(&o, r[i].detail);
        put_c(&o, '}');
    }
    put_s(&o, "]}}");
    if (cap > 0) {
        buf[o.len < cap ? o.len : cap - 1] = '\0';
    }
    return o.len;
}
