// CO5300 AMOLED driver (QSPI) as an esp_lcd panel.
//
// Pin-agnostic: the caller creates the QSPI bus and panel IO (see
// CO5300_PANEL_IO_QSPI_CONFIG) and passes the reset GPIO in the dev config.
// Standard esp_lcd_panel_* calls work (reset/init/draw_bitmap/set_gap/
// disp_on_off/disp_sleep/invert_color); mirror and swap_xy are not supported.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_panel_dev.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t cmd;
    const uint8_t *data;
    uint8_t data_bytes;
    uint16_t delay_ms; // after the command
} co5300_init_cmd_t;

/** Optional, via esp_lcd_panel_dev_config_t.vendor_config. NULL = built-in sequence. */
typedef struct {
    const co5300_init_cmd_t *init_cmds;
    size_t init_cmds_size;
} co5300_vendor_config_t;

/**
 * QSPI panel IO settings for CO5300: 32-bit command phase (opcode + DCS command),
 * 8-bit params, quad data lines, SPI mode 0.
 */
#define CO5300_PANEL_IO_QSPI_CONFIG(cs, pclk, done_cb, cb_ctx) \
    {                                                           \
        .cs_gpio_num = (cs),                                    \
        .dc_gpio_num = -1,                                      \
        .spi_mode = 0,                                          \
        .pclk_hz = (pclk),                                      \
        .trans_queue_depth = 10,                                \
        .on_color_trans_done = (done_cb),                       \
        .user_ctx = (cb_ctx),                                   \
        .lcd_cmd_bits = 32,                                     \
        .lcd_param_bits = 8,                                    \
        .flags = {.quad_mode = true},                           \
    }

esp_err_t co5300_new_panel(esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *cfg,
                           esp_lcd_panel_handle_t *ret_panel);

/** DCS 0x51 Write Display Brightness, 0..255. */
esp_err_t co5300_set_brightness(esp_lcd_panel_handle_t panel, uint8_t level);

#ifdef __cplusplus
}
#endif
