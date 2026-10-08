#include "link_bulk.h"

#include <string.h>

static int32_t ms_diff(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b);
}

/* ---------------------------------------------------------------- receiver */

static link_bulk_rx_event_t rx_finish(link_bulk_rx_t *rx)
{
    rx->verified = rx->sink.finish ? rx->sink.finish(rx->sink.ctx) : true;
    rx->state = LINK_BULK_RX_DONE;
    return LINK_BULK_RX_COMPLETE;
}

bool link_bulk_rx_begin(link_bulk_rx_t *rx, uint32_t id, uint32_t size, uint32_t window, uint32_t resume_offset,
                        const link_bulk_sink_t *sink)
{
    if (window == 0 || window > LINK_BULK_WINDOW_MAX || resume_offset > size || !sink || !sink->write) {
        return false;
    }
    memset(rx, 0, sizeof *rx);
    rx->id = id;
    rx->size = size;
    rx->window = window;
    rx->next = resume_offset;
    rx->sink = *sink;
    rx->state = LINK_BULK_RX_RECEIVING;
    if (rx->next == rx->size) {
        (void)rx_finish(rx); // empty, or fully received before: the caller sends TransferEnd
    }
    return true;
}

// Ack the in-order progress after a gap or duplicates. One ack per burst: the sender resends a
// whole window, so the rest of that window is quiet; if a full window of chunks passes without an
// in-order one the ack itself was lost and goes out again.
static link_bulk_rx_event_t rx_resync_ack(link_bulk_rx_t *rx)
{
    if (!rx->resync_sent) {
        rx->resync_sent = true;
        rx->suppressed = 0;
        rx->in_window = 0;
        return LINK_BULK_RX_ACK;
    }
    if (++rx->suppressed >= rx->window) {
        rx->suppressed = 0;
        return LINK_BULK_RX_ACK;
    }
    return LINK_BULK_RX_NONE;
}

link_bulk_rx_event_t link_bulk_rx_chunk(link_bulk_rx_t *rx, uint32_t id, uint32_t offset, const uint8_t *data,
                                        size_t len)
{
    if (id != rx->id) {
        return LINK_BULK_RX_NONE;
    }
    if (rx->state == LINK_BULK_RX_DONE) {
        return LINK_BULK_RX_COMPLETE; // the sender missed TransferEnd and is resending: say it again
    }
    if (rx->state != LINK_BULK_RX_RECEIVING) {
        return LINK_BULK_RX_NONE;
    }
    if (offset > rx->next) { // a chunk went missing: tell the sender
        rx->gap_chunks++;
        return rx_resync_ack(rx);
    }
    if (offset < rx->next || len == 0 || len > rx->size - rx->next) {
        // Already stored (a rewind after a lost ack, or a stale chunk): re-ack so a sender that
        // missed our ack moves on. Empty or overlong chunks are ignored.
        if (offset < rx->next && len != 0) {
            rx->dup_chunks++;
            return rx_resync_ack(rx);
        }
        return LINK_BULK_RX_NONE;
    }

    if (!rx->sink.write(rx->sink.ctx, offset, data, len)) {
        rx->state = LINK_BULK_RX_FAILED;
        return LINK_BULK_RX_ERROR;
    }
    rx->next += (uint32_t)len;
    rx->resync_sent = false;
    rx->in_window++;
    if (rx->next == rx->size) {
        return rx_finish(rx);
    }
    if (rx->in_window >= rx->window) {
        rx->in_window = 0;
        return LINK_BULK_RX_ACK;
    }
    return LINK_BULK_RX_NONE;
}

void link_bulk_rx_abort(link_bulk_rx_t *rx)
{
    if (rx->state == LINK_BULK_RX_RECEIVING) {
        rx->state = LINK_BULK_RX_FAILED;
    }
}

/* ------------------------------------------------------------------ sender */

bool link_bulk_tx_begin(link_bulk_tx_t *tx, uint32_t id, uint32_t size, uint32_t window, uint32_t chunk_size,
                        uint32_t start_offset, uint32_t now_ms)
{
    if (window == 0 || window > LINK_BULK_WINDOW_MAX || chunk_size == 0 || start_offset > size) {
        return false;
    }
    memset(tx, 0, sizeof *tx);
    tx->id = id;
    tx->size = size;
    tx->window = window;
    tx->chunk = chunk_size;
    tx->acked = tx->sent = start_offset;
    tx->deadline_ms = now_ms + LINK_BULK_ACK_TIMEOUT_MS;
    // Nothing left to send (empty file, or fully resumed): the receiver finishes on the ack.
    tx->state = start_offset == size ? LINK_BULK_TX_WAIT_END : LINK_BULK_TX_SENDING;
    return true;
}

bool link_bulk_tx_next(const link_bulk_tx_t *tx, uint32_t *offset, size_t *len)
{
    if (tx->state != LINK_BULK_TX_SENDING || tx->sent >= tx->size) {
        return false;
    }
    const uint32_t left = tx->size - tx->sent;
    *offset = tx->sent;
    *len = left < tx->chunk ? left : tx->chunk;
    return true;
}

void link_bulk_tx_sent(link_bulk_tx_t *tx, uint32_t now_ms)
{
    uint32_t off;
    size_t len;
    if (!link_bulk_tx_next(tx, &off, &len)) {
        return;
    }
    tx->sent += (uint32_t)len;
    tx->in_window++;
    tx->chunks_sent++;
    tx->last_off = off;
    if (tx->in_window >= tx->window || tx->sent >= tx->size) {
        tx->state = LINK_BULK_TX_WAIT_ACK;
        tx->deadline_ms = now_ms + LINK_BULK_ACK_TIMEOUT_MS;
    }
}

// Go back to `offset` and send a fresh window from there.
static void rewind_to(link_bulk_tx_t *tx, uint32_t offset, uint32_t now_ms)
{
    if (tx->sent > offset) {
        tx->chunks_resent += (tx->sent - offset + tx->chunk - 1) / tx->chunk;
    }
    tx->sent = offset;
    tx->in_window = 0;
    tx->state = LINK_BULK_TX_SENDING;
    tx->deadline_ms = now_ms + LINK_BULK_ACK_TIMEOUT_MS;
}

void link_bulk_tx_on_status(link_bulk_tx_t *tx, uint32_t next_offset, uint32_t now_ms)
{
    if (tx->state != LINK_BULK_TX_SENDING && tx->state != LINK_BULK_TX_WAIT_ACK && tx->state != LINK_BULK_TX_WAIT_END) {
        return;
    }
    if (next_offset > tx->size || next_offset < tx->acked) {
        return; // nonsense or a stale ack
    }
    tx->stalls = 0;
    tx->acked = next_offset;
    tx->deadline_ms = now_ms + LINK_BULK_ACK_TIMEOUT_MS;
    if (next_offset == tx->size) {
        tx->sent = tx->size;
        tx->state = LINK_BULK_TX_WAIT_END;
        return;
    }
    if (next_offset == tx->sent && tx->state == LINK_BULK_TX_WAIT_ACK) {
        tx->in_window = 0; // the whole window landed: next one
        tx->state = LINK_BULK_TX_SENDING;
    } else if (next_offset != tx->sent) {
        rewind_to(tx, next_offset, now_ms); // the receiver is behind: resend from its offset
    }
    // next_offset == sent while still SENDING: an ack for an earlier window (we keep going).
}

void link_bulk_tx_on_end(link_bulk_tx_t *tx, bool verified)
{
    if (tx->state == LINK_BULK_TX_IDLE || tx->state == LINK_BULK_TX_DONE || tx->state == LINK_BULK_TX_FAILED) {
        return;
    }
    tx->state = verified ? LINK_BULK_TX_DONE : LINK_BULK_TX_FAILED;
}

uint32_t link_bulk_tx_poll(link_bulk_tx_t *tx, uint32_t now_ms)
{
    if (tx->state != LINK_BULK_TX_WAIT_ACK && tx->state != LINK_BULK_TX_WAIT_END) {
        return UINT32_MAX; // sending: the caller is busy anyway; idle/done: nothing to time
    }
    if (ms_diff(now_ms, tx->deadline_ms) >= 0) {
        if (++tx->stalls >= LINK_BULK_MAX_STALLS) {
            tx->state = LINK_BULK_TX_FAILED;
            return UINT32_MAX;
        }
        // Lost last chunk or lost ack (or TransferEnd): resend from what is confirmed.
        if (tx->state == LINK_BULK_TX_WAIT_END) {
            // Everything was acked, so only TransferEnd is missing: the last chunk makes the
            // receiver repeat it. Nothing was sent this session (fully resumed): just wait again.
            if (tx->chunks_sent) {
                rewind_to(tx, tx->last_off, now_ms);
            } else {
                tx->deadline_ms = now_ms + LINK_BULK_ACK_TIMEOUT_MS;
            }
        } else {
            rewind_to(tx, tx->acked, now_ms);
        }
    }
    return tx->state == LINK_BULK_TX_SENDING ? UINT32_MAX : (uint32_t)ms_diff(tx->deadline_ms, now_ms);
}

void link_bulk_tx_abort(link_bulk_tx_t *tx)
{
    if (tx->state != LINK_BULK_TX_DONE) {
        tx->state = LINK_BULK_TX_FAILED;
    }
}
