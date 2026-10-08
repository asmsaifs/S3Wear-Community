#include "s3w_mailbox.h"

#include <string.h>

void s3w_mbox_init(s3w_mbox_t *m, s3w_mbox_msg_t *slots, uint16_t cap)
{
    memset(m, 0, sizeof *m);
    m->slots = slots;
    m->cap = cap;
}

bool s3w_mbox_push(s3w_mbox_t *m, s3w_mbox_fn_t fn, void *ctx, int32_t arg, const void *data, size_t len)
{
    if (!fn || len > S3W_MBOX_PAYLOAD_MAX || (len && !data) || m->count >= m->cap) {
        m->dropped++;
        return false;
    }
    s3w_mbox_msg_t *s = &m->slots[(m->head + m->count) % m->cap];
    s->fn = fn;
    s->ctx = ctx;
    s->arg = arg;
    s->len = (uint16_t)len;
    if (len) {
        memcpy(s->data, data, len);
    }
    m->count++;
    if (m->count > m->high_water) {
        m->high_water = m->count;
    }
    return true;
}

bool s3w_mbox_pop(s3w_mbox_t *m, s3w_mbox_msg_t *out)
{
    if (m->count == 0) {
        return false;
    }
    const s3w_mbox_msg_t *s = &m->slots[m->head];
    out->fn = s->fn;
    out->ctx = s->ctx;
    out->arg = s->arg;
    out->len = s->len;
    memcpy(out->data, s->data, s->len);
    m->head = (uint16_t)((m->head + 1) % m->cap);
    m->count--;
    return true;
}
