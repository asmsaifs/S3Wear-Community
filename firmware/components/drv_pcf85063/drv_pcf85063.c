#include "drv_pcf85063.h"

#include <stdlib.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "drv_pcf85063";

#define REG_CTRL1  0x00
#define REG_CTRL2  0x01
#define REG_OFFSET 0x02
#define REG_TIME   0x04
#define REG_ALARM  0x0B

#define CTRL1_STOP    (1 << 5)
#define CTRL1_12_24   (1 << 1)
#define CTRL2_AIE     (1 << 7)
#define CTRL2_AF      (1 << 6)
#define CTRL2_COF_OFF 0x07 // CLKOUT disabled

#define I2C_TIMEOUT_MS 50

struct pcf85063_s {
    i2c_master_dev_handle_t dev;
};

static esp_err_t rd(pcf85063_handle_t h, uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(h->dev, &reg, 1, data, len, I2C_TIMEOUT_MS);
}

static esp_err_t wr(pcf85063_handle_t h, uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t buf[1 + PCF85063_TIME_REG_COUNT];
    if (len > sizeof buf - 1) {
        return ESP_ERR_INVALID_SIZE;
    }
    buf[0] = reg;
    for (size_t i = 0; i < len; i++) {
        buf[1 + i] = data[i];
    }
    return i2c_master_transmit(h->dev, buf, len + 1, I2C_TIMEOUT_MS);
}

static esp_err_t wr8(pcf85063_handle_t h, uint8_t reg, uint8_t v)
{
    return wr(h, reg, &v, 1);
}

esp_err_t pcf85063_new(const pcf85063_config_t *cfg, pcf85063_handle_t *out)
{
    ESP_RETURN_ON_FALSE(cfg && cfg->bus && out, ESP_ERR_INVALID_ARG, TAG, "args");
    struct pcf85063_s *h = calloc(1, sizeof *h);
    ESP_RETURN_ON_FALSE(h, ESP_ERR_NO_MEM, TAG, "no mem");

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = cfg->addr,
        .scl_speed_hz = cfg->scl_hz,
    };
    esp_err_t ret = i2c_master_bus_add_device(cfg->bus, &dev_cfg, &h->dev);
    if (ret != ESP_OK) {
        free(h);
        return ret;
    }

    uint8_t ctrl[2];
    ESP_GOTO_ON_ERROR(rd(h, REG_CTRL1, ctrl, 2), fail, TAG, "not responding");
    // Run the clock in 24 h mode; keep CAP_SEL as strapped by the factory/previous boot.
    ESP_GOTO_ON_ERROR(wr8(h, REG_CTRL1, ctrl[0] & ~(CTRL1_STOP | CTRL1_12_24)), fail, TAG, "ctrl1");
    // Keep AIE/AF as they are (an alarm may have woken us); switch CLKOUT off.
    ESP_GOTO_ON_ERROR(wr8(h, REG_CTRL2, (ctrl[1] & (CTRL2_AIE | CTRL2_AF)) | CTRL2_COF_OFF), fail, TAG, "ctrl2");

    if (cfg->int_gpio >= 0 && cfg->isr_cb) {
        const gpio_config_t io = {
            .pin_bit_mask = 1ULL << cfg->int_gpio,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE, // INT is open-drain
            .intr_type = GPIO_INTR_NEGEDGE,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&io), fail, TAG, "int gpio");
        ESP_GOTO_ON_ERROR(gpio_isr_handler_add(cfg->int_gpio, cfg->isr_cb, cfg->isr_ctx), fail, TAG, "int isr");
    }
    *out = h;
    return ESP_OK;

fail:
    i2c_master_bus_rm_device(h->dev);
    free(h);
    return ret;
}

esp_err_t pcf85063_get_time(pcf85063_handle_t h, struct tm *utc, bool *osc_stopped)
{
    uint8_t regs[PCF85063_TIME_REG_COUNT];
    ESP_RETURN_ON_ERROR(rd(h, REG_TIME, regs, sizeof regs), TAG, "read time");
    ESP_RETURN_ON_FALSE(pcf85063_time_decode(regs, utc, osc_stopped), ESP_ERR_INVALID_RESPONSE, TAG, "bad BCD");
    return ESP_OK;
}

esp_err_t pcf85063_set_time(pcf85063_handle_t h, const struct tm *utc)
{
    uint8_t regs[PCF85063_TIME_REG_COUNT];
    ESP_RETURN_ON_FALSE(pcf85063_time_encode(utc, regs), ESP_ERR_INVALID_ARG, TAG, "year out of range");
    // One burst write: the time registers are latched for the whole access.
    return wr(h, REG_TIME, regs, sizeof regs);
}

esp_err_t pcf85063_set_alarm(pcf85063_handle_t h, const struct tm *utc)
{
    uint8_t regs[PCF85063_ALARM_REG_COUNT];
    pcf85063_alarm_encode(utc, regs);
    ESP_RETURN_ON_ERROR(wr(h, REG_ALARM, regs, sizeof regs), TAG, "alarm regs");
    uint8_t ctrl2;
    ESP_RETURN_ON_ERROR(rd(h, REG_CTRL2, &ctrl2, 1), TAG, "ctrl2");
    return wr8(h, REG_CTRL2, (ctrl2 | CTRL2_AIE) & ~CTRL2_AF);
}

esp_err_t pcf85063_disable_alarm(pcf85063_handle_t h)
{
    uint8_t ctrl2;
    ESP_RETURN_ON_ERROR(rd(h, REG_CTRL2, &ctrl2, 1), TAG, "ctrl2");
    return wr8(h, REG_CTRL2, ctrl2 & ~(CTRL2_AIE | CTRL2_AF));
}

esp_err_t pcf85063_check_alarm(pcf85063_handle_t h, bool *fired)
{
    uint8_t ctrl2;
    ESP_RETURN_ON_ERROR(rd(h, REG_CTRL2, &ctrl2, 1), TAG, "ctrl2");
    *fired = (ctrl2 & CTRL2_AF) != 0;
    return *fired ? wr8(h, REG_CTRL2, ctrl2 & ~CTRL2_AF) : ESP_OK;
}

esp_err_t pcf85063_set_offset(pcf85063_handle_t h, int8_t steps, bool coarse)
{
    return wr8(h, REG_OFFSET, pcf85063_offset_encode(steps, coarse));
}

esp_err_t pcf85063_get_offset(pcf85063_handle_t h, int8_t *steps, bool *coarse)
{
    uint8_t reg;
    ESP_RETURN_ON_ERROR(rd(h, REG_OFFSET, &reg, 1), TAG, "offset");
    *steps = pcf85063_offset_decode(reg, coarse);
    return ESP_OK;
}
