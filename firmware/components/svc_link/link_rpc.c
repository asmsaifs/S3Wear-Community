#include "link_rpc.h"

#include <string.h>

// Signed difference of two ms counters that may wrap.
static int32_t ms_diff(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b);
}

void link_rpc_init(link_rpc_t *r, link_rpc_mem_t mem, link_rpc_tx_fn tx, void *tx_ctx)
{
    memset(r, 0, sizeof *r);
    r->mem = mem;
    r->tx = tx;
    r->tx_ctx = tx_ctx;
    r->next_id = 1;
}

uint32_t link_rpc_next_id(link_rpc_t *r)
{
    const uint32_t id = r->next_id++;
    if (r->next_id == 0) {
        r->next_id = 1;
    }
    return id;
}

static void free_req(link_rpc_t *r, link_rpc_req_t *q)
{
    r->mem.release(q->msg);
    memset(q, 0, sizeof *q);
}

bool link_rpc_start(link_rpc_t *r, uint32_t id, const uint8_t *msg, size_t len, uint32_t now_ms, link_rpc_done_fn done,
                    void *user)
{
    if (id == 0 || len == 0) {
        return false;
    }
    link_rpc_req_t *slot = NULL;
    for (int i = 0; i < LINK_RPC_MAX_PENDING; i++) {
        if (r->pending[i].id == id) {
            return false;
        }
        if (!slot && r->pending[i].id == 0) {
            slot = &r->pending[i];
        }
    }
    if (!slot) {
        return false;
    }
    uint8_t *copy = r->mem.alloc(len);
    if (!copy) {
        return false;
    }
    memcpy(copy, msg, len);
    if (!r->tx(r->tx_ctx, copy, len)) {
        r->mem.release(copy);
        return false;
    }
    slot->id = id;
    slot->msg = copy;
    slot->len = len;
    slot->sent = 1;
    slot->deadline_ms = now_ms + LINK_RPC_TIMEOUT_MS;
    slot->done = done;
    slot->user = user;
    return true;
}

bool link_rpc_on_reply(link_rpc_t *r, uint32_t id, const void *reply)
{
    for (int i = 0; i < LINK_RPC_MAX_PENDING; i++) {
        link_rpc_req_t *q = &r->pending[i];
        if (q->id != 0 && q->id == id) {
            const link_rpc_done_fn done = q->done;
            void *user = q->user;
            free_req(r, q);
            if (done) {
                done(user, LINK_RPC_OK, reply);
            }
            return true;
        }
    }
    return false;
}

uint32_t link_rpc_poll(link_rpc_t *r, uint32_t now_ms)
{
    uint32_t wait = UINT32_MAX;
    for (int i = 0; i < LINK_RPC_MAX_PENDING; i++) {
        link_rpc_req_t *q = &r->pending[i];
        if (q->id == 0) {
            continue;
        }
        if (ms_diff(now_ms, q->deadline_ms) >= 0) {
            if (q->sent > LINK_RPC_RETRIES) {
                const link_rpc_done_fn done = q->done;
                void *user = q->user;
                free_req(r, q);
                r->timeouts++;
                if (done) {
                    done(user, LINK_RPC_TIMEOUT, NULL);
                }
                continue;
            }
            q->sent++; // a failed tx still uses up the attempt: the link is not usable
            r->retries++;
            (void)r->tx(r->tx_ctx, q->msg, q->len);
            q->deadline_ms = now_ms + LINK_RPC_TIMEOUT_MS;
        }
        const uint32_t left = (uint32_t)ms_diff(q->deadline_ms, now_ms);
        if (left < wait) {
            wait = left;
        }
    }
    return wait;
}

static void free_seen(link_rpc_t *r, link_rpc_seen_t *s)
{
    if (s->reply) {
        r->mem.release(s->reply);
    }
    memset(s, 0, sizeof *s);
}

void link_rpc_reset(link_rpc_t *r)
{
    for (int i = 0; i < LINK_RPC_MAX_PENDING; i++) {
        link_rpc_req_t *q = &r->pending[i];
        if (q->id == 0) {
            continue;
        }
        const link_rpc_done_fn done = q->done;
        void *user = q->user;
        free_req(r, q);
        if (done) {
            done(user, LINK_RPC_DISCONNECTED, NULL);
        }
    }
    for (int i = 0; i < LINK_RPC_DEDUPE; i++) {
        free_seen(r, &r->seen[i]);
    }
    r->seen_head = 0;
}

link_rpc_seen_result_t link_rpc_on_request(link_rpc_t *r, uint32_t id, const uint8_t **reply, size_t *reply_len)
{
    for (int i = 0; i < LINK_RPC_DEDUPE; i++) {
        link_rpc_seen_t *s = &r->seen[i];
        if (s->id == id) {
            if (s->reply) {
                *reply = s->reply;
                *reply_len = s->reply_len;
                return LINK_SEEN_REPLAY;
            }
            return LINK_SEEN_IGNORE;
        }
    }
    link_rpc_seen_t *s = &r->seen[r->seen_head];
    r->seen_head = (uint8_t)((r->seen_head + 1) % LINK_RPC_DEDUPE);
    free_seen(r, s);
    s->id = id;
    return LINK_SEEN_NEW;
}

void link_rpc_cache_reply(link_rpc_t *r, uint32_t id, const uint8_t *reply, size_t len)
{
    if (len == 0 || len > LINK_RPC_REPLY_CACHE_MAX) {
        return;
    }
    for (int i = 0; i < LINK_RPC_DEDUPE; i++) {
        link_rpc_seen_t *s = &r->seen[i];
        if (s->id == id && !s->reply) {
            s->reply = r->mem.alloc(len);
            if (s->reply) {
                memcpy(s->reply, reply, len);
                s->reply_len = len;
            }
            return;
        }
    }
}
