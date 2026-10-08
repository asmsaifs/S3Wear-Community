// S3W Link framing (docs/06-ble-protocol.md §2): 4-byte frame header, control-channel
// fragmentation and reassembly, bulk chunk header. Pure C, no ESP-IDF: also built by
// firmware/host_test.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LINK_PROTO_MAJOR 1
#define LINK_HDR_LEN 4
/** Largest control message (serialized Envelope), docs/06 §2. */
#define LINK_CTRL_MAX_MSG (16 * 1024)
/** Bulk chunk header: uint32 transfer_id, uint32 offset (little-endian). */
#define LINK_BULK_HDR_LEN 8
/** Smallest ATT payload a frame can use: header + total_len + 1 byte. */
#define LINK_MIN_FRAME 8
/** Largest frame: an attribute value is at most 512 bytes (Core spec Vol 3 Part F §3.2.9), so
 *  MTU 517 still carries 512 per write / notification. Android drops longer notifications. */
#define LINK_MAX_FRAME 512

/** ATT payload per write / notification for a negotiated MTU. */
static inline size_t link_max_frame(uint16_t mtu)
{
    const size_t n = mtu > 3 ? (size_t)mtu - 3 : 0;
    return n < LINK_MAX_FRAME ? n : LINK_MAX_FRAME;
}

typedef enum {
    LINK_FRAG_SINGLE = 0,
    LINK_FRAG_FIRST = 1,
    LINK_FRAG_MIDDLE = 2,
    LINK_FRAG_LAST = 3,
} link_frag_t;

typedef enum {
    LINK_CHAN_CONTROL = 0,
    LINK_CHAN_BULK = 1,
} link_chan_t;

typedef struct {
    uint8_t frag;    // link_frag_t
    uint8_t version; // protocol major
    uint16_t seq;
    uint8_t chan;    // link_chan_t
    const uint8_t *payload;
    size_t len;
} link_frame_t;

/** false: shorter than the header or an unknown channel. Does not check the version. */
bool link_frame_parse(const uint8_t *data, size_t len, link_frame_t *out);

/** Writes header + payload. Returns the frame length, 0 if cap is too small. */
size_t link_frame_build(uint8_t *out, size_t cap, link_frag_t frag, uint16_t seq, link_chan_t chan,
                        const uint8_t *payload, size_t len);

/** Next sequence number for one direction of one channel. */
typedef struct {
    uint16_t next;
} link_seq_t;

/** Send one ATT payload; false = not sent (the fragmenter stops). */
typedef bool (*link_tx_fn)(void *ctx, link_chan_t chan, const uint8_t *frame, size_t len);

/**
 * Split a control message into frames of at most max_frame bytes (ATT MTU − 3) and send
 * them in order, consuming seq numbers. Returns false if the message is too big, max_frame
 * is below LINK_MIN_FRAME or tx failed; seq numbers of frames already sent stay consumed,
 * so the receiver sees a gap and drops the partial message.
 */
bool link_ctrl_send(link_seq_t *seq, const uint8_t *msg, size_t len, size_t max_frame, link_tx_fn tx, void *ctx);

/** Frame a bulk chunk (single frame) into out. Returns the length, 0 if cap is too small. */
size_t link_bulk_build(uint8_t *out, size_t cap, link_seq_t *seq, uint32_t transfer_id, uint32_t offset,
                       const uint8_t *data, size_t len);

typedef struct {
    uint32_t transfer_id;
    uint32_t offset;
    const uint8_t *data;
    size_t len;
} link_bulk_chunk_t;

/** Parse a bulk-channel frame payload (after the frame header). */
bool link_bulk_parse(const link_frame_t *f, link_bulk_chunk_t *out);

typedef struct {
    uint8_t *buf; // cap bytes, caller-owned (PSRAM on the watch)
    size_t cap;
    size_t total;
    size_t got;
    uint16_t expect; // next sequence number
    bool have_seq;   // expect is valid
    bool active;     // a multi-frame message is in progress
    uint32_t messages;
    uint32_t dropped; // messages discarded (gap, overrun, too big)
    uint32_t dups;    // duplicate frames ignored
} link_reasm_t;

typedef enum {
    LINK_RX_NONE,    // consumed, nothing to deliver yet
    LINK_RX_MESSAGE, // a complete message is in *msg / *len, valid until the next push
    LINK_RX_DROPPED, // the frame or the message in progress was discarded
} link_rx_t;

void link_reasm_init(link_reasm_t *r, uint8_t *buf, size_t cap);
/** Drop any message in progress and forget the sequence (new connection). */
void link_reasm_reset(link_reasm_t *r);

/**
 * Feed one control-channel frame. On a sequence gap the message in progress is discarded
 * and the sender's request times out and retries (docs/06 §2); a frame that starts a
 * message (SINGLE / FIRST) resynchronises. A repeat of the previous frame is ignored.
 */
link_rx_t link_reasm_push(link_reasm_t *r, const link_frame_t *f, const uint8_t **msg, size_t *len);

#ifdef __cplusplus
}
#endif
