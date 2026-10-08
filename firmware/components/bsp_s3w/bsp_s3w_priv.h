#pragma once

#include "esp_err.h"

esp_err_t bsp_buttons_init(void);

/** Read + dispatch AXP2101 IRQ status now (timer-task context). */
void bsp_pmu_poll(void);

#include "driver/gpio.h"

/** First thing in the ISR of a wake pin: if armed, back to its edge interrupt. */
void bsp_wake_isr_fired(gpio_num_t gpio);
/** Event handled (finger up, button released, alarm read): arm the pin again if
 *  its source is still requested and the pin is idle. Task context. */
void bsp_wake_rearm(gpio_num_t gpio);
