// Fixed-capacity FIFO of deferred calls with a copied payload (pure C, host-tested).
// Not thread-safe by itself: s3w_ui.c wraps it in a critical section.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define S3W_MBOX_PAYLOAD_MAX 128

/** Runs in the consumer (UI task). data is valid only during the call. */
typedef void (*s3w_mbox_fn_t)(void *ctx, int32_t arg, const void *data, size_t len);

typedef struct {
    s3w_mbox_fn_t fn;
    void *ctx;
    int32_t arg;
    uint16_t len;
    uint8_t data[S3W_MBOX_PAYLOAD_MAX];
} s3w_mbox_msg_t;

typedef struct {
    s3w_mbox_msg_t *slots;
    uint16_t cap;
    uint16_t head;      // next to pop
    uint16_t count;
    uint16_t high_water;
    uint32_t dropped;   // pushes rejected (full or payload too big)
} s3w_mbox_t;

void s3w_mbox_init(s3w_mbox_t *m, s3w_mbox_msg_t *slots, uint16_t cap);

/** Copy a message in. false (and dropped++) if full, fn is NULL or len > S3W_MBOX_PAYLOAD_MAX. */
bool s3w_mbox_push(s3w_mbox_t *m, s3w_mbox_fn_t fn, void *ctx, int32_t arg, const void *data, size_t len);

/** Oldest message out (FIFO). false if empty. */
bool s3w_mbox_pop(s3w_mbox_t *m, s3w_mbox_msg_t *out);

static inline uint16_t s3w_mbox_count(const s3w_mbox_t *m)
{
    return m->count;
}

#ifdef __cplusplus
}
#endif
