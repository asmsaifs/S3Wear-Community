// BOOT (GPIO0) and PWR SYS_OUT (GPIO10) buttons with edge interrupt + debounce.
// Event driven: no polling. Any edge restarts a 20 ms one-shot FreeRTOS timer; the
// timer callback samples the pin and reports a change if the level is stable.
#include "bsp_s3w.h"

#include "bsp_s3w_pins.h"
#include "bsp_s3w_priv.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

static const char *TAG = "bsp_buttons";

#define DEBOUNCE_MS 20

typedef struct {
    gpio_num_t gpio;
    int active_level;
    const char *name;
    TimerHandle_t timer;
    volatile bool pressed;
} button_t;

static button_t s_buttons[BSP_BUTTON_COUNT] = {
    [BSP_BUTTON_BOOT] = {.gpio = BSP_PIN_BTN_BOOT, .active_level = BSP_BTN_BOOT_ACTIVE_LVL, .name = "BOOT"},
    [BSP_BUTTON_PWR] = {.gpio = BSP_PIN_BTN_PWR_SYSOUT, .active_level = BSP_BTN_PWR_ACTIVE_LVL, .name = "PWR"},
};

static bsp_button_cb_t s_cb;
static void *s_cb_ctx;

static void IRAM_ATTR button_isr(void *arg)
{
    button_t *b = arg;
    bsp_wake_isr_fired(b->gpio);
    BaseType_t woken = pdFALSE;
    xTimerResetFromISR(b->timer, &woken);
    if (woken) {
        portYIELD_FROM_ISR();
    }
}

static void debounce_cb(TimerHandle_t t)
{
    const bsp_button_t id = (bsp_button_t)(uintptr_t)pvTimerGetTimerID(t);
    button_t *b = &s_buttons[id];
    const bool pressed = gpio_get_level(b->gpio) == b->active_level;
    if (pressed == b->pressed) {
        return;
    }
    b->pressed = pressed;
    if (id == BSP_BUTTON_PWR) {
        bsp_pmu_poll(); // the AXP2101 latches short/long-press IRQs; read them right away
    }
    if (s_cb) {
        s_cb(id, pressed, s_cb_ctx);
    }
    if (!pressed) {
        bsp_wake_rearm(b->gpio);
    }
}

esp_err_t bsp_buttons_init(void)
{
    esp_err_t err = gpio_install_isr_service(0);
    ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_INVALID_STATE, err, TAG, "ISR service");

    for (int i = 0; i < BSP_BUTTON_COUNT; i++) {
        button_t *b = &s_buttons[i];
        const gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << b->gpio,
            .mode = GPIO_MODE_INPUT,
            // BOOT idles high (pull-up); SYS_OUT is driven by the PMU side.
            .pull_up_en = b->active_level == 0 ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
            .intr_type = GPIO_INTR_ANYEDGE,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "gpio %d", b->gpio);
        b->timer = xTimerCreate(b->name, pdMS_TO_TICKS(DEBOUNCE_MS), pdFALSE, (void *)(uintptr_t)i, debounce_cb);
        ESP_RETURN_ON_FALSE(b->timer, ESP_ERR_NO_MEM, TAG, "timer");
        b->pressed = gpio_get_level(b->gpio) == b->active_level;
        ESP_RETURN_ON_ERROR(gpio_isr_handler_add(b->gpio, button_isr, b), TAG, "isr add");
    }
    return ESP_OK;
}

esp_err_t bsp_buttons_set_callback(bsp_button_cb_t cb, void *ctx)
{
    s_cb_ctx = ctx;
    s_cb = cb;
    return ESP_OK;
}

bool bsp_button_is_pressed(bsp_button_t button)
{
    return button < BSP_BUTTON_COUNT && s_buttons[button].pressed;
}

const char *bsp_button_name(bsp_button_t button)
{
    return button < BSP_BUTTON_COUNT ? s_buttons[button].name : "?";
}
