// Streaming PNG encoder: see include/png_enc.h.
#include "png_enc.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define OUT_MAX    4096 // one IDAT chunk per full buffer
#define W_MAX      4096
#define MATCH_DIST 3    // one RGB pixel back
#define MATCH_MAX  258
#define MATCH_MIN  3

static uint32_t s_crc_table[256];
static bool s_crc_ready;

static void crc_init(void)
{
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) {
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        }
        s_crc_table[n] = c;
    }
    s_crc_ready = true;
}

uint32_t png_crc32(uint32_t crc, const uint8_t *data, size_t len)
{
    if (!s_crc_ready) {
        crc_init(); // idempotent: two tasks racing here write the same table
    }
    crc = ~crc;
    while (len--) {
        crc = s_crc_table[(crc ^ *data++) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

typedef struct {
    png_sink_fn sink;
    void *ctx;
    int err;
    uint8_t out[OUT_MAX];
    size_t out_len;
    uint32_t bit_buf; // deflate bits, LSB first
    int bit_cnt;
    uint32_t adler_a, adler_b;
} enc_t;

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void emit(enc_t *e, const uint8_t *d, size_t n)
{
    if (!e->err) {
        e->err = e->sink(e->ctx, d, n);
    }
}

static void chunk(enc_t *e, const char type[4], const uint8_t *data, size_t len)
{
    uint8_t head[8];
    put_be32(head, (uint32_t)len);
    memcpy(head + 4, type, 4);
    uint32_t crc = png_crc32(0, head + 4, 4);
    crc = png_crc32(crc, data, len);
    uint8_t tail[4];
    put_be32(tail, crc);
    emit(e, head, 8);
    emit(e, data, len);
    emit(e, tail, 4);
}

static void flush_idat(enc_t *e)
{
    if (e->out_len) {
        chunk(e, "IDAT", e->out, e->out_len);
        e->out_len = 0;
    }
}

static void put_byte(enc_t *e, uint8_t b)
{
    if (e->out_len == OUT_MAX) {
        flush_idat(e);
    }
    e->out[e->out_len++] = b;
}

// LSB-first bits (extra bits, headers).
static void put_bits(enc_t *e, uint32_t v, int n)
{
    e->bit_buf |= v << e->bit_cnt;
    e->bit_cnt += n;
    while (e->bit_cnt >= 8) {
        put_byte(e, (uint8_t)e->bit_buf);
        e->bit_buf >>= 8;
        e->bit_cnt -= 8;
    }
}

// Huffman codes go MSB first.
static void put_code(enc_t *e, uint32_t code, int n)
{
    uint32_t r = 0;
    for (int i = 0; i < n; i++) {
        r = (r << 1) | ((code >> i) & 1);
    }
    put_bits(e, r, n);
}

static void put_sym(enc_t *e, unsigned s) // fixed literal/length code, RFC 1951 3.2.6
{
    if (s < 144) {
        put_code(e, 0x30 + s, 8);
    } else if (s < 256) {
        put_code(e, 0x190 + (s - 144), 9);
    } else if (s < 280) {
        put_code(e, s - 256, 7);
    } else {
        put_code(e, 0xC0 + (s - 280), 8);
    }
}

static const uint16_t k_len_base[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                        31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t k_len_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                        2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};

static void put_match(enc_t *e, unsigned len)
{
    int i = 28;
    while (k_len_base[i] > len) {
        i--;
    }
    put_sym(e, 257 + i);
    put_bits(e, len - k_len_base[i], k_len_extra[i]);
    put_code(e, MATCH_DIST - 1, 5); // distance code 2 = distance 3, no extra bits
}

static void adler_update(enc_t *e, const uint8_t *d, size_t n)
{
    for (size_t i = 0; i < n; i++) { // n <= 12 KB per row: a/b stay far below 2^32 between mods
        e->adler_a += d[i];
        e->adler_b += e->adler_a;
    }
    e->adler_a %= 65521;
    e->adler_b %= 65521;
}

int png_encode_rgb565(const uint16_t *px, uint32_t w, uint32_t h, png_sink_fn sink, void *ctx)
{
    if (!px || !sink || w == 0 || h == 0 || w > W_MAX || h > W_MAX) {
        return -1;
    }
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    const size_t row_len = 1 + 3 * (size_t)w;
    enc_t *e = calloc(1, sizeof *e + row_len); // off the caller's stack (the console task is small)
    if (!e) {
        return -1;
    }
    e->sink = sink;
    e->ctx = ctx;
    e->adler_a = 1;
    uint8_t *row = (uint8_t *)(e + 1);

    emit(e, sig, sizeof sig);
    uint8_t ihdr[13];
    put_be32(ihdr, w);
    put_be32(ihdr + 4, h);
    ihdr[8] = 8; // bit depth
    ihdr[9] = 2; // RGB
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk(e, "IHDR", ihdr, sizeof ihdr);

    put_byte(e, 0x78); // zlib: deflate, 32 KB window, fastest
    put_byte(e, 0x01);
    put_bits(e, 1, 1); // BFINAL
    put_bits(e, 1, 2); // BTYPE = fixed Huffman

    for (uint32_t y = 0; y < h && !e->err; y++) {
        row[0] = 0; // filter: none
        for (uint32_t x = 0; x < w; x++) {
            const uint16_t c = px[(size_t)y * w + x];
            const uint8_t r5 = (c >> 11) & 0x1F, g6 = (c >> 5) & 0x3F, b5 = c & 0x1F;
            row[1 + 3 * x] = (uint8_t)((r5 << 3) | (r5 >> 2));
            row[2 + 3 * x] = (uint8_t)((g6 << 2) | (g6 >> 4));
            row[3 + 3 * x] = (uint8_t)((b5 << 3) | (b5 >> 2));
        }
        adler_update(e, row, row_len);
        for (size_t i = 0; i < row_len;) {
            size_t run = 0;
            if (i > MATCH_DIST) { // index 0 is the filter byte: never a match source
                while (i + run < row_len && run < MATCH_MAX && row[i + run] == row[i + run - MATCH_DIST]) {
                    run++;
                }
            }
            if (run >= MATCH_MIN) {
                put_match(e, (unsigned)run);
                i += run;
            } else {
                put_sym(e, row[i++]);
            }
        }
    }
    put_sym(e, 256); // end of block
    if (e->bit_cnt) {
        put_byte(e, (uint8_t)e->bit_buf); // pad to a byte
    }
    uint8_t ad[4];
    put_be32(ad, (e->adler_b << 16) | e->adler_a);
    for (int i = 0; i < 4; i++) {
        put_byte(e, ad[i]);
    }
    flush_idat(e);
    chunk(e, "IEND", NULL, 0);
    const int err = e->err;
    free(e);
    return err;
}
