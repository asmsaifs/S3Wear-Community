// FACE_CONFIG string codec (wf_cfg.h). Pure C.
#include "wf_cfg.h"

#include <stdio.h>
#include <string.h>

static bool token_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
}

static bool token_ok(const char *s)
{
    size_t n = 0;
    for (; s && token_char(s[n]); n++) {
    }
    return s && n > 0 && n < WF_CFG_TOKEN_MAX && s[n] == '\0';
}

/** Copy the token at *p into out and advance; false if empty or too long. */
static bool take_token(const char **p, char out[WF_CFG_TOKEN_MAX])
{
    size_t n = 0;
    while (token_char((*p)[n])) {
        n++;
    }
    if (n == 0 || n >= WF_CFG_TOKEN_MAX) {
        return false;
    }
    memcpy(out, *p, n);
    out[n] = '\0';
    *p += n;
    return true;
}

static const char *skip_to(const char *p, const char *stops)
{
    while (*p && strchr(stops, *p) == NULL) {
        p++;
    }
    return p;
}

int wf_cfg_parse(const char *s, wf_cfg_item_cb_t cb, void *ctx)
{
    int bad = 0;
    const char *p = s ? s : "";
    while (*p) {
        char face[WF_CFG_TOKEN_MAX];
        if (*p == ';') { // empty entry
            p++;
            continue;
        }
        if (!take_token(&p, face) || *p != ':') {
            bad++;
            p = skip_to(p, ";");
            continue;
        }
        p++;
        while (*p && *p != ';') {
            char key[WF_CFG_TOKEN_MAX];
            char value[WF_CFG_TOKEN_MAX];
            if (*p == ',') { // empty item
                p++;
                continue;
            }
            if (take_token(&p, key) && *p == '=' && (p++, take_token(&p, value)) && (*p == ',' || *p == ';' || !*p)) {
                if (!cb || !cb(face, key, value, ctx)) {
                    bad++;
                }
            } else {
                bad++;
                p = skip_to(p, ",;");
            }
            if (*p == ',') {
                p++;
            }
        }
    }
    return bad;
}

void wf_cfg_writer_init(wf_cfg_writer_t *w, char *buf, size_t cap)
{
    memset(w, 0, sizeof *w);
    w->buf = buf;
    w->cap = cap;
    if (cap > 0) {
        buf[0] = '\0';
    }
}

bool wf_cfg_put(wf_cfg_writer_t *w, const char *face, const char *key, const char *value)
{
    if (!token_ok(face) || !token_ok(key) || !token_ok(value)) {
        return false;
    }
    const bool same_face = w->need > 0 && strcmp(w->face, face) == 0;
    char item[3 * WF_CFG_TOKEN_MAX + 4];
    const int n = same_face ? snprintf(item, sizeof item, ",%s=%s", key, value)
                            : snprintf(item, sizeof item, "%s%s:%s=%s", w->need > 0 ? ";" : "", face, key, value);
    const bool fits = w->len == w->need && w->len + (size_t)n < w->cap; // nothing dropped before, room for NUL
    w->need += (size_t)n;
    snprintf(w->face, sizeof w->face, "%s", face);
    if (!fits) {
        return false;
    }
    memcpy(w->buf + w->len, item, (size_t)n + 1);
    w->len += (size_t)n;
    return true;
}

void wf_cfg_color_str(uint32_t rgb, char *buf)
{
    snprintf(buf, 7, "%06lx", (unsigned long)(rgb & 0xFFFFFFu));
}

bool wf_cfg_color_parse(const char *s, uint32_t *rgb)
{
    uint32_t v = 0;
    int i = 0;
    for (; s && i < 6; i++) {
        const char c = s[i];
        const int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
        if (d < 0) {
            return false;
        }
        v = v << 4 | (uint32_t)d;
    }
    if (s == NULL || s[6] != '\0') {
        return false;
    }
    *rgb = v;
    return true;
}
