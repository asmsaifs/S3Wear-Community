// Windowed bulk transfer engine (docs/06 §6), receiver and sender. Pure C: data goes through
// callbacks, time comes from the caller, the wire messages (TransferBegin / Status / End) are
// built by svc_link from what these state machines report.
//
// Protocol: the sender streams chunks and waits for TransferStatus{next_offset} after every
// `window` chunks and after the last one. The receiver acks the in-order progress; on a gap
// (a lost chunk) it acks next_offset once and the sender rewinds there, so loss costs at most
// a window. A sender that hears nothing for LINK_BULK_ACK_TIMEOUT_MS rewinds to the last
// acked offset too (lost last chunk or lost ack). Resume: the receiver answers TransferBegin
// with the offset it already has (same sha256) and the sender starts there.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LINK_BULK_WINDOW_MAX 64
#define LINK_BULK_ACK_TIMEOUT_MS 3000
#define LINK_BULK_MAX_STALLS 5 // consecutive ack timeouts before the sender gives up

/* ---------------------------------------------------------------- receiver */

typedef struct {
    /** Store data at offset (always the next unwritten offset). false = storage failed. */
    bool (*write)(void *ctx, uint32_t offset, const uint8_t *data, size_t len);
    /** All bytes arrived: check the hash / commit. true = verified. */
    bool (*finish)(void *ctx);
    void *ctx;
} link_bulk_sink_t;

typedef enum {
    LINK_BULK_RX_IDLE,
    LINK_BULK_RX_RECEIVING,
    LINK_BULK_RX_DONE,
    LINK_BULK_RX_FAILED,
} link_bulk_rx_state_t;

typedef struct {
    link_bulk_rx_state_t state;
    uint32_t id;
    uint32_t size;
    uint32_t window;
    uint32_t next; // every byte below is stored
    uint32_t in_window;
    bool resync_sent; // a gap/duplicate ack went out; mostly quiet until an in-order chunk
    uint32_t suppressed; // chunks ignored since that ack
    bool verified;
    link_bulk_sink_t sink;
    uint32_t dup_chunks, gap_chunks;
} link_bulk_rx_t;

typedef enum {
    LINK_BULK_RX_NONE,     // chunk stored (or ignored), nothing to send
    LINK_BULK_RX_ACK,      // send TransferStatus{id, next_offset}
    LINK_BULK_RX_COMPLETE, // all bytes in: send TransferEnd{id, verified}
    LINK_BULK_RX_ERROR,    // storage failed: send TransferEnd{id, status INTERNAL}; transfer is dead
} link_bulk_rx_event_t;

/**
 * resume_offset ≤ size: bytes already stored from an earlier try (0 for a fresh transfer). false:
 * bad parameters. If nothing is left to receive the state is DONE at once: send TransferEnd.
 */
bool link_bulk_rx_begin(link_bulk_rx_t *rx, uint32_t id, uint32_t size, uint32_t window, uint32_t resume_offset,
                        const link_bulk_sink_t *sink);
/** Offset to report in the TransferStatus that answers TransferBegin. */
static inline uint32_t link_bulk_rx_next(const link_bulk_rx_t *rx)
{
    return rx->next;
}
link_bulk_rx_event_t link_bulk_rx_chunk(link_bulk_rx_t *rx, uint32_t id, uint32_t offset, const uint8_t *data,
                                        size_t len);
/** Abandon (link lost, user cancel). */
void link_bulk_rx_abort(link_bulk_rx_t *rx);

/* ------------------------------------------------------------------ sender */

typedef enum {
    LINK_BULK_TX_IDLE,
    LINK_BULK_TX_SENDING,  // chunks to send (link_bulk_tx_next)
    LINK_BULK_TX_WAIT_ACK, // window sent, waiting for TransferStatus
    LINK_BULK_TX_WAIT_END, // everything acked, waiting for TransferEnd
    LINK_BULK_TX_DONE,
    LINK_BULK_TX_FAILED,
} link_bulk_tx_state_t;

typedef struct {
    link_bulk_tx_state_t state;
    uint32_t id;
    uint32_t size;
    uint32_t window;
    uint32_t chunk;
    uint32_t acked;  // receiver has every byte below
    uint32_t sent;   // next offset to send
    uint32_t in_window;
    uint32_t deadline_ms;
    uint8_t stalls;
    uint32_t last_off; // offset of the last chunk sent
    uint32_t chunks_sent, chunks_resent;
} link_bulk_tx_t;

/** start_offset: next_offset from the TransferStatus that answered TransferBegin. */
bool link_bulk_tx_begin(link_bulk_tx_t *tx, uint32_t id, uint32_t size, uint32_t window, uint32_t chunk_size,
                        uint32_t start_offset, uint32_t now_ms);
/** Next chunk to put on the wire. false: nothing to send now (waiting for an ack, or finished). */
bool link_bulk_tx_next(const link_bulk_tx_t *tx, uint32_t *offset, size_t *len);
/** The chunk from link_bulk_tx_next() went out. */
void link_bulk_tx_sent(link_bulk_tx_t *tx, uint32_t now_ms);
/** TransferStatus for this transfer arrived. */
void link_bulk_tx_on_status(link_bulk_tx_t *tx, uint32_t next_offset, uint32_t now_ms);
/** TransferEnd arrived: verified or not. */
void link_bulk_tx_on_end(link_bulk_tx_t *tx, bool verified);
/**
 * Handle the ack timeout. Returns ms until the next deadline, UINT32_MAX when none. After
 * LINK_BULK_MAX_STALLS timeouts in a row the state becomes FAILED.
 */
uint32_t link_bulk_tx_poll(link_bulk_tx_t *tx, uint32_t now_ms);
void link_bulk_tx_abort(link_bulk_tx_t *tx);

#ifdef __cplusplus
}
#endif
