// Input service: buttons -> presses -> actions, palm cover -> screen off (svc_input.h).
//
// Wake-ups: none while idle. A one-shot timer per button runs only while a press is
// pending (LONG while held, at most SVC_INPUT_POWER_LONG_MS; the double/triple gap,
// SVC_INPUT_MULTI_GAP_MS after a release). Touch samples come from the touch task,
// which polls only while a finger is down.
#include "svc_input.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "hal.h"
#include "palm_detect.h"
#include "s3w_event.h"
#include "svc_power.h"

static const char *TAG = "svc_input";

ESP_EVENT_DEFINE_BASE(SVC_INPUT_EVENT);

// Palm rules (palm_detect.h). The FT3168 tracks two points, so a larger count is a
// blob. Its per-point area scale is not documented: area_min stays off until it is
// measured on the watch (`input` prints the last contact).
#define PALM_COVER_PCT  60 // docs/03 F3
#define PALM_AREA_MIN   0
#define PALM_MAX_POINTS 2

_Static_assert((int)SVC_INPUT_BUTTON_BACK == (int)HAL_BUTTON_BACK &&
                   (int)SVC_INPUT_BUTTON_POWER == (int)HAL_BUTTON_POWER &&
                   (int)SVC_INPUT_BUTTON_COUNT == (int)HAL_BUTTON_COUNT,
               "svc_input buttons map 1:1 onto HAL buttons");

static const uint32_t k_long_ms[SVC_INPUT_BUTTON_COUNT] = {
    [SVC_INPUT_BUTTON_BACK] = SVC_INPUT_BACK_LONG_MS,
    [SVC_INPUT_BUTTON_POWER] = SVC_INPUT_POWER_LONG_MS,
};

static struct {
    bool started;
    TimerHandle_t timer[SVC_INPUT_BUTTON_COUNT];
    btn_gesture_fsm_t fsm[SVC_INPUT_BUTTON_COUNT];
    bool swallow[SVC_INPUT_BUTTON_COUNT]; // press woke the screen: ignore until release
    uint8_t map[SVC_INPUT_BUTTON_COUNT][BTN_GESTURE_COUNT]; // svc_input_action_t

    palm_detect_t palm;
    svc_input_stats_t stats;
    // Current contact (touch task), copied to stats.last_* on release.
    uint8_t cur_points;
    uint8_t cur_area;
    uint16_t cur_w;
    uint16_t cur_h;
    bool cur_covered;
} s;

// fsm, map and stats: the timer task, the touch task and API callers.
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool screen_on(void)
{
    return power_state_screen_on(svc_power_state());
}

static void post_event(int32_t id, const void *data, size_t len)
{
    if (s3w_event_post(SVC_INPUT_EVENT, id, data, len) != ESP_OK) {
        ESP_LOGW(TAG, "event %d dropped", (int)id);
    }
}

// Under s_lock.
static btn_gesture_cfg_t cfg_for(svc_input_button_t b)
{
    uint8_t clicks = 1;
    if (s.map[b][BTN_GESTURE_TRIPLE] != SVC_INPUT_ACTION_NONE) {
        clicks = 3;
    } else if (s.map[b][BTN_GESTURE_DOUBLE] != SVC_INPUT_ACTION_NONE) {
        clicks = 2;
    }
    return (btn_gesture_cfg_t){
        .long_ms = s.map[b][BTN_GESTURE_LONG] != SVC_INPUT_ACTION_NONE ? k_long_ms[b] : 0,
        .gap_ms = SVC_INPUT_MULTI_GAP_MS,
        .max_clicks = clicks,
    };
}

static void emit(svc_input_button_t b, btn_gesture_t g)
{
    if (g == BTN_GESTURE_NONE) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    s.stats.presses[b][g]++;
    const svc_input_action_t action = (svc_input_action_t)s.map[b][g];
    portEXIT_CRITICAL(&s_lock);

    ESP_LOGI(TAG, "%s %s -> %s", svc_input_button_name(b), btn_gesture_name(g), svc_input_action_name(action));
    const svc_input_evt_button_t evt = {.button = (uint8_t)b, .press = (uint8_t)g};
    post_event(SVC_INPUT_EVT_BUTTON, &evt, sizeof evt);
    if (action != SVC_INPUT_ACTION_NONE) {
        const svc_input_evt_action_t a = {.action = (uint8_t)action, .button = (uint8_t)b, .press = (uint8_t)g};
        post_event(SVC_INPUT_EVT_ACTION, &a, sizeof a);
    }
}

// Timer task (from the button callback or the button's own timer).
static void rearm(svc_input_button_t b, uint32_t now)
{
    portENTER_CRITICAL(&s_lock);
    const uint32_t ms = btn_gesture_ms_to_next(&s.fsm[b], now);
    portEXIT_CRITICAL(&s_lock);
    if (ms == BTN_GESTURE_NO_DEADLINE) {
        xTimerStop(s.timer[b], 0);
        return;
    }
    TickType_t ticks = pdMS_TO_TICKS(ms);
    xTimerChangePeriod(s.timer[b], ticks > 0 ? ticks : 1, 0); // also starts it
}

static void on_timer(TimerHandle_t t)
{
    const svc_input_button_t b = (svc_input_button_t)(uintptr_t)pvTimerGetTimerID(t);
    const uint32_t now = now_ms();
    portENTER_CRITICAL(&s_lock);
    const btn_gesture_t g = btn_gesture_tick(&s.fsm[b], now);
    portEXIT_CRITICAL(&s_lock);
    emit(b, g);
    rearm(b, now);
}

// Debounced edge, timer task (bsp_buttons).
static void on_button(hal_button_t button, bool pressed, void *ctx)
{
    (void)ctx;
    if (button >= HAL_BUTTON_COUNT) {
        return;
    }
    const svc_input_button_t b = (svc_input_button_t)button;
    const uint32_t now = now_ms();
    btn_gesture_t g;
    if (pressed) {
        if (!screen_on()) {
            s.swallow[b] = true; // this press only wakes
            portENTER_CRITICAL(&s_lock);
            s.stats.wake_presses++;
            portEXIT_CRITICAL(&s_lock);
            svc_power_wake(SVC_POWER_WAKE_BUTTON);
            return;
        }
        svc_power_user_activity();
        portENTER_CRITICAL(&s_lock);
        g = btn_gesture_press(&s.fsm[b], now);
        portEXIT_CRITICAL(&s_lock);
    } else {
        if (s.swallow[b]) {
            s.swallow[b] = false;
            return;
        }
        portENTER_CRITICAL(&s_lock);
        g = btn_gesture_release(&s.fsm[b], now);
        portEXIT_CRITICAL(&s_lock);
    }
    emit(b, g);
    rearm(b, now);
}

// Touch task, every sample while in contact and once on release.
static void on_contact(const hal_touch_contact_t *c, void *ctx)
{
    (void)ctx;
    const palm_sample_t smp = {
        .points = c->points,
        .area_max = c->area_max,
        .x_min = c->x_min,
        .y_min = c->y_min,
        .x_max = c->x_max,
        .y_max = c->y_max,
    };
    if (c->points == 0) {
        portENTER_CRITICAL(&s_lock);
        s.stats.last_points = s.cur_points;
        s.stats.last_area = s.cur_area;
        s.stats.last_w = s.cur_w;
        s.stats.last_h = s.cur_h;
        s.stats.last_covered = s.cur_covered;
        portEXIT_CRITICAL(&s_lock);
        s.cur_points = 0;
        s.cur_area = 0;
        s.cur_w = 0;
        s.cur_h = 0;
        s.cur_covered = false;
    } else {
        const uint16_t w = c->x_max >= c->x_min ? c->x_max - c->x_min + 1 : 0;
        const uint16_t h = c->y_max >= c->y_min ? c->y_max - c->y_min + 1 : 0;
        s.cur_points = c->points > s.cur_points ? c->points : s.cur_points;
        s.cur_area = c->area_max > s.cur_area ? c->area_max : s.cur_area;
        s.cur_w = w > s.cur_w ? w : s.cur_w;
        s.cur_h = h > s.cur_h ? h : s.cur_h;
        s.cur_covered |= palm_sample_covers(&s.palm.cfg, &smp);
    }

    if (!palm_detect_feed(&s.palm, &smp, now_ms()) || !screen_on()) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    s.stats.palms++;
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "palm cover (%u points, area %u): screen off", c->points, c->area_max);
    svc_power_screen_off();
    post_event(SVC_INPUT_EVT_PALM, NULL, 0);
}

// --- API ---------------------------------------------------------------------------

esp_err_t svc_input_start(void)
{
    ESP_RETURN_ON_FALSE(!s.started, ESP_ERR_INVALID_STATE, TAG, "already started");
    s.map[SVC_INPUT_BUTTON_BACK][BTN_GESTURE_SHORT] = SVC_INPUT_ACTION_BACK;
    s.map[SVC_INPUT_BUTTON_BACK][BTN_GESTURE_LONG] = SVC_INPUT_ACTION_SHORTCUT;
    s.map[SVC_INPUT_BUTTON_POWER][BTN_GESTURE_SHORT] = SVC_INPUT_ACTION_HOME;
    s.map[SVC_INPUT_BUTTON_POWER][BTN_GESTURE_LONG] = SVC_INPUT_ACTION_POWER_MENU;
    s.map[SVC_INPUT_BUTTON_POWER][BTN_GESTURE_TRIPLE] = SVC_INPUT_ACTION_SOS;

    for (int i = 0; i < SVC_INPUT_BUTTON_COUNT; i++) {
        const btn_gesture_cfg_t cfg = cfg_for((svc_input_button_t)i);
        btn_gesture_init(&s.fsm[i], &cfg);
        // Period is set on every start (rearm); 1 tick is a placeholder.
        s.timer[i] = xTimerCreate(svc_input_button_name((svc_input_button_t)i), 1, pdFALSE, (void *)(uintptr_t)i,
                                  on_timer);
        ESP_RETURN_ON_FALSE(s.timer[i], ESP_ERR_NO_MEM, TAG, "timer");
    }
    const palm_cfg_t palm = {
        .width = HAL_DISPLAY_HRES,
        .height = HAL_DISPLAY_VRES,
        .cover_pct = PALM_COVER_PCT,
        .area_min = PALM_AREA_MIN,
        .max_points = PALM_MAX_POINTS,
        .hold_ms = SVC_INPUT_PALM_HOLD_MS,
    };
    palm_detect_init(&s.palm, &palm);

    s.started = true;
    ESP_RETURN_ON_ERROR(hal_buttons_set_callback(on_button, NULL), TAG, "buttons");
    hal_touch_set_contact_cb(on_contact, NULL);
    ESP_LOGI(TAG, "started: long BOOT %d ms / PWR %d ms, multi-press gap %d ms", SVC_INPUT_BACK_LONG_MS,
             SVC_INPUT_POWER_LONG_MS, SVC_INPUT_MULTI_GAP_MS);
    return ESP_OK;
}

esp_err_t svc_input_set_action(svc_input_button_t button, btn_gesture_t press, svc_input_action_t action)
{
    ESP_RETURN_ON_FALSE(button < SVC_INPUT_BUTTON_COUNT && press > BTN_GESTURE_NONE && press < BTN_GESTURE_COUNT &&
                            action < SVC_INPUT_ACTION_COUNT,
                        ESP_ERR_INVALID_ARG, TAG, "args");
    portENTER_CRITICAL(&s_lock);
    s.map[button][press] = (uint8_t)action;
    const btn_gesture_cfg_t cfg = cfg_for(button);
    if (s.started) {
        btn_gesture_set_cfg(&s.fsm[button], &cfg); // a pending press is dropped (its timer finds nothing)
    }
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

svc_input_action_t svc_input_get_action(svc_input_button_t button, btn_gesture_t press)
{
    if (button >= SVC_INPUT_BUTTON_COUNT || press >= BTN_GESTURE_COUNT) {
        return SVC_INPUT_ACTION_NONE;
    }
    portENTER_CRITICAL(&s_lock);
    const svc_input_action_t a = (svc_input_action_t)s.map[button][press];
    portEXIT_CRITICAL(&s_lock);
    return a;
}

void svc_input_get_stats(svc_input_stats_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s.stats;
    portEXIT_CRITICAL(&s_lock);
}

const char *svc_input_button_name(svc_input_button_t button)
{
    return button == SVC_INPUT_BUTTON_BACK ? "BOOT" : button == SVC_INPUT_BUTTON_POWER ? "PWR" : "?";
}

const char *svc_input_action_name(svc_input_action_t action)
{
    static const char *const k_names[] = {
        [SVC_INPUT_ACTION_NONE] = "none",
        [SVC_INPUT_ACTION_BACK] = "back",
        [SVC_INPUT_ACTION_HOME] = "home",
        [SVC_INPUT_ACTION_POWER_MENU] = "power menu",
        [SVC_INPUT_ACTION_SHORTCUT] = "shortcut",
        [SVC_INPUT_ACTION_SOS] = "sos",
    };
    return (unsigned)action < SVC_INPUT_ACTION_COUNT ? k_names[action] : "?";
}
