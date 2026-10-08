// Light-sleep wake-up and pin states (svc_power, docs/02-firmware-architecture.md §7).
//
// ESP32-S3 light sleep can only be ended by a GPIO *level*, but the drivers use
// edge interrupts. bsp_wake_arm() switches each requested pin to its active level
// with wake-up enabled; the pin's ISR calls bsp_wake_isr_fired() first, which puts
// the edge interrupt back, so a held button or finger fires once instead of
// storming. When the handler is done with the event it calls bsp_wake_rearm().
// IMU INT1 has no active level (wake-on-motion toggles it on every event): it is
// armed for the level opposite to the one it has when armed.
//
// CONFIG_ESP_SLEEP_GPIO_RESET_WORKAROUND (default on the S3) isolates every pin in
// light sleep. Pins whose level must hold are taken out of that here.
#include "bsp_s3w.h"

#include "bsp_s3w_pins.h"
#include "bsp_s3w_priv.h"
#include "driver/rtc_io.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "bsp_sleep";

#define WAKE_TOGGLE GPIO_INTR_DISABLE // wake on the level opposite to the current one

typedef struct {
    gpio_num_t gpio;
    gpio_int_type_t wake_level; // light-sleep wake-up (level only), or WAKE_TOGGLE
    gpio_int_type_t normal;     // the driver's own interrupt type
    uint32_t source;            // BSP_WAKE_*
} wake_pin_t;

static const wake_pin_t k_wake_pins[] = {
    {BSP_PIN_TP_INT, GPIO_INTR_LOW_LEVEL, GPIO_INTR_NEGEDGE, BSP_WAKE_TOUCH},
    {BSP_PIN_BTN_BOOT, BSP_BTN_BOOT_ACTIVE_LVL ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL, GPIO_INTR_ANYEDGE,
     BSP_WAKE_BUTTONS},
    {BSP_PIN_BTN_PWR_SYSOUT, BSP_BTN_PWR_ACTIVE_LVL ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL, GPIO_INTR_ANYEDGE,
     BSP_WAKE_BUTTONS},
    {BSP_PIN_RTC_INT, GPIO_INTR_LOW_LEVEL, GPIO_INTR_NEGEDGE, BSP_WAKE_RTC},
    {BSP_PIN_IMU_INT1, WAKE_TOGGLE, GPIO_INTR_ANYEDGE, BSP_WAKE_IMU},
};
#define WAKE_PIN_COUNT (sizeof k_wake_pins / sizeof k_wake_pins[0])

// Levels that must hold through light sleep: amp off, panel/touch out of reset,
// LCD and SD deselected, I2C idle high.
static const gpio_num_t k_keep_pins[] = {
    BSP_PIN_PA_CTRL, BSP_PIN_LCD_RESET, BSP_PIN_LCD_CS, BSP_PIN_TP_RESET,
    BSP_PIN_SD_CS,   BSP_PIN_I2C_SCL,   BSP_PIN_I2C_SDA,
};

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_requested; // BSP_WAKE_* asked for by bsp_wake_arm()
static uint32_t s_armed;     // bit i = k_wake_pins[i] is in level/wake mode

static int pin_index(gpio_num_t gpio)
{
    for (int i = 0; i < (int)WAKE_PIN_COUNT; i++) {
        if (k_wake_pins[i].gpio == gpio) {
            return i;
        }
    }
    return -1;
}

// Caller holds s_lock.
static void arm_pin(int i)
{
    const wake_pin_t *p = &k_wake_pins[i];
    gpio_int_type_t level = p->wake_level;
    if (level == WAKE_TOGGLE) {
        level = gpio_get_level(p->gpio) ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL;
    }
    gpio_wakeup_enable(p->gpio, level); // sets the level interrupt type too
    s_armed |= 1u << i;
}

static void disarm_pin(int i)
{
    const wake_pin_t *p = &k_wake_pins[i];
    gpio_wakeup_disable(p->gpio);
    gpio_set_intr_type(p->gpio, p->normal);
    s_armed &= ~(1u << i);
}

static bool pin_active(const wake_pin_t *p)
{
    if (p->wake_level == WAKE_TOGGLE) {
        return false; // never "held"
    }
    const int level = gpio_get_level(p->gpio);
    return p->wake_level == GPIO_INTR_HIGH_LEVEL ? level == 1 : level == 0;
}

esp_err_t bsp_sleep_init(void)
{
    for (size_t i = 0; i < sizeof k_keep_pins / sizeof k_keep_pins[0]; i++) {
        ESP_RETURN_ON_ERROR(gpio_sleep_sel_dis(k_keep_pins[i]), TAG, "keep gpio %d", k_keep_pins[i]);
    }
    return esp_sleep_enable_gpio_wakeup();
}

esp_err_t bsp_wake_arm(uint32_t sources)
{
    portENTER_CRITICAL(&s_lock);
    s_requested = sources;
    for (int i = 0; i < (int)WAKE_PIN_COUNT; i++) {
        const bool want = (sources & k_wake_pins[i].source) != 0;
        const bool armed = (s_armed & (1u << i)) != 0;
        if (want && !armed && !pin_active(&k_wake_pins[i])) {
            arm_pin(i);
        } else if (!want && armed) {
            disarm_pin(i);
        }
    }
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

void bsp_wake_disarm(void)
{
    portENTER_CRITICAL(&s_lock);
    s_requested = 0;
    for (int i = 0; i < (int)WAKE_PIN_COUNT; i++) {
        if (s_armed & (1u << i)) {
            disarm_pin(i);
        }
    }
    portEXIT_CRITICAL(&s_lock);
}

void bsp_wake_isr_fired(gpio_num_t gpio)
{
    const int i = pin_index(gpio);
    if (i < 0) {
        return;
    }
    portENTER_CRITICAL_ISR(&s_lock);
    if (s_armed & (1u << i)) {
        disarm_pin(i);
    }
    portEXIT_CRITICAL_ISR(&s_lock);
}

void bsp_wake_rearm(gpio_num_t gpio)
{
    const int i = pin_index(gpio);
    if (i < 0) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    const wake_pin_t *p = &k_wake_pins[i];
    if ((s_requested & p->source) && !(s_armed & (1u << i)) && !pin_active(p)) {
        arm_pin(i);
    }
    portEXIT_CRITICAL(&s_lock);
}

esp_err_t bsp_deep_sleep_start(uint64_t timer_us, bool keep_display)
{
    // GPIO10 is an RTC IO; SYS_OUT is driven by the PMU, high while PWR is held.
    ESP_RETURN_ON_ERROR(esp_sleep_enable_ext1_wakeup_io(1ULL << BSP_PIN_BTN_PWR_SYSOUT, ESP_EXT1_WAKEUP_ANY_HIGH),
                        TAG, "ext1");
    // The RTC INT (GPIO39) is not an RTC IO: an alarm wakes through the RTC timer.
    if (timer_us > 0) {
        ESP_RETURN_ON_ERROR(esp_sleep_enable_timer_wakeup(timer_us), TAG, "timer");
    }
    gpio_set_level(BSP_PIN_PA_CTRL, 0);
    gpio_hold_en(BSP_PIN_PA_CTRL); // GPIO46 is not an RTC IO: hold the digital pad
    if (keep_display) {
        // Panel out of reset and deselected (CS taken back from the SPI peripheral as a
        // plain output, high first). Both are RTC IOs: the hold lasts through the
        // wake-up until bsp_display_new() releases it.
        const gpio_num_t keep[] = {BSP_PIN_LCD_RESET, BSP_PIN_LCD_CS};
        for (size_t i = 0; i < sizeof keep / sizeof keep[0]; i++) {
            gpio_set_level(keep[i], 1);
            const gpio_config_t out = {.pin_bit_mask = 1ULL << keep[i], .mode = GPIO_MODE_OUTPUT};
            gpio_config(&out);
            gpio_set_level(keep[i], 1);
            gpio_hold_en(keep[i]);
        }
        esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON); // RTC IO holds
    }
    gpio_deep_sleep_hold_en();
    ESP_LOGI(TAG, "deep sleep until PWR%s%s", timer_us ? " or the timer" : "", keep_display ? ", panel on" : "");
    esp_deep_sleep_start();
    return ESP_FAIL; // not reached
}
