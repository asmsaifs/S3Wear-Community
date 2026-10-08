// Request/reply bookkeeping for the control channel (docs/06 §3): ids, a reply timeout of
// 5 s, up to 3 retries with the same id, and receiver-side dedupe so a retried request
// does not run twice. Works on opaque serialized messages and never reads a clock: the
// caller passes now_ms and sleeps until link_rpc_poll() says the next deadline. Pure C,
// not thread-safe (svc_link owns it from one task).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LINK_RPC_TIMEOUT_MS 5000
#define LINK_RPC_RETRIES 3     // after the first send: 4 transmissions in all
#define LINK_RPC_MAX_PENDING 4 // requests in flight
#define LINK_RPC_DEDUPE 8      // peer request ids remembered
#define LINK_RPC_REPLY_CACHE_MAX 1024 // larger replies are not cached (duplicate request is then dropped)

typedef struct {
    void *(*alloc)(size_t n);
    void (*release)(void *p);
} link_rpc_mem_t;

typedef enum {
    LINK_RPC_OK,           // reply received
    LINK_RPC_TIMEOUT,      // no reply after the last retry
    LINK_RPC_DISCONNECTED, // link lost or reset
    LINK_RPC_FAILED,       // never sent: no free slot, out of memory or the first transmission failed
} link_rpc_result_t;

/** reply is whatever was passed to link_rpc_on_reply() (NULL unless result is OK). */
typedef void (*link_rpc_done_fn)(void *user, link_rpc_result_t result, const void *reply);

/** Transmit a serialized message; false = could not send (counts as a lost transmission). */
typedef bool (*link_rpc_tx_fn)(void *ctx, const uint8_t *msg, size_t len);

typedef struct {
    uint32_t id; // 0 = free slot
    uint8_t *msg;
    size_t len;
    uint8_t sent;        // transmissions so far
    uint32_t deadline_ms;
    link_rpc_done_fn done;
    void *user;
} link_rpc_req_t;

typedef struct {
    uint32_t id; // 0 = free
    uint8_t *reply;
    size_t reply_len;
} link_rpc_seen_t;

typedef struct {
    link_rpc_mem_t mem;
    link_rpc_tx_fn tx;
    void *tx_ctx;
    uint32_t next_id;
    link_rpc_req_t pending[LINK_RPC_MAX_PENDING];
    link_rpc_seen_t seen[LINK_RPC_DEDUPE];
    uint8_t seen_head;
    uint32_t retries; // retransmissions since init
    uint32_t timeouts;
} link_rpc_t;

void link_rpc_init(link_rpc_t *r, link_rpc_mem_t mem, link_rpc_tx_fn tx, void *tx_ctx);

/** Non-zero id for the next request (wraps, skips 0). */
uint32_t link_rpc_next_id(link_rpc_t *r);

/**
 * Track a request (its envelope carries id) and send it. Returns false, tracking nothing, if
 * all slots are busy, memory ran out or the first transmission failed. done runs from
 * link_rpc_on_reply / link_rpc_poll / link_rpc_reset, exactly once.
 */
bool link_rpc_start(link_rpc_t *r, uint32_t id, const uint8_t *msg, size_t len, uint32_t now_ms, link_rpc_done_fn done,
                    void *user);

/** A reply to request `id` arrived. false: not (or no longer) pending, e.g. a late duplicate. */
bool link_rpc_on_reply(link_rpc_t *r, uint32_t id, const void *reply);

/**
 * Retransmit what is due, expire what ran out of retries. Returns the delay until the next
 * deadline in ms, or UINT32_MAX when nothing is pending.
 */
uint32_t link_rpc_poll(link_rpc_t *r, uint32_t now_ms);

/** Fail everything pending (link down) and forget the dedupe table (a new session restarts ids). */
void link_rpc_reset(link_rpc_t *r);

typedef enum {
    LINK_SEEN_NEW,       // first time: handle it
    LINK_SEEN_REPLAY,    // handled before and the reply was cached: *reply is it, send it again
    LINK_SEEN_IGNORE,    // handled before, no cached reply (or still handling): do nothing
} link_rpc_seen_result_t;

/** Peer request `id` arrived. A NEW id is remembered, evicting the oldest. */
link_rpc_seen_result_t link_rpc_on_request(link_rpc_t *r, uint32_t id, const uint8_t **reply, size_t *reply_len);

/** Remember the serialized reply for request `id` so a retry can be answered without running the handler again. */
void link_rpc_cache_reply(link_rpc_t *r, uint32_t id, const uint8_t *reply, size_t len);

#ifdef __cplusplus
}
#endif
