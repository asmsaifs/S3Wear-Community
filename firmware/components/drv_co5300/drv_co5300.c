// CO5300 AMOLED panel driver for esp_lcd (QSPI).
//
// QSPI framing (Waveshare BSP / CO5300 datasheet): the 32-bit command phase is
// <opcode:8><0x00:8><dcs_cmd:8><0x00:8>. Opcode 0x02 = write command + params on
// one line, 0x32 = write pixel data on four lines.
#include "drv_co5300.h"

#include <stdlib.h>
#include <sys/cdefs.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_interface.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "drv_co5300";

#define OPCODE_WRITE_CMD   0x02
#define OPCODE_WRITE_COLOR 0x32

#define CMD_WRITE_BRIGHTNESS 0x51

#define RESET_HOLD_MS  10
#define RESET_WAIT_MS  200 // Arduino_CO5300: CO5300_RST_DELAY
#define SLEEP_OUT_MS   120 // datasheet: wait before next command / display on
#define SLEEP_IN_MS    120

// Vendor init sequence, copied verbatim from the Waveshare BSP lcd_init_cmds[]
// (docs/vendor/README.md). 0x2A/0x2B already include the 0x16 column gap.
static const co5300_init_cmd_t k_vendor_init[] = {
    {0x11, NULL, 0, SLEEP_OUT_MS},                        // sleep out
    {0xC4, (const uint8_t[]){0x80}, 1, 0},                // SPI mode control
    {0x44, (const uint8_t[]){0x01, 0xD1}, 2, 0},          // TE scan line 465
    {0x35, (const uint8_t[]){0x00}, 1, 0},                // TE on (V-blank only)
    {0x53, (const uint8_t[]){0x20}, 1, 10},               // CTRL display 1: brightness control on
    {0x63, (const uint8_t[]){0xFF}, 1, 10},               // HBM brightness
    {0x51, (const uint8_t[]){0x00}, 1, 10},               // brightness 0 until first frame
    {0x2A, (const uint8_t[]){0x00, 0x16, 0x01, 0xAF}, 4, 0},
    {0x2B, (const uint8_t[]){0x00, 0x00, 0x01, 0xF5}, 4, 0},
    {0x29, NULL, 0, 10},                                  // display on
};

typedef struct {
    esp_lcd_panel_t base;
    esp_lcd_panel_io_handle_t io;
    int reset_gpio;
    bool reset_level;
    int x_gap;
    int y_gap;
    uint8_t madctl;
    uint8_t colmod;
    uint8_t bytes_per_pixel;
    const co5300_init_cmd_t *init_cmds;
    size_t init_cmds_size;
} co5300_panel_t;

static esp_err_t tx_param(co5300_panel_t *p, uint8_t cmd, const void *data, size_t len)
{
    const int lcd_cmd = (OPCODE_WRITE_CMD << 24) | (cmd << 8);
    return esp_lcd_panel_io_tx_param(p->io, lcd_cmd, data, len);
}

static esp_err_t tx_color(co5300_panel_t *p, uint8_t cmd, const void *data, size_t len)
{
    const int lcd_cmd = (OPCODE_WRITE_COLOR << 24) | (cmd << 8);
    return esp_lcd_panel_io_tx_color(p->io, lcd_cmd, data, len);
}

static esp_err_t panel_del(esp_lcd_panel_t *panel)
{
    co5300_panel_t *p = __containerof(panel, co5300_panel_t, base);
    if (p->reset_gpio >= 0) {
        gpio_reset_pin(p->reset_gpio);
    }
    free(p);
    return ESP_OK;
}

static esp_err_t panel_reset(esp_lcd_panel_t *panel)
{
    co5300_panel_t *p = __containerof(panel, co5300_panel_t, base);
    if (p->reset_gpio >= 0) {
        gpio_set_level(p->reset_gpio, p->reset_level);
        vTaskDelay(pdMS_TO_TICKS(RESET_HOLD_MS));
        gpio_set_level(p->reset_gpio, !p->reset_level);
        vTaskDelay(pdMS_TO_TICKS(RESET_WAIT_MS));
    } else {
        ESP_RETURN_ON_ERROR(tx_param(p, LCD_CMD_SWRESET, NULL, 0), TAG, "swreset");
        vTaskDelay(pdMS_TO_TICKS(RESET_WAIT_MS));
    }
    return ESP_OK;
}

static esp_err_t panel_init(esp_lcd_panel_t *panel)
{
    co5300_panel_t *p = __containerof(panel, co5300_panel_t, base);
    ESP_RETURN_ON_ERROR(tx_param(p, LCD_CMD_MADCTL, &p->madctl, 1), TAG, "madctl");
    ESP_RETURN_ON_ERROR(tx_param(p, LCD_CMD_COLMOD, &p->colmod, 1), TAG, "colmod");
    for (size_t i = 0; i < p->init_cmds_size; i++) {
        const co5300_init_cmd_t *c = &p->init_cmds[i];
        ESP_RETURN_ON_ERROR(tx_param(p, c->cmd, c->data, c->data_bytes), TAG, "init cmd 0x%02X", c->cmd);
        if (c->delay_ms) {
            vTaskDelay(pdMS_TO_TICKS(c->delay_ms));
        }
    }
    return ESP_OK;
}

static esp_err_t panel_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end,
                                   const void *color_data)
{
    co5300_panel_t *p = __containerof(panel, co5300_panel_t, base);
    ESP_RETURN_ON_FALSE(x_start < x_end && y_start < y_end, ESP_ERR_INVALID_ARG, TAG, "bad area");
    x_start += p->x_gap;
    x_end += p->x_gap;
    y_start += p->y_gap;
    y_end += p->y_gap;

    const uint8_t caset[] = {x_start >> 8, x_start & 0xFF, (x_end - 1) >> 8, (x_end - 1) & 0xFF};
    const uint8_t raset[] = {y_start >> 8, y_start & 0xFF, (y_end - 1) >> 8, (y_end - 1) & 0xFF};
    ESP_RETURN_ON_ERROR(tx_param(p, LCD_CMD_CASET, caset, sizeof caset), TAG, "caset");
    ESP_RETURN_ON_ERROR(tx_param(p, LCD_CMD_RASET, raset, sizeof raset), TAG, "raset");
    const size_t len = (size_t)(x_end - x_start) * (y_end - y_start) * p->bytes_per_pixel;
    return tx_color(p, LCD_CMD_RAMWR, color_data, len);
}

static esp_err_t panel_invert_color(esp_lcd_panel_t *panel, bool invert)
{
    co5300_panel_t *p = __containerof(panel, co5300_panel_t, base);
    return tx_param(p, invert ? LCD_CMD_INVON : LCD_CMD_INVOFF, NULL, 0);
}

static esp_err_t panel_mirror(esp_lcd_panel_t *panel, bool x, bool y)
{
    (void)panel;
    (void)x;
    (void)y;
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t panel_swap_xy(esp_lcd_panel_t *panel, bool swap)
{
    (void)panel;
    (void)swap;
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t panel_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap)
{
    co5300_panel_t *p = __containerof(panel, co5300_panel_t, base);
    p->x_gap = x_gap;
    p->y_gap = y_gap;
    return ESP_OK;
}

static esp_err_t panel_disp_on_off(esp_lcd_panel_t *panel, bool on)
{
    co5300_panel_t *p = __containerof(panel, co5300_panel_t, base);
    return tx_param(p, on ? LCD_CMD_DISPON : LCD_CMD_DISPOFF, NULL, 0);
}

static esp_err_t panel_disp_sleep(esp_lcd_panel_t *panel, bool sleep)
{
    co5300_panel_t *p = __containerof(panel, co5300_panel_t, base);
    ESP_RETURN_ON_ERROR(tx_param(p, sleep ? LCD_CMD_SLPIN : LCD_CMD_SLPOUT, NULL, 0), TAG, "sleep");
    vTaskDelay(pdMS_TO_TICKS(sleep ? SLEEP_IN_MS : SLEEP_OUT_MS));
    return ESP_OK;
}

esp_err_t co5300_set_brightness(esp_lcd_panel_handle_t panel, uint8_t level)
{
    ESP_RETURN_ON_FALSE(panel, ESP_ERR_INVALID_ARG, TAG, "panel");
    co5300_panel_t *p = __containerof(panel, co5300_panel_t, base);
    return tx_param(p, CMD_WRITE_BRIGHTNESS, &level, 1);
}

esp_err_t co5300_new_panel(esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *cfg,
                           esp_lcd_panel_handle_t *ret_panel)
{
    ESP_RETURN_ON_FALSE(io && cfg && ret_panel, ESP_ERR_INVALID_ARG, TAG, "args");

    uint8_t colmod;
    uint8_t bpp_bytes;
    switch (cfg->bits_per_pixel) {
    case 16:
        colmod = 0x55;
        bpp_bytes = 2;
        break;
    case 24:
        colmod = 0x77;
        bpp_bytes = 3;
        break;
    default:
        ESP_LOGE(TAG, "unsupported bpp %d", (int)cfg->bits_per_pixel);
        return ESP_ERR_NOT_SUPPORTED;
    }

    co5300_panel_t *p = calloc(1, sizeof *p);
    ESP_RETURN_ON_FALSE(p, ESP_ERR_NO_MEM, TAG, "no mem");

    if (cfg->reset_gpio_num >= 0) {
        const gpio_config_t io_cfg = {
            .pin_bit_mask = 1ULL << cfg->reset_gpio_num,
            .mode = GPIO_MODE_OUTPUT,
        };
        esp_err_t err = gpio_config(&io_cfg);
        if (err != ESP_OK) {
            free(p);
            return err;
        }
    }

    const co5300_vendor_config_t *vc = cfg->vendor_config;
    p->io = io;
    p->reset_gpio = cfg->reset_gpio_num;
    p->reset_level = cfg->flags.reset_active_high;
    p->madctl = cfg->rgb_ele_order == LCD_RGB_ELEMENT_ORDER_BGR ? LCD_CMD_BGR_BIT : 0;
    p->colmod = colmod;
    p->bytes_per_pixel = bpp_bytes;
    p->init_cmds = vc && vc->init_cmds ? vc->init_cmds : k_vendor_init;
    p->init_cmds_size = vc && vc->init_cmds ? vc->init_cmds_size : sizeof k_vendor_init / sizeof k_vendor_init[0];

    p->base.del = panel_del;
    p->base.reset = panel_reset;
    p->base.init = panel_init;
    p->base.draw_bitmap = panel_draw_bitmap;
    p->base.invert_color = panel_invert_color;
    p->base.mirror = panel_mirror;
    p->base.swap_xy = panel_swap_xy;
    p->base.set_gap = panel_set_gap;
    p->base.disp_on_off = panel_disp_on_off;
    p->base.disp_sleep = panel_disp_sleep;

    *ret_panel = &p->base;
    return ESP_OK;
}
