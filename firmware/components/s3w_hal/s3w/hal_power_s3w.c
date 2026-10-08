#include "hal_power.h"

#include "bsp_s3w.h"

_Static_assert((int)HAL_WAKE_TOUCH == (int)BSP_WAKE_TOUCH && (int)HAL_WAKE_BUTTONS == (int)BSP_WAKE_BUTTONS &&
                   (int)HAL_WAKE_RTC == (int)BSP_WAKE_RTC && (int)HAL_WAKE_MOTION == (int)BSP_WAKE_IMU,
               "HAL wake sources map 1:1 onto BSP wake sources");

esp_err_t hal_power_init(void)
{
    return bsp_sleep_init();
}

esp_err_t hal_power_arm_wake(uint32_t sources)
{
    return bsp_wake_arm(sources);
}

void hal_power_disarm_wake(void)
{
    bsp_wake_disarm();
}

esp_err_t hal_power_deep_sleep(uint64_t timer_us, bool keep_display)
{
    return bsp_deep_sleep_start(timer_us, keep_display);
}
