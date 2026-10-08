// Streaming PNG encoder for an RGB565 frame (P8-17). Pure C, also built by firmware/host_test.
//
// 8-bit RGB, filter 0, zlib stream with one fixed-Huffman block. The only match used is
// "same pixel as the one before" (distance 3), which shrinks flat UI screens ~50x without
// a hash table or a window. Needs ~1.3 KB of RAM per row plus the 4 KB output buffer.
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Receives the PNG bytes in order. Return 0 on success; non-zero aborts the encode. */
typedef int (*png_sink_fn)(void *ctx, const uint8_t *data, size_t len);

/**
 * Encode w*h RGB565 pixels (host-order uint16, row-major, no padding) as a PNG.
 * Returns 0, -1 for bad arguments (w or h 0, or > 4096) or the sink's non-zero value.
 */
int png_encode_rgb565(const uint16_t *px, uint32_t w, uint32_t h, png_sink_fn sink, void *ctx);

/** Standard CRC-32 (the PNG / zlib one). Continue with crc = previous result; start at 0. */
uint32_t png_crc32(uint32_t crc, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
