#include "hal_input.h"

#include "bsp_s3w.h"

_Static_assert((int)HAL_BUTTON_BACK == (int)BSP_BUTTON_BOOT && (int)HAL_BUTTON_POWER == (int)BSP_BUTTON_PWR &&
                   (int)HAL_BUTTON_COUNT == (int)BSP_BUTTON_COUNT,
               "HAL buttons map 1:1 onto BSP buttons");

static hal_button_cb_t s_cb;
static void *s_ctx;

bool hal_touch_read(uint16_t *x, uint16_t *y)
{
    return bsp_touch_get(x, y);
}

static hal_touch_down_cb_t s_touch_cb;
static void *s_touch_ctx;

static void on_bsp_touch(uint16_t x, uint16_t y, void *ctx)
{
    (void)x;
    (void)y;
    (void)ctx;
    const hal_touch_down_cb_t cb = s_touch_cb;
    if (cb) {
        cb(s_touch_ctx);
    }
}

void hal_touch_set_down_cb(hal_touch_down_cb_t cb, void *ctx)
{
    s_touch_ctx = ctx;
    s_touch_cb = cb;
    bsp_touch_set_event_cb(cb ? on_bsp_touch : NULL, NULL);
}

_Static_assert(sizeof(hal_touch_contact_t) == sizeof(bsp_touch_contact_t), "same layout");

static hal_touch_contact_cb_t s_contact_cb;
static void *s_contact_ctx;

static void on_bsp_contact(const bsp_touch_contact_t *c, void *ctx)
{
    (void)ctx;
    const hal_touch_contact_cb_t cb = s_contact_cb;
    if (cb) {
        const hal_touch_contact_t hc = {
            .points = c->points,
            .area_max = c->area_max,
            .x_min = c->x_min,
            .y_min = c->y_min,
            .x_max = c->x_max,
            .y_max = c->y_max,
        };
        cb(&hc, s_contact_ctx);
    }
}

void hal_touch_set_contact_cb(hal_touch_contact_cb_t cb, void *ctx)
{
    s_contact_ctx = ctx;
    s_contact_cb = cb;
    bsp_touch_set_contact_cb(cb ? on_bsp_contact : NULL, NULL);
}

esp_err_t hal_touch_set_low_power(bool low_power)
{
    return bsp_touch_set_low_power(low_power);
}

static void on_bsp_button(bsp_button_t button, bool pressed, void *ctx)
{
    (void)ctx;
    if (s_cb) {
        s_cb((hal_button_t)button, pressed, s_ctx);
    }
}

esp_err_t hal_buttons_set_callback(hal_button_cb_t cb, void *ctx)
{
    s_ctx = ctx;
    s_cb = cb;
    return bsp_buttons_set_callback(cb ? on_bsp_button : NULL, NULL);
}

bool hal_button_is_pressed(hal_button_t button)
{
    return bsp_button_is_pressed((bsp_button_t)button);
}

const char *hal_button_name(hal_button_t button)
{
    return button == HAL_BUTTON_BACK ? "BACK" : button == HAL_BUTTON_POWER ? "POWER" : "?";
}
