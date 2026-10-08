// LVGL 9 port for the CO5300 AMOLED.
//
// - Render mode (Kconfig S3W_LVGL_RENDER_*, docs/02 §6): partial into two internal
//   DMA buffers, or direct into a PSRAM frame buffer flushed through the same two
//   buffers as bounce buffers. Pixels are byte-swapped to big-endian on the way out.
// - Rounder: CO5300 needs even x1/y1 and odd x2/y2 (docs/01-hardware.md §5).
// - Each frame's first flush waits for the panel tearing-effect pulse.
// - LVGL heap (lv_malloc) lives in PSRAM.
// - Each frame holds a no-light-sleep PM lock (the TE wait needs the GPIO edge
//   interrupt, which cannot wake light sleep).
// - One UI task (core 1) owns LVGL. Other tasks wrap LVGL calls in
//   lv_lock()/lv_unlock() (LV_OS_FREERTOS).
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_io_handle_t io;
    uint16_t hres;
    uint16_t vres;
    uint16_t buf_lines;                     // lines per draw buffer
    esp_err_t (*te_wait)(uint32_t timeout_ms); // optional V-blank sync
} s3w_lvgl_port_cfg_t;

/** Bytes per draw buffer for a given config (the panel's max transfer size). */
#define S3W_LVGL_PORT_BUF_BYTES(hres, lines) ((size_t)(hres) * (lines) * 2)

/** lv_init, display + buffers, tick, UI task. Call once. */
esp_err_t s3w_lvgl_port_init(const s3w_lvgl_port_cfg_t *cfg);

/** The display created by s3w_lvgl_port_init(), else NULL. */
lv_display_t *s3w_lvgl_port_display(void);

/**
 * Panel output (svc_power, through the app's UI hook). Call on the UI task or with
 * lv_lock() held. Off: frames are rendered but not sent, so nothing reaches a
 * sleeping panel. On: the whole screen is redrawn at the next refresh.
 */
void s3w_lvgl_port_set_output(bool on);

/**
 * Screen off or AOD: the UI task sleeps until the next LVGL timer or a post, with
 * no periodic cap, so the CPU can stay in light sleep. UI task or lv_lock() held.
 */
void s3w_lvgl_port_set_low_power(bool low_power);

/**
 * Partial rendering with one internal DMA draw buffer instead of two (true) — frees one
 * (S3W_LVGL_PORT_BUF_BYTES, 32 KB) for the Wi-Fi driver (P9-01), at the cost of rendering waiting
 * for each band's DMA — or with two again (false). Takes lv_lock() and waits for the flush in
 * flight; any task but one already holding lv_lock() from another task. ESP_ERR_NO_MEM: the second
 * buffer could not be allocated back (stays single; call again later). ESP_ERR_NOT_SUPPORTED in
 * direct render mode.
 */
esp_err_t s3w_lvgl_port_set_single_buffer(bool single);

/**
 * Screenshot (P8-17): repaint the whole screen, layers and overlays included, and copy what is
 * sent to the panel into dst (hres * vres RGB565, host byte order, row-major). Blocks the calling
 * task (not the UI task) until the frame is complete or timeout_ms passes (ESP_ERR_TIMEOUT).
 * ESP_ERR_INVALID_STATE: panel output is off, a capture is running, or called on the UI task.
 */
esp_err_t s3w_lvgl_port_capture(uint16_t *dst, uint32_t timeout_ms);

/** Panel output is on (the screen shows frames). Any task. */
bool s3w_lvgl_port_output_is_on(void);

/** Flush-side timing, accumulated since the last reset (display performance, P2-04). */
typedef struct {
    uint32_t frames;      // refresh cycles that flushed anything
    uint32_t flushes;     // flush_cb calls (bands in partial mode, dirty areas in direct)
    uint64_t px;          // pixels sent
    uint64_t te_wait_us;  // waiting for the tearing-effect pulse
    uint64_t swap_us;     // byte swap (partial) or copy + swap into bounce buffers (direct)
    uint64_t bus_wait_us; // in esp_lcd_panel_draw_bitmap, i.e. waiting for the previous DMA
} s3w_lvgl_port_stats_t;

/** Copy the stats (out may be NULL) and optionally zero them. Takes lv_lock(). */
void s3w_lvgl_port_stats(s3w_lvgl_port_stats_t *out, bool reset);

#ifdef __cplusplus
}
#endif
