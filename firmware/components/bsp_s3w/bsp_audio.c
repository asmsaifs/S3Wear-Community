// Audio: ES8311 + ES7210 on I2S0 with the NS4150B speaker amp on PA_CTRL.
#include "bsp_s3w.h"

#include "bsp_s3w_pins.h"
#include "drv_audio.h"
#include "esp_check.h"

static const char *TAG = "bsp_audio";

static bool s_started;

esp_err_t bsp_audio_start(void)
{
    ESP_RETURN_ON_FALSE(bsp_i2c_bus(), ESP_ERR_INVALID_STATE, TAG, "bsp_init_early first");
    if (s_started) {
        return ESP_OK;
    }
    const drv_audio_config_t cfg = {
        .bus = bsp_i2c_bus(),
        .es8311_addr = BSP_I2C_ADDR_ES8311,
        .es7210_addr = BSP_I2C_ADDR_ES7210,
        .mclk = BSP_PIN_I2S_MCLK,
        .bclk = BSP_PIN_I2S_BCLK,
        .ws = BSP_PIN_I2S_WS,
        .dout = BSP_PIN_I2S_DOUT,
        .din = BSP_PIN_I2S_DIN,
        .pa = BSP_PIN_PA_CTRL,
    };
    ESP_RETURN_ON_ERROR(drv_audio_init(&cfg), TAG, "drv_audio");
    s_started = true;
    return ESP_OK;
}

bool bsp_audio_ready(void)
{
    return s_started;
}
