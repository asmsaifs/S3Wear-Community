// Touch: FT3168 on the shared I2C bus, read by a small INT-driven task so that the
// UI task never touches I2C (CLAUDE.md). Idle cost: zero wake-ups until INT fires;
// while a finger is down the task polls every TOUCH_POLL_MS to track movement and
// reports each sample to the contact callback (svc_input palm detection).
#include "bsp_s3w.h"

#include "bsp_s3w_pins.h"
#include "bsp_s3w_priv.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "bsp_touch";

#define TOUCH_TASK_PRIO  9
#define TOUCH_TASK_CORE  0
#define TOUCH_TASK_STACK 3072
#define TOUCH_POLL_MS    10 // FT3168 active report period is ~12 ms
#define TOUCH_PROBE_MS   20
// Large contact without coordinates: keep polling (palm detection) up to ~1 s.
#define BLOB_MAX_SAMPLES 100

static ft3168_handle_t s_tp;
static TaskHandle_t s_task;
static SemaphoreHandle_t s_io_lock; // reads vs. power-mode changes
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static struct {
    uint16_t x;
    uint16_t y;
    bool pressed;
} s_state;
static bsp_touch_event_cb_t s_event_cb;
static void *s_event_ctx;
static volatile bsp_touch_contact_cb_t s_contact_cb;
static void *s_contact_ctx;

static void touch_isr(void *ctx)
{
    (void)ctx;
    bsp_wake_isr_fired(BSP_PIN_TP_INT);
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_task, &woken);
    if (woken) {
        portYIELD_FROM_ISR();
    }
}

static void contact_summary(const ft3168_contact_t *c, bsp_touch_contact_t *out)
{
    *out = (bsp_touch_contact_t){.points = c->count, .x_min = UINT16_MAX, .y_min = UINT16_MAX};
    for (int i = 0; i < c->valid; i++) {
        const ft3168_point_t *p = &c->pt[i];
        out->x_min = p->x < out->x_min ? p->x : out->x_min;
        out->y_min = p->y < out->y_min ? p->y : out->y_min;
        out->x_max = p->x > out->x_max ? p->x : out->x_max;
        out->y_max = p->y > out->y_max ? p->y : out->y_max;
        out->area_max = p->area > out->area_max ? p->area : out->area_max;
    }
    if (c->valid == 0) {
        out->x_min = 0;
        out->y_min = 0;
    }
}

static void touch_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        bool contact;
        int blob_samples = 0;
        do {
            ft3168_contact_t c;
            xSemaphoreTake(s_io_lock, portMAX_DELAY);
            const esp_err_t err = ft3168_read_contact(s_tp, &c);
            xSemaphoreGive(s_io_lock);
            if (err != ESP_OK) {
                c = (ft3168_contact_t){0};
            }
            const bool down = c.valid > 0;
            // A count beyond the tracked points (large contact) has no coordinates but
            // still matters for palm detection; poll it for at most BLOB_MAX_SAMPLES.
            blob_samples = (!down && c.count > 0) ? blob_samples + 1 : 0;
            contact = down || (c.count > 0 && blob_samples <= BLOB_MAX_SAMPLES);
            portENTER_CRITICAL(&s_lock);
            const bool was_down = s_state.pressed;
            s_state.pressed = down;
            if (down) {
                s_state.x = c.pt[0].x;
                s_state.y = c.pt[0].y;
            }
            portEXIT_CRITICAL(&s_lock);
            if (down && !was_down && s_event_cb) {
                s_event_cb(c.pt[0].x, c.pt[0].y, s_event_ctx);
            }
            const bsp_touch_contact_cb_t contact_cb = s_contact_cb;
            if (contact_cb) {
                bsp_touch_contact_t sum;
                contact_summary(&c, &sum);
                if (!contact) {
                    sum.points = 0; // the final sample of this contact is always a release
                }
                contact_cb(&sum, s_contact_ctx);
            }
            if (contact) {
                vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_MS));
            }
        } while (contact);
        bsp_wake_rearm(BSP_PIN_TP_INT);
    }
}

esp_err_t bsp_touch_start(void)
{
    ESP_RETURN_ON_FALSE(bsp_i2c_bus(), ESP_ERR_INVALID_STATE, TAG, "bsp_init_early first");
    ESP_RETURN_ON_FALSE(!s_tp, ESP_ERR_INVALID_STATE, TAG, "already started");
    s_io_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_io_lock, ESP_ERR_NO_MEM, TAG, "lock");

    BaseType_t ok = xTaskCreatePinnedToCore(touch_task, "touch", TOUCH_TASK_STACK, NULL, TOUCH_TASK_PRIO, &s_task,
                                            TOUCH_TASK_CORE);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task");

    const ft3168_config_t cfg = {
        .bus = bsp_i2c_bus(),
        .addr = BSP_I2C_ADDR_FT3168,
        .rst_gpio = BSP_PIN_TP_RESET,
        .int_gpio = BSP_PIN_TP_INT,
        .x_max = BSP_LCD_H_RES,
        .y_max = BSP_LCD_V_RES,
        .scl_hz = BSP_I2C_FREQ_HZ,
        .isr_cb = touch_isr,
    };
    // esp_lcd_touch installs the GPIO ISR service itself and logs an error because
    // bsp_init_early() already did; silence that one known-harmless message.
    esp_log_level_set("gpio", ESP_LOG_NONE);
    const esp_err_t err = ft3168_new(&cfg, &s_tp);
    esp_log_level_set("gpio", ESP_LOG_INFO);
    ESP_RETURN_ON_ERROR(err, TAG, "ft3168");

    uint8_t id = 0;
    uint8_t fw = 0;
    if (ft3168_read_id(s_tp, &id, &fw) == ESP_OK) {
        ESP_LOGI(TAG, "FT3168 chip id 0x%02X fw 0x%02X", id, fw);
    }
    return ESP_OK;
}

bool bsp_touch_get(uint16_t *x, uint16_t *y)
{
    portENTER_CRITICAL(&s_lock);
    const bool pressed = s_state.pressed;
    *x = s_state.x;
    *y = s_state.y;
    portEXIT_CRITICAL(&s_lock);
    return pressed;
}

void bsp_touch_set_event_cb(bsp_touch_event_cb_t cb, void *ctx)
{
    s_event_ctx = ctx;
    s_event_cb = cb;
}

void bsp_touch_set_contact_cb(bsp_touch_contact_cb_t cb, void *ctx)
{
    s_contact_ctx = ctx;
    s_contact_cb = cb;
}

void bsp_touch_get_event_cb(bsp_touch_event_cb_t *cb, void **ctx)
{
    *cb = s_event_cb;
    *ctx = s_event_ctx;
}

esp_err_t bsp_touch_set_low_power(bool low_power)
{
    ESP_RETURN_ON_FALSE(s_tp, ESP_ERR_INVALID_STATE, TAG, "not started");
    xSemaphoreTake(s_io_lock, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    // No ACK: the controller is in its own idle monitor mode (see the header) and
    // a touch makes it active again, which is all either direction needs.
    if (i2c_master_probe(bsp_i2c_bus(), BSP_I2C_ADDR_FT3168, TOUCH_PROBE_MS) == ESP_OK) {
        err = ft3168_set_power_mode(s_tp, low_power ? FT3168_PMODE_MONITOR : FT3168_PMODE_ACTIVE);
    }
    xSemaphoreGive(s_io_lock);
    return err;
}

ft3168_handle_t bsp_touch_handle(void)
{
    return s_tp;
}
