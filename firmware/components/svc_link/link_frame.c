#include "link_frame.h"

#include <string.h>

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    put_u16(p, (uint16_t)v);
    put_u16(p + 2, (uint16_t)(v >> 16));
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t get_u32(const uint8_t *p)
{
    return get_u16(p) | ((uint32_t)get_u16(p + 2) << 16);
}

bool link_frame_parse(const uint8_t *data, size_t len, link_frame_t *out)
{
    if (!data || len < LINK_HDR_LEN || data[3] > LINK_CHAN_BULK) {
        return false;
    }
    out->frag = data[0] >> 6;
    out->version = data[0] & 0x3F;
    out->seq = get_u16(data + 1);
    out->chan = data[3];
    out->payload = data + LINK_HDR_LEN;
    out->len = len - LINK_HDR_LEN;
    return true;
}

size_t link_frame_build(uint8_t *out, size_t cap, link_frag_t frag, uint16_t seq, link_chan_t chan,
                        const uint8_t *payload, size_t len)
{
    if (cap < LINK_HDR_LEN + len) {
        return 0;
    }
    out[0] = (uint8_t)((frag << 6) | LINK_PROTO_MAJOR);
    put_u16(out + 1, seq);
    out[3] = (uint8_t)chan;
    if (len) {
        memcpy(out + LINK_HDR_LEN, payload, len);
    }
    return LINK_HDR_LEN + len;
}

bool link_ctrl_send(link_seq_t *seq, const uint8_t *msg, size_t len, size_t max_frame, link_tx_fn tx, void *ctx)
{
    if (len == 0 || len > LINK_CTRL_MAX_MSG || max_frame < LINK_MIN_FRAME) {
        return false;
    }
    uint8_t frame[LINK_MAX_FRAME];
    if (max_frame > sizeof frame) {
        max_frame = sizeof frame;
    }
    const size_t room = max_frame - LINK_HDR_LEN;

    if (len <= room) {
        const size_t n = link_frame_build(frame, sizeof frame, LINK_FRAG_SINGLE, seq->next, LINK_CHAN_CONTROL, msg, len);
        if (!tx(ctx, LINK_CHAN_CONTROL, frame, n)) {
            return false;
        }
        seq->next++;
        return true;
    }

    size_t off = 0;
    while (off < len) {
        uint8_t body[LINK_MAX_FRAME];
        size_t body_len;
        link_frag_t frag;
        if (off == 0) {
            // First fragment: uint16 total_len, then data.
            put_u16(body, (uint16_t)len);
            body_len = room - 2;
            memcpy(body + 2, msg, body_len);
            body_len += 2;
            off = room - 2;
            frag = LINK_FRAG_FIRST;
        } else {
            body_len = len - off < room ? len - off : room;
            memcpy(body, msg + off, body_len);
            off += body_len;
            frag = off >= len ? LINK_FRAG_LAST : LINK_FRAG_MIDDLE;
        }
        const size_t n = link_frame_build(frame, sizeof frame, frag, seq->next, LINK_CHAN_CONTROL, body, body_len);
        if (!tx(ctx, LINK_CHAN_CONTROL, frame, n)) {
            return false;
        }
        seq->next++;
    }
    return true;
}

size_t link_bulk_build(uint8_t *out, size_t cap, link_seq_t *seq, uint32_t transfer_id, uint32_t offset,
                       const uint8_t *data, size_t len)
{
    if (cap < LINK_HDR_LEN + LINK_BULK_HDR_LEN + len) {
        return 0;
    }
    out[0] = (uint8_t)((LINK_FRAG_SINGLE << 6) | LINK_PROTO_MAJOR);
    put_u16(out + 1, seq->next++);
    out[3] = LINK_CHAN_BULK;
    put_u32(out + LINK_HDR_LEN, transfer_id);
    put_u32(out + LINK_HDR_LEN + 4, offset);
    if (len) {
        memcpy(out + LINK_HDR_LEN + LINK_BULK_HDR_LEN, data, len);
    }
    return LINK_HDR_LEN + LINK_BULK_HDR_LEN + len;
}

bool link_bulk_parse(const link_frame_t *f, link_bulk_chunk_t *out)
{
    if (f->chan != LINK_CHAN_BULK || f->len < LINK_BULK_HDR_LEN) {
        return false;
    }
    out->transfer_id = get_u32(f->payload);
    out->offset = get_u32(f->payload + 4);
    out->data = f->payload + LINK_BULK_HDR_LEN;
    out->len = f->len - LINK_BULK_HDR_LEN;
    return true;
}

void link_reasm_init(link_reasm_t *r, uint8_t *buf, size_t cap)
{
    memset(r, 0, sizeof *r);
    r->buf = buf;
    r->cap = cap < LINK_CTRL_MAX_MSG ? cap : LINK_CTRL_MAX_MSG;
}

void link_reasm_reset(link_reasm_t *r)
{
    r->active = false;
    r->have_seq = false;
    r->total = r->got = 0;
}

static link_rx_t discard(link_reasm_t *r)
{
    r->active = false;
    r->dropped++;
    return LINK_RX_DROPPED;
}

link_rx_t link_reasm_push(link_reasm_t *r, const link_frame_t *f, const uint8_t **msg, size_t *len)
{
    const bool starts = f->frag == LINK_FRAG_SINGLE || f->frag == LINK_FRAG_FIRST;

    if (r->have_seq && f->seq != r->expect) {
        if ((uint16_t)(f->seq + 1) == r->expect) {
            r->dups++; // the previous frame again (a retransmission): no state change
            return LINK_RX_NONE;
        }
        // Gap: whatever was in progress is lost.
        const bool lost = r->active;
        r->active = false;
        if (!starts) {
            r->have_seq = false; // wait for a frame that starts a message
            if (lost) {
                r->dropped++;
            }
            return LINK_RX_DROPPED;
        }
        if (lost) {
            r->dropped++;
        }
    } else if (!r->have_seq && !starts) {
        return LINK_RX_DROPPED; // joined mid-message: wait for the next start
    }
    r->have_seq = true;
    r->expect = (uint16_t)(f->seq + 1);

    switch (f->frag) {
    case LINK_FRAG_SINGLE:
        r->active = false;
        if (f->len == 0 || f->len > r->cap) {
            return discard(r);
        }
        memcpy(r->buf, f->payload, f->len);
        *msg = r->buf;
        *len = f->len;
        r->messages++;
        return LINK_RX_MESSAGE;

    case LINK_FRAG_FIRST: {
        if (r->active) {
            r->dropped++; // a new message while one was incomplete (the gap check missed it)
        }
        r->active = false;
        if (f->len < 3) {
            return discard(r);
        }
        const size_t total = (size_t)(f->payload[0] | (f->payload[1] << 8));
        const size_t n = f->len - 2;
        if (total == 0 || total > r->cap || n >= total) {
            return discard(r); // does not fit, or a "first" that already holds everything
        }
        memcpy(r->buf, f->payload + 2, n);
        r->total = total;
        r->got = n;
        r->active = true;
        return LINK_RX_NONE;
    }

    default: // MIDDLE, LAST
        if (!r->active) {
            return discard(r);
        }
        if (r->got + f->len > r->total) {
            return discard(r);
        }
        memcpy(r->buf + r->got, f->payload, f->len);
        r->got += f->len;
        if (f->frag == LINK_FRAG_MIDDLE) {
            if (r->got == r->total) {
                return discard(r); // "middle" that completes the message: sender bug
            }
            return LINK_RX_NONE;
        }
        r->active = false;
        if (r->got != r->total) {
            return discard(r);
        }
        *msg = r->buf;
        *len = r->total;
        r->messages++;
        return LINK_RX_MESSAGE;
    }
}
