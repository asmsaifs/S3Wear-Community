// AMOLED bring-up: QSPI bus + CO5300 panel, and the LCD_TE tearing-effect line.
#include "bsp_s3w.h"

#include "bsp_s3w_pins.h"
#include "driver/spi_master.h"
#include "drv_co5300.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "bsp_display";

#define LCD_SPI_HOST SPI2_HOST

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_te_sem;

static void IRAM_ATTR te_isr(void *arg)
{
    (void)arg;
    BaseType_t woken = pdFALSE;
    // One-shot: disarm here (by interrupt type), re-armed by bsp_display_te_wait().
    gpio_set_intr_type(BSP_PIN_LCD_TE, GPIO_INTR_DISABLE);
    xSemaphoreGiveFromISR(s_te_sem, &woken);
    if (woken) {
        portYIELD_FROM_ISR();
    }
}

static esp_err_t te_init(void)
{
    s_te_sem = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_te_sem, ESP_ERR_NO_MEM, TAG, "te sem");
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BSP_PIN_LCD_TE,
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_DISABLE,
    };
    // The interrupt stays disarmed except while a flush waits for TE, so an idle
    // display does not wake the CPU at 60 Hz.
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "te gpio");
    return gpio_isr_handler_add(BSP_PIN_LCD_TE, te_isr, NULL);
}

esp_err_t bsp_display_new(size_t max_transfer_bytes, const bsp_display_cbs_t *cbs, bool keep_frame,
                          esp_lcd_panel_handle_t *out_panel, esp_lcd_panel_io_handle_t *out_io)
{
    ESP_RETURN_ON_FALSE(!s_panel, ESP_ERR_INVALID_STATE, TAG, "already created");
    ESP_RETURN_ON_FALSE(out_panel && out_io, ESP_ERR_INVALID_ARG, TAG, "args");

    const spi_bus_config_t bus = {
        .sclk_io_num = BSP_PIN_LCD_SCLK,
        .data0_io_num = BSP_PIN_LCD_SIO0,
        .data1_io_num = BSP_PIN_LCD_SIO1,
        .data2_io_num = BSP_PIN_LCD_SIO2,
        .data3_io_num = BSP_PIN_LCD_SIO3,
        .max_transfer_sz = (int)max_transfer_bytes,
        .flags = SPICOMMON_BUSFLAG_QUAD,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "spi bus");

    const esp_lcd_panel_io_spi_config_t io_cfg = CO5300_PANEL_IO_QSPI_CONFIG(
        BSP_PIN_LCD_CS, CONFIG_S3W_LCD_PCLK_MHZ * 1000 * 1000, cbs ? cbs->on_color_trans_done : NULL,
        cbs ? cbs->user_ctx : NULL);
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_cfg, &s_io), TAG, "io");

    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = BSP_PIN_LCD_RESET,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(co5300_new_panel(s_io, &panel_cfg, &s_panel), TAG, "panel");
    // Reset and CS may still be held from a WATCH-ONLY deep sleep (bsp_deep_sleep_start):
    // drive reset high (inactive) before the pad follows the driver again.
    gpio_set_level(BSP_PIN_LCD_RESET, 1);
    gpio_hold_dis(BSP_PIN_LCD_RESET);
    gpio_hold_dis(BSP_PIN_LCD_CS);
    if (!keep_frame) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset");
        ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "init");
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, BSP_LCD_X_GAP, BSP_LCD_Y_GAP), TAG, "gap");
    ESP_RETURN_ON_ERROR(te_init(), TAG, "te");

    ESP_LOGI(TAG, "CO5300 %dx%d, QSPI %d MHz%s", BSP_LCD_H_RES, BSP_LCD_V_RES, CONFIG_S3W_LCD_PCLK_MHZ,
             keep_frame ? ", attached (kept on through deep sleep)" : "");
    *out_panel = s_panel;
    *out_io = s_io;
    return ESP_OK;
}

esp_lcd_panel_handle_t bsp_display_panel(void)
{
    return s_panel;
}

esp_err_t bsp_display_te_wait(uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(s_te_sem, ESP_ERR_INVALID_STATE, TAG, "no te");
    xSemaphoreTake(s_te_sem, 0); // drop a stale edge
    gpio_set_intr_type(BSP_PIN_LCD_TE, GPIO_INTR_POSEDGE);
    if (xSemaphoreTake(s_te_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        gpio_set_intr_type(BSP_PIN_LCD_TE, GPIO_INTR_DISABLE);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t bsp_display_brightness_set(uint8_t level)
{
    ESP_RETURN_ON_FALSE(s_panel, ESP_ERR_INVALID_STATE, TAG, "no panel");
    return co5300_set_brightness(s_panel, level);
}

esp_err_t bsp_display_power(bool on)
{
    ESP_RETURN_ON_FALSE(s_panel, ESP_ERR_INVALID_STATE, TAG, "no panel");
    if (on) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_sleep(s_panel, false), TAG, "sleep out"); // waits 120 ms
        return esp_lcd_panel_disp_on_off(s_panel, true);
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, false), TAG, "display off");
    return esp_lcd_panel_disp_sleep(s_panel, true);
}

int bsp_display_te_count_edges(uint32_t window_ms)
{
    // Busy-poll the TE line (diagnostics only): count rising edges in the window.
    int edges = 0;
    int last = gpio_get_level(BSP_PIN_LCD_TE);
    const int64_t end = esp_timer_get_time() + (int64_t)window_ms * 1000;
    while (esp_timer_get_time() < end) {
        const int lvl = gpio_get_level(BSP_PIN_LCD_TE);
        edges += lvl && !last;
        last = lvl;
    }
    return edges;
}
