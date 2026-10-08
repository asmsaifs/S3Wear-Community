#include "hal_display.h"

#include "bsp_s3w.h"
#include "bsp_s3w_pins.h"

_Static_assert(HAL_DISPLAY_HRES == BSP_LCD_H_RES && HAL_DISPLAY_VRES == BSP_LCD_V_RES, "HAL display size");

static uint8_t s_brightness;

esp_err_t hal_display_set_brightness(uint8_t level)
{
    const esp_err_t err = bsp_display_brightness_set(level);
    if (err == ESP_OK) {
        s_brightness = level;
    }
    return err;
}

uint8_t hal_display_get_brightness(void)
{
    return s_brightness;
}

esp_err_t hal_display_set_power(bool on)
{
    return bsp_display_power(on);
}
