#include "sim_screenshot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/libs/lodepng/lodepng.h"

static void headless_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)area;
    (void)px_map;
    /* The frame stays in the draw buffer; sim_screenshot_save() reads it. */
    lv_display_flush_ready(disp);
}

lv_display_t *sim_headless_display_create(int32_t hor_res, int32_t ver_res)
{
    lv_display_t *disp = lv_display_create(hor_res, ver_res);
    if (disp == NULL) {
        return NULL;
    }
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

    uint32_t stride = lv_draw_buf_width_to_stride(hor_res, LV_COLOR_FORMAT_RGB565);
    uint32_t size = stride * (uint32_t)ver_res;
    void *buf = malloc(size);
    if (buf == NULL) {
        lv_display_delete(disp);
        return NULL;
    }
    /* FULL mode: every refresh redraws the whole frame into buf. */
    lv_display_set_buffers(disp, buf, NULL, size, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(disp, headless_flush_cb);
    return disp;
}

long sim_lit_pixels_x100(lv_display_t *disp)
{
    lv_obj_invalidate(lv_display_get_screen_active(disp));
    lv_refr_now(disp);
    lv_draw_buf_t *fb = lv_display_get_buf_active(disp);
    const uint32_t w = (uint32_t)lv_display_get_horizontal_resolution(disp);
    const uint32_t h = (uint32_t)lv_display_get_vertical_resolution(disp);
    long lit = 0;
    for (uint32_t y = 0; y < h; y++) {
        const uint16_t *row = (const uint16_t *)(fb->data + (size_t)y * fb->header.stride);
        for (uint32_t x = 0; x < w; x++) {
            lit += row[x] != 0;
        }
    }
    return (long)((long long)lit * 10000 / ((long long)w * h));
}

bool sim_screenshot_save(lv_display_t *disp, const char *path)
{
    lv_obj_invalidate(lv_display_get_screen_active(disp));
    lv_refr_now(disp);

    lv_draw_buf_t *fb = lv_display_get_buf_active(disp);
    uint32_t w = (uint32_t)lv_display_get_horizontal_resolution(disp);
    uint32_t h = (uint32_t)lv_display_get_vertical_resolution(disp);

    uint8_t *rgb = malloc((size_t)w * h * 3);
    if (rgb == NULL) {
        return false;
    }
    for (uint32_t y = 0; y < h; y++) {
        const uint16_t *row = (const uint16_t *)(fb->data + (size_t)y * fb->header.stride);
        uint8_t *out = rgb + (size_t)y * w * 3;
        for (uint32_t x = 0; x < w; x++) {
            uint16_t c = row[x];
            uint8_t r5 = (c >> 11) & 0x1F;
            uint8_t g6 = (c >> 5) & 0x3F;
            uint8_t b5 = c & 0x1F;
            out[x * 3 + 0] = (uint8_t)((r5 << 3) | (r5 >> 2));
            out[x * 3 + 1] = (uint8_t)((g6 << 2) | (g6 >> 4));
            out[x * 3 + 2] = (uint8_t)((b5 << 3) | (b5 >> 2));
        }
    }

    /* Encode in memory: lodepng's *_file() goes through lv_fs, which needs a
     * drive letter. png is allocated with lv_malloc. */
    unsigned char *png = NULL;
    size_t png_size = 0;
    unsigned err = lodepng_encode24(&png, &png_size, rgb, w, h);
    free(rgb);
    if (err != 0) {
        fprintf(stderr, "screenshot: encode: %s\n", lodepng_error_text(err));
        return false;
    }

    FILE *f = fopen(path, "wb");
    bool ok = f != NULL && fwrite(png, 1, png_size, f) == png_size;
    if (f != NULL && fclose(f) != 0) {
        ok = false;
    }
    lv_free(png);
    if (!ok) {
        fprintf(stderr, "screenshot: cannot write %s\n", path);
    }
    return ok;
}

static unsigned char *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    unsigned char *buf = NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        long n = ftell(f);
        if (n > 0 && fseek(f, 0, SEEK_SET) == 0 && (buf = malloc((size_t)n)) != NULL) {
            if (fread(buf, 1, (size_t)n, f) == (size_t)n) {
                *size = (size_t)n;
            } else {
                free(buf);
                buf = NULL;
            }
        }
    }
    fclose(f);
    return buf;
}

/* LVGL's lodepng is patched to decode into an lv_draw_buf_t (ARGB8888 memory,
 * here RGBA byte order from lodepng_convert), not a plain byte array. */
static lv_draw_buf_t *decode(const char *path)
{
    size_t size = 0;
    unsigned char *png = read_file(path, &size);
    if (png == NULL) {
        fprintf(stderr, "compare: cannot read %s\n", path);
        return NULL;
    }
    lv_draw_buf_t *buf = NULL;
    unsigned w = 0;
    unsigned h = 0;
    unsigned err = lodepng_decode32((unsigned char **)&buf, &w, &h, png, size);
    free(png);
    if (err != 0) {
        fprintf(stderr, "compare: %s: %s\n", path, lodepng_error_text(err));
        return NULL;
    }
    return buf;
}

long sim_png_compare(const char *actual, const char *expected)
{
    lv_draw_buf_t *a = decode(actual);
    lv_draw_buf_t *e = decode(expected);
    long diff = -1;
    if (a != NULL && e != NULL) {
        const uint32_t w = a->header.w;
        const uint32_t h = a->header.h;
        if (w != e->header.w || h != e->header.h) {
            fprintf(stderr, "compare: size %ux%u, expected %ux%u\n", (unsigned)w, (unsigned)h,
                    (unsigned)e->header.w, (unsigned)e->header.h);
        } else {
            diff = 0;
            for (uint32_t y = 0; y < h; y++) {
                const uint8_t *ra = a->data + (size_t)y * a->header.stride;
                const uint8_t *re = e->data + (size_t)y * e->header.stride;
                for (uint32_t x = 0; x < w; x++) {
                    if (memcmp(ra + x * 4, re + x * 4, 3) != 0) {
                        if (diff == 0) {
                            fprintf(stderr, "compare: first difference at (%u, %u)\n", (unsigned)x, (unsigned)y);
                        }
                        diff++;
                    }
                }
            }
        }
    }
    if (a != NULL) {
        lv_draw_buf_destroy(a);
    }
    if (e != NULL) {
        lv_draw_buf_destroy(e);
    }
    return diff;
}
