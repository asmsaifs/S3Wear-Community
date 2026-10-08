// SHA-256 (FIPS 180-4). Pure C, no allocation, so the watch, the simulator and host tests share
// it. App packages (app_pkg.h) and bulk transfers hash with it; Ed25519 comes from Monocypher
// (monocypher-ed25519.h, same component).
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define S3W_SHA256_LEN 32

typedef struct {
    uint32_t h[8];
    uint64_t bytes; // total length so far
    uint8_t buf[64];
    size_t fill;    // bytes in buf
} s3w_sha256_t;

void s3w_sha256_init(s3w_sha256_t *c);
void s3w_sha256_update(s3w_sha256_t *c, const void *data, size_t len);
void s3w_sha256_final(s3w_sha256_t *c, uint8_t out[S3W_SHA256_LEN]);

/** One call: out = SHA-256(data). */
void s3w_sha256(const void *data, size_t len, uint8_t out[S3W_SHA256_LEN]);

#ifdef __cplusplus
}
#endif
