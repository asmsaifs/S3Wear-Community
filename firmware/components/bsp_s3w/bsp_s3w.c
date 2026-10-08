#include "bsp_s3w.h"

#include "bsp_s3w_pins.h"
#include "bsp_s3w_priv.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "bsp_s3w";

static i2c_master_bus_handle_t s_i2c_bus;

static const struct {
    uint8_t addr;
    const char *name;
} k_i2c_devices[BSP_I2C_DEVICE_COUNT] = {
    {BSP_I2C_ADDR_ES8311, "ES8311"},
    {BSP_I2C_ADDR_AXP2101, "AXP2101"},
    {BSP_I2C_ADDR_FT3168, "FT3168"},
    {BSP_I2C_ADDR_ES7210, "ES7210"},
    {BSP_I2C_ADDR_PCF85063, "PCF85063"},
    {BSP_I2C_ADDR_QMI8658, "QMI8658"},
};

static esp_err_t pa_ctrl_low(void)
{
    // Speaker amp off. GPIO46 is a strapping pin: only driven after reset.
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BSP_PIN_PA_CTRL,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_set_level(BSP_PIN_PA_CTRL, 0), TAG, "PA level");
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "PA gpio");
    return gpio_hold_dis(BSP_PIN_PA_CTRL); // held low through WATCH-ONLY deep sleep

}

static esp_err_t i2c_bus_init(void)
{
    const i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = BSP_PIN_I2C_SDA,
        .scl_io_num = BSP_PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true, // board has external pull-ups; internal ones are harmless
    };
    return i2c_new_master_bus(&cfg, &s_i2c_bus);
}

esp_err_t bsp_init_early(void)
{
    if (s_i2c_bus) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(pa_ctrl_low(), TAG, "PA_CTRL");
    ESP_RETURN_ON_ERROR(i2c_bus_init(), TAG, "I2C bus");
    ESP_RETURN_ON_ERROR(bsp_buttons_init(), TAG, "buttons");
    ESP_LOGI(TAG, "early init done (I2C %d kHz)", BSP_I2C_FREQ_HZ / 1000);
    return ESP_OK;
}

i2c_master_bus_handle_t bsp_i2c_bus(void)
{
    return s_i2c_bus;
}

esp_err_t bsp_i2c_add_device(uint8_t addr_7bit, i2c_master_dev_handle_t *out_dev)
{
    ESP_RETURN_ON_FALSE(s_i2c_bus && out_dev, ESP_ERR_INVALID_STATE, TAG, "bus not ready");
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr_7bit,
        .scl_speed_hz = BSP_I2C_FREQ_HZ,
    };
    return i2c_master_bus_add_device(s_i2c_bus, &cfg, out_dev);
}

uint8_t bsp_i2c_device_addr(size_t idx)
{
    return idx < BSP_I2C_DEVICE_COUNT ? k_i2c_devices[idx].addr : 0;
}

const char *bsp_i2c_device_name(uint8_t addr_7bit)
{
    for (size_t i = 0; i < BSP_I2C_DEVICE_COUNT; i++) {
        if (k_i2c_devices[i].addr == addr_7bit) {
            return k_i2c_devices[i].name;
        }
    }
    return NULL;
}
