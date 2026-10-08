#include "hal_sim.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "hal.h"

static bool s_headless = true;

/* --- Display ------------------------------------------------------------------- */

static uint8_t s_brightness;

esp_err_t hal_display_set_brightness(uint8_t level)
{
    s_brightness = level; /* the window always shows full brightness */
    return ESP_OK;
}

uint8_t hal_display_get_brightness(void)
{
    return s_brightness;
}

esp_err_t hal_display_set_power(bool on)
{
    (void)on; /* the window stays visible */
    return ESP_OK;
}

/* --- Touch and buttons ---------------------------------------------------------- */

static hal_button_cb_t s_btn_cb;
static void *s_btn_ctx;
static bool s_btn_down[HAL_BUTTON_COUNT];
static bool s_inj_pressed;
static uint16_t s_inj_x;
static uint16_t s_inj_y;

void hal_sim_touch_inject(bool pressed, uint16_t x, uint16_t y)
{
    s_inj_pressed = pressed;
    s_inj_x = x;
    s_inj_y = y;
}

void hal_touch_set_down_cb(hal_touch_down_cb_t cb, void *ctx)
{
    (void)cb; /* no power service in the simulator yet */
    (void)ctx;
}

void hal_touch_set_contact_cb(hal_touch_contact_cb_t cb, void *ctx)
{
    (void)cb; /* no svc_input in the simulator yet (single mouse pointer: no palm) */
    (void)ctx;
}

esp_err_t hal_touch_set_low_power(bool low_power)
{
    (void)low_power;
    return ESP_OK;
}

bool hal_touch_read(uint16_t *x, uint16_t *y)
{
    if (s_headless) {
        *x = s_inj_x;
        *y = s_inj_y;
        return s_inj_pressed;
    }
    if (SDL_GetMouseFocus() == NULL) {
        return false;
    }
    int mx = 0;
    int my = 0;
    const uint32_t buttons = SDL_GetMouseState(&mx, &my);
    if (mx < 0 || my < 0 || mx >= HAL_DISPLAY_HRES || my >= HAL_DISPLAY_VRES) {
        return false;
    }
    *x = (uint16_t)mx;
    *y = (uint16_t)my;
    return (buttons & SDL_BUTTON_LMASK) != 0;
}

esp_err_t hal_buttons_set_callback(hal_button_cb_t cb, void *ctx)
{
    s_btn_ctx = ctx;
    s_btn_cb = cb;
    return ESP_OK;
}

bool hal_button_is_pressed(hal_button_t button)
{
    return button < HAL_BUTTON_COUNT && s_btn_down[button];
}

const char *hal_button_name(hal_button_t button)
{
    return button == HAL_BUTTON_BACK ? "BACK" : button == HAL_BUTTON_POWER ? "POWER" : "?";
}

/* --- RTC: host clock plus an offset set by hal_rtc_set() ------------------------ */

static time_t s_rtc_offset;
static time_t s_alarm_at; /* 0 = none */
static hal_rtc_alarm_cb_t s_alarm_cb;
static void *s_alarm_ctx;

esp_err_t hal_rtc_get(time_t *utc, bool *valid)
{
    *utc = time(NULL) + s_rtc_offset;
    *valid = true;
    return ESP_OK;
}

esp_err_t hal_rtc_set(time_t utc)
{
    s_rtc_offset = utc - time(NULL);
    return ESP_OK;
}

esp_err_t hal_rtc_set_offset(int8_t steps)
{
    (void)steps; /* the host clock needs no trim */
    return ESP_OK;
}

esp_err_t hal_rtc_set_alarm(time_t utc)
{
    s_alarm_at = utc;
    return ESP_OK;
}

esp_err_t hal_rtc_cancel_alarm(void)
{
    s_alarm_at = 0;
    return ESP_OK;
}

void hal_rtc_set_alarm_cb(hal_rtc_alarm_cb_t cb, void *ctx)
{
    s_alarm_ctx = ctx;
    s_alarm_cb = cb;
}

/* --- PMU: fake battery, C toggles the charger ----------------------------------- */

static hal_battery_t s_battery = {.percent = 80, .mv = 3950, .charging = false, .vbus = false};
static hal_pmu_event_cb_t s_pmu_cb;
static void *s_pmu_ctx;

esp_err_t hal_pmu_read_battery(hal_battery_t *out)
{
    *out = s_battery;
    return ESP_OK;
}

esp_err_t hal_pmu_set_event_cb(hal_pmu_event_cb_t cb, void *ctx)
{
    s_pmu_ctx = ctx;
    s_pmu_cb = cb;
    return ESP_OK;
}

esp_err_t hal_pmu_power_off(void)
{
    printf("hal_sim: power off\n");
    exit(0);
}

/* --- Sleep: the host never sleeps ------------------------------------------------ */

esp_err_t hal_power_init(void)
{
    return ESP_OK;
}

esp_err_t hal_power_arm_wake(uint32_t sources)
{
    (void)sources;
    return ESP_OK;
}

void hal_power_disarm_wake(void)
{
}

esp_err_t hal_power_deep_sleep(uint64_t timer_us, bool keep_display)
{
    printf("hal_sim: deep sleep (watch-only), timer %llu us, display %s\n", (unsigned long long)timer_us,
           keep_display ? "kept" : "off");
    exit(0);
}

static void pmu_event(hal_pmu_event_t evt)
{
    if (s_pmu_cb) {
        s_pmu_cb(evt, s_pmu_ctx);
    }
}

void hal_sim_battery_set(int percent, bool vbus)
{
    const bool plugged = vbus && !s_battery.vbus;
    const bool unplugged = !vbus && s_battery.vbus;
    s_battery.percent = (int8_t)percent;
    s_battery.mv = (uint16_t)(percent >= 0 ? 3400 + percent * 8 : 0); /* rough Li-ion curve */
    s_battery.vbus = vbus;
    s_battery.charging = vbus && percent >= 0 && percent < 100;
    if (plugged || unplugged) {
        pmu_event(plugged ? HAL_PMU_EVT_VBUS_IN : HAL_PMU_EVT_VBUS_OUT);
        if (s_battery.charging) {
            pmu_event(HAL_PMU_EVT_CHG_START);
        }
    }
}

/* --- SD card: a host directory named by $S3W_SIM_SD, else no card --------------- */

static bool s_sd_mounted;

static bool sd_dir_exists(void)
{
    const char *dir = getenv("S3W_SIM_SD");
    struct stat st;
    return dir && stat(dir, &st) == 0 && S_ISDIR(st.st_mode);
}

esp_err_t hal_sd_mount(const char *base_path)
{
    (void)base_path;
    s_sd_mounted = sd_dir_exists();
    return s_sd_mounted ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t hal_sd_unmount(void)
{
    s_sd_mounted = false;
    return ESP_OK;
}

bool hal_sd_is_mounted(void)
{
    return s_sd_mounted;
}

bool hal_sd_present(void)
{
    return s_sd_mounted && sd_dir_exists();
}

/* --- Main loop hooks ------------------------------------------------------------ */

void hal_sim_init(bool headless)
{
    s_headless = headless;
}

static void button_level(hal_button_t b, bool down)
{
    if (down != s_btn_down[b]) {
        s_btn_down[b] = down;
        if (s_btn_cb) {
            s_btn_cb(b, down, s_btn_ctx);
        }
    }
}

void hal_sim_button_inject(hal_button_t button, bool pressed)
{
    if (button < HAL_BUTTON_COUNT) {
        button_level(button, pressed);
    }
}

void hal_sim_poll(void)
{
    if (!s_headless) {
        /* LVGL's SDL driver pumps events; the keyboard state is current. */
        const uint8_t *keys = SDL_GetKeyboardState(NULL);
        button_level(HAL_BUTTON_BACK, keys[SDL_SCANCODE_ESCAPE] || keys[SDL_SCANCODE_BACKSPACE]);
        button_level(HAL_BUTTON_POWER, keys[SDL_SCANCODE_P]);

        static bool c_was;
        const bool c = keys[SDL_SCANCODE_C];
        if (c && !c_was) {
            hal_sim_battery_set(s_battery.percent, !s_battery.vbus);
        }
        c_was = c;
    }

    if (s_alarm_at && time(NULL) + s_rtc_offset >= s_alarm_at) {
        s_alarm_at = 0;
        if (s_alarm_cb) {
            s_alarm_cb(s_alarm_ctx);
        }
    }
}
