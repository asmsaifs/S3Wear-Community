#include "drv_ft3168.h"

#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "drv_ft3168";

#define REG_TD_STATUS 0x02 // point count, then 6 bytes per point from 0x03
#define POINT_BYTES   6
#define COUNT_MAX     5    // larger counts are not point data (esp_lcd_touch_ft5x06 does the same)
#define REG_G_CTRL  0x86 // 0 = stay active, 1 = auto-enter monitor when idle
#define REG_CHIP_ID 0xA0
#define REG_PMODE   0xA5
#define REG_FW_VER  0xA6

struct ft3168_s {
    esp_lcd_panel_io_handle_t io;
    esp_lcd_touch_handle_t tp;
    ft3168_config_t cfg;
};

static void int_cb(esp_lcd_touch_handle_t tp)
{
    struct ft3168_s *h = tp->config.user_data;
    if (h && h->cfg.isr_cb) {
        h->cfg.isr_cb(h->cfg.isr_ctx);
    }
}

// The controller boots for a while after reset (registers read 0 before that), and by
// default drops into monitor mode after 2 s idle, where it stops answering I2C until
// touched. Clearing ID_G_CTRL only delays that to ~25 s (P2-06, docs/01 §6); INT and
// touches keep working throughout.
#define BOOT_WAIT_MS 150

static esp_err_t touch_create(struct ft3168_s *h)
{
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = h->cfg.x_max,
        .y_max = h->cfg.y_max,
        .rst_gpio_num = h->cfg.rst_gpio,
        .int_gpio_num = h->cfg.int_gpio,
        .levels = {.reset = 0, .interrupt = 0},
        .interrupt_callback = h->cfg.isr_cb ? int_cb : NULL,
        .user_data = h,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_ft5x06(h->io, &tp_cfg, &h->tp), TAG, "ft5x06");
    vTaskDelay(pdMS_TO_TICKS(BOOT_WAIT_MS));
    const uint8_t keep_active = 0;
    return esp_lcd_panel_io_tx_param(h->io, REG_G_CTRL, &keep_active, 1);
}

esp_err_t ft3168_new(const ft3168_config_t *cfg, ft3168_handle_t *out)
{
    ESP_RETURN_ON_FALSE(cfg && cfg->bus && out, ESP_ERR_INVALID_ARG, TAG, "args");
    struct ft3168_s *h = calloc(1, sizeof *h);
    ESP_RETURN_ON_FALSE(h, ESP_ERR_NO_MEM, TAG, "no mem");
    h->cfg = *cfg;

    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    io_cfg.dev_addr = cfg->addr;
    io_cfg.scl_speed_hz = cfg->scl_hz;
    esp_err_t err = esp_lcd_new_panel_io_i2c(cfg->bus, &io_cfg, &h->io);
    if (err == ESP_OK) {
        err = touch_create(h);
    }
    if (err != ESP_OK) {
        if (h->io) {
            esp_lcd_panel_io_del(h->io);
        }
        free(h);
        return err;
    }
    *out = h;
    return ESP_OK;
}

bool ft3168_read(ft3168_handle_t h, uint16_t *x, uint16_t *y)
{
    if (esp_lcd_touch_read_data(h->tp) != ESP_OK) {
        return false;
    }
    esp_lcd_touch_point_data_t pt;
    uint8_t n = 0;
    if (esp_lcd_touch_get_data(h->tp, &pt, &n, 1) != ESP_OK || n == 0) {
        return false;
    }
    *x = pt.x;
    *y = pt.y;
    return true;
}

esp_err_t ft3168_read_contact(ft3168_handle_t h, ft3168_contact_t *out)
{
    uint8_t buf[1 + POINT_BYTES * FT3168_MAX_POINTS];
    memset(out, 0, sizeof *out);
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_rx_param(h->io, REG_TD_STATUS, buf, sizeof buf), TAG, "points");
    out->count = buf[0] & 0x0F;
    if (out->count > COUNT_MAX) {
        return ESP_OK;
    }
    out->valid = out->count < FT3168_MAX_POINTS ? out->count : FT3168_MAX_POINTS;
    for (int i = 0; i < out->valid; i++) {
        const uint8_t *p = &buf[1 + i * POINT_BYTES];
        out->pt[i] = (ft3168_point_t){
            .x = (uint16_t)(((p[0] & 0x0F) << 8) | p[1]),
            .y = (uint16_t)(((p[2] & 0x0F) << 8) | p[3]),
            .weight = p[4],
            .area = p[5] >> 4,
        };
    }
    return ESP_OK;
}

esp_err_t ft3168_set_power_mode(ft3168_handle_t h, ft3168_pmode_t mode)
{
    const uint8_t v = (uint8_t)mode;
    return esp_lcd_panel_io_tx_param(h->io, REG_PMODE, &v, 1);
}

esp_err_t ft3168_get_power_mode(ft3168_handle_t h, ft3168_pmode_t *mode)
{
    uint8_t v = 0;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_rx_param(h->io, REG_PMODE, &v, 1), TAG, "pmode");
    *mode = (ft3168_pmode_t)(v & 0x03);
    return ESP_OK;
}

esp_err_t ft3168_read_id(ft3168_handle_t h, uint8_t *chip_id, uint8_t *fw_ver)
{
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_rx_param(h->io, REG_CHIP_ID, chip_id, 1), TAG, "id");
    return esp_lcd_panel_io_rx_param(h->io, REG_FW_VER, fw_ver, 1);
}

esp_err_t ft3168_reset(ft3168_handle_t h)
{
    ESP_RETURN_ON_FALSE(h->cfg.rst_gpio >= 0, ESP_ERR_NOT_SUPPORTED, TAG, "no reset pin");
    esp_lcd_touch_del(h->tp);
    h->tp = NULL;
    return touch_create(h);
}
