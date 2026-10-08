#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Create an offscreen display (no SDL) holding one full RGB565 frame. */
lv_display_t *sim_headless_display_create(int32_t hor_res, int32_t ver_res);

/** Render all pending changes on @p disp and write the frame to @p path as PNG. */
bool sim_screenshot_save(lv_display_t *disp, const char *path);

/** Render pending changes and return the share of lit (non-black) pixels, in 0.01 %. */
long sim_lit_pixels_x100(lv_display_t *disp);

/** Pixel-compare two PNGs: number of differing pixels, or -1 if unreadable / sizes differ. */
long sim_png_compare(const char *actual, const char *expected);

#ifdef __cplusplus
}
#endif
