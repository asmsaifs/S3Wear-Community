// Power service: applies power_fsm states to the panel, touch, UI, PM locks and wake
// inputs; reads the battery (docs/02 §7). Buttons belong to svc_input.
//
// Wake-ups of the svc_power task: requests on its queue (event driven), the
// state machine's next deadline (DIM / screen off, only while the screen is on)
// and one battery read every BATTERY_POLL_MS. In WATCH-ONLY the chip deep-sleeps
// and boots once a minute to redraw the time (watch_only.h; app_main's tick boot).
#include "svc_power.h"

#include <string.h>
#include <sys/time.h>

#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "hal.h"
#include "s3w_event.h"
#include "s3w_task.h"
#include "s3w_ui.h"
#include "sdkconfig.h"
#include "svc_modes.h"
#include "svc_settings.h"
#include "watch_only.h"

static const char *TAG = "svc_power";

ESP_EVENT_DEFINE_BASE(SVC_POWER_EVENT);

#define QUEUE_LEN          16
#define UI_HOOK_TIMEOUT_MS 500
// Fuel gauge %: at the worst case (screen on, ~70 mA from 400 mAh) it moves 1 %
// every ~3.4 min, so once a minute is plenty; one I2C read. Charger and USB
// changes are read at once from the PMU events.
#define BATTERY_POLL_MS 60000
#define BATTERY_MAH     400 // kit cell, docs/01-hardware.md §4
#define DIM_PCT         30  // DIM: this share of the user brightness (docs/02 §7)
#define AOD_PCT         10  // AOD brightness, % of full (docs/01 §5: 5-15 %)
#define PM_MIN_MHZ      40  // DFS floor (XTAL)
// Battery saver caps (docs/03 F5): brightness and screen timeout.
#define SAVER_BRIGHTNESS_PCT 50
#define SAVER_TIMEOUT_S      10
// Low-battery thresholds that wake the screen for their alert (docs/03 F5).
#define LOW_WAKE_PCT         10
#define WATCH_ONLY_MAGIC   0x57A7C40Cu

typedef enum {
    MSG_WAKE,       // a = svc_power_wake_t
    MSG_ACTIVITY,
    MSG_TOUCH,
    MSG_SCREEN_OFF,
    MSG_HOLD,       // a = hold
    MSG_PMU,        // a = hal_pmu_event_t
    MSG_SETTINGS,
    MSG_MODES,      // a = MODES_* bits
    MSG_WATCH_ONLY,
    MSG_SHUTDOWN,
    MSG_RESTART,
} msg_type_t;

typedef struct {
    uint8_t type;
    uint8_t a;
    uint8_t b;
} msg_t;

// svc_modes state as MSG_MODES carries it.
#define MODES_QUIET   0x01u
#define MODES_DARK    0x02u
#define MODES_THEATER 0x04u

static const uint8_t k_low_thresholds[] = {3, 10, 15}; // ascending, docs/03 F5

static struct {
    QueueHandle_t queue;
    TaskHandle_t task;
    svc_power_ui_hook_t ui_hook;
    esp_pm_lock_handle_t lock_cpu;   // ESP_PM_CPU_FREQ_MAX while the screen is on
    esp_pm_lock_handle_t lock_awake; // ESP_PM_NO_LIGHT_SLEEP while the screen is on
    bool locks_held;

    power_fsm_t fsm;
    volatile power_state_t applied;
    uint8_t brightness; // user level 0..255
    bool wake_on_tap;
    bool wake_on_raise; // arm the IMU interrupt (svc_sensors drives it)
    bool raise_unused;  // woken by a raise and not touched since (false-wake count)
    uint8_t modes;      // MODES_* (svc_modes): quiet, dark, theater
    int64_t wake_t0_us; // screen wake latency (log)
    svc_power_wake_t wake_reason;

    svc_power_battery_t battery;
    bool battery_valid;
    battery_hist_t hist; // s_stats_lock
    uint32_t battery_next_ms;
    uint8_t low_reported; // lowest threshold posted since unplugged (100 = none)

    // Stats (copied out under s_stats_lock)
    uint64_t state_ms[POWER_STATE_COUNT];
    uint32_t state_since_ms;
    uint32_t wakes[SVC_POWER_WAKE_COUNT];
    uint32_t raise_unused_count;
    int8_t seg_pct; // drain segment start %, -1 = none
    bool seg_anchored; // first step after unplug/boot only anchors (gauge position unknown)
    uint32_t seg_start_ms;
    uint64_t seg_state_ms[POWER_STATE_COUNT];
    uint64_t seg_ls_us;
    uint32_t last_drain_ma_x10;
} s;

static portMUX_TYPE s_stats_lock = portMUX_INITIALIZER_UNLOCKED;
static int64_t s_alarm_wake;     // UTC s for the WATCH-ONLY timer, 0 = none (s_stats_lock)
static volatile bool s_alarm_boot; // booted by that timer, no user wake since

// Written just before every WATCH-ONLY deep sleep; survives it (not a power loss:
// the magic tells).
typedef struct {
    uint32_t magic;
    bool ticks;        // minute ticks (panel on); false = dark, PWR and alarm only
    int64_t alarm_utc; // next alarm, 0 = none
} watch_only_rtc_t;
static RTC_NOINIT_ATTR watch_only_rtc_t s_rtc_wo;
static volatile uint32_t s_ls_count; // light sleep (IRAM callback)
static volatile uint64_t s_ls_us;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// Battery history clock (does not wrap like now_ms()).
static uint32_t uptime_s(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

static esp_err_t post(msg_type_t type, uint8_t a, uint8_t b)
{
    ESP_RETURN_ON_FALSE(s.queue, ESP_ERR_INVALID_STATE, TAG, "not started");
    const msg_t m = {.type = (uint8_t)type, .a = a, .b = b};
    return xQueueSend(s.queue, &m, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

// --- Inputs (driver and event contexts: queue only) -------------------------------

static void on_touch_down(void *ctx)
{
    (void)ctx;
    post(MSG_TOUCH, 0, 0);
}

static void on_pmu(hal_pmu_event_t evt, void *ctx)
{
    (void)ctx;
    post(MSG_PMU, (uint8_t)evt, 0);
}

static void on_setting(void *ctx, esp_event_base_t base, int32_t id, const void *data, size_t len)
{
    (void)ctx;
    (void)base;
    if (id == SVC_SETTINGS_EVT_CHANGED && data && len >= sizeof(svc_settings_evt_changed_t)) {
        switch (((const svc_settings_evt_changed_t *)data)->id) {
        case S3W_SETTING_DISPLAY_BRIGHTNESS:
        case S3W_SETTING_SCREEN_TIMEOUT_S:
        case S3W_SETTING_AOD:
        case S3W_SETTING_WAKE_ON_TAP:
        case S3W_SETTING_RAISE_TO_WAKE:
        case S3W_SETTING_BATTERY_SAVER:
            break;
        default:
            return;
        }
    } else if (id != SVC_SETTINGS_EVT_RESET) {
        return;
    }
    post(MSG_SETTINGS, 0, 0);
}

static uint8_t modes_bits(const modes_state_t *m)
{
    return (m->quiet ? MODES_QUIET : 0) | (m->dark ? MODES_DARK : 0) | (m->theater ? MODES_THEATER : 0);
}

static void on_modes(void *ctx, esp_event_base_t base, int32_t id, const void *data, size_t len)
{
    (void)ctx;
    (void)base;
    (void)id;
    if (len >= sizeof(modes_state_t)) {
        post(MSG_MODES, modes_bits(data), 0);
    }
}

#if CONFIG_PM_LIGHT_SLEEP_CALLBACKS
// Idle task, inside the PM critical section: counters only.
static IRAM_ATTR esp_err_t on_light_sleep_exit(int64_t slept_us, void *arg)
{
    (void)arg;
    if (slept_us > 0) {
        s_ls_count++;
        s_ls_us += (uint64_t)slept_us;
    }
    return ESP_OK;
}
#endif

// --- Outputs ---------------------------------------------------------------------

static uint8_t pct_to_level(int pct)
{
    return (uint8_t)((pct * 255 + 50) / 100);
}

static uint8_t level_for(power_state_t st)
{
    switch (st) {
    case POWER_STATE_ACTIVE:
        return s.brightness;
    case POWER_STATE_DIM: {
        const int dim = s.brightness * DIM_PCT / 100;
        return (uint8_t)(dim > 0 ? dim : 1);
    }
    case POWER_STATE_AOD:
        return pct_to_level(AOD_PCT);
    default:
        return 0;
    }
}

static void post_event(int32_t id, const void *data, size_t len)
{
    if (s3w_event_post(SVC_POWER_EVENT, id, data, len) != ESP_OK) {
        ESP_LOGW(TAG, "event %d dropped", (int)id);
    }
}

static void post_state(power_state_t prev)
{
    const svc_power_evt_state_t evt = {
        .state = (uint8_t)s.applied,
        .prev = (uint8_t)prev,
        .saver = s.fsm.policy.saver,
    };
    post_event(SVC_POWER_EVT_STATE, &evt, sizeof evt);
}

static void ui_trampoline(void *ctx)
{
    s.ui_hook((svc_power_ui_mode_t)(uintptr_t)ctx);
    xTaskNotifyGive(s.task);
}

// Run the UI hook on the UI task and wait for it: the panel must not change state
// while a frame is being sent, and must not light up before the new frame.
static void ui_mode(svc_power_ui_mode_t mode)
{
    if (!s.ui_hook) {
        return;
    }
    ulTaskNotifyTake(pdTRUE, 0); // a late reply from an earlier timeout
    if (s3w_ui_post(ui_trampoline, (void *)(uintptr_t)mode) != ESP_OK) {
        ESP_LOGW(TAG, "UI mailbox full");
        return;
    }
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(UI_HOOK_TIMEOUT_MS)) == 0) {
        ESP_LOGW(TAG, "UI hook (mode %d) timed out", (int)mode);
    }
}

static void locks(bool take)
{
    if (take == s.locks_held) {
        return;
    }
    s.locks_held = take;
    if (take) {
        esp_pm_lock_acquire(s.lock_cpu);
        esp_pm_lock_acquire(s.lock_awake);
    } else {
        esp_pm_lock_release(s.lock_awake);
        esp_pm_lock_release(s.lock_cpu);
    }
}

static int64_t utc_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

// WATCH-ONLY: deep sleep until the next minute tick (panel kept on) or, dark, with the
// panel off until PWR; both also wake for the next alarm. Does not return on success.
static void watch_only_deep_sleep(bool dark)
{
    portENTER_CRITICAL(&s_stats_lock);
    const int64_t alarm = s_alarm_wake;
    portEXIT_CRITICAL(&s_stats_lock);
    const uint64_t timer_us = watch_only_sleep_us(utc_ms(), alarm, !dark);
    s_rtc_wo = (watch_only_rtc_t){.magic = WATCH_ONLY_MAGIC, .ticks = !dark, .alarm_utc = alarm};
    ESP_LOGI(TAG, "watch-only: deep sleep %llu ms%s%s", (unsigned long long)(timer_us / 1000),
             dark ? ", panel off (battery empty)" : "", alarm ? ", alarm set" : "");
    hal_power_deep_sleep(timer_us, !dark);
    s_rtc_wo.magic = 0;
}

static uint32_t wake_sources(void)
{
    return HAL_WAKE_BUTTONS | HAL_WAKE_RTC | (s.wake_on_tap ? HAL_WAKE_TOUCH : 0) |
           (s.wake_on_raise ? HAL_WAKE_MOTION : 0);
}

static void panel_on_dark(void)
{
    hal_display_set_brightness(0); // the panel still holds the last frame
    if (hal_display_set_power(true) != ESP_OK) {
        ESP_LOGE(TAG, "panel on failed");
    }
}

static void apply_state(power_state_t from, power_state_t to)
{
    const bool on_from = power_state_screen_on(from);
    const bool on_to = power_state_screen_on(to);
    const bool panel_from = power_state_panel_on(from);

    if (on_to) {
        if (!on_from) {
            locks(true);
            hal_power_disarm_wake();
            if (!panel_from) {
                panel_on_dark();
            }
            ui_mode(SVC_POWER_UI_ON);
        }
        hal_display_set_brightness(level_for(to));
        if (!on_from) {
            ESP_LOGI(TAG, "screen on in %lld ms (%s)", (esp_timer_get_time() - s.wake_t0_us) / 1000,
                     svc_power_wake_name(s.wake_reason));
            // After the frame: leaving monitor mode without a touch is a ~150 ms reset.
            if (hal_touch_set_low_power(false) != ESP_OK) {
                ESP_LOGW(TAG, "touch did not leave low-power mode");
            }
        }
        return;
    }

    if (on_from) {
        hal_touch_set_low_power(true);
        if (s.raise_unused) {
            // Raised, never touched: most likely a false wake (P3-05 walk test).
            s.raise_unused = false;
            portENTER_CRITICAL(&s_stats_lock);
            s.raise_unused_count++;
            portEXIT_CRITICAL(&s_stats_lock);
        }
    }
    // WATCH-ONLY shows the time-only screen at AOD brightness and leaves the panel on;
    // with an empty battery (dark) it goes off like the other states.
    const bool watch_face = to == POWER_STATE_WATCH_ONLY &&
                            !(s.battery_valid && s.battery.percent >= 0 && s.battery.percent <= WATCH_ONLY_DARK_PCT);
    if (to == POWER_STATE_AOD || watch_face) {
        if (!panel_from) {
            panel_on_dark();
        }
        ui_mode(watch_face ? SVC_POWER_UI_WATCH_ONLY : SVC_POWER_UI_AOD);
        hal_display_set_brightness(level_for(POWER_STATE_AOD));
    } else if (panel_from) {
        ui_mode(SVC_POWER_UI_OFF);
        hal_display_set_power(false);
    }

    if (to == POWER_STATE_WATCH_ONLY) {
        ESP_LOGW(TAG, "watch-only: time only, PWR to exit");
        watch_only_deep_sleep(!watch_face);
        ESP_LOGE(TAG, "deep sleep failed");
    } else if (to == POWER_STATE_OFF) {
        ESP_LOGW(TAG, "power off");
        hal_pmu_power_off();
        ESP_LOGE(TAG, "power off failed");
    }
    hal_power_arm_wake(wake_sources());
    locks(false);
}

static uint64_t state_ms_now(int i, uint32_t now)
{
    return s.state_ms[i] + ((power_state_t)i == s.applied ? now - s.state_since_ms : 0);
}

static void apply(uint32_t now)
{
    const power_state_t to = s.fsm.state;
    const power_state_t from = s.applied;
    if (to == from) {
        return;
    }
    portENTER_CRITICAL(&s_stats_lock);
    s.state_ms[from] += now - s.state_since_ms;
    s.state_since_ms = now;
    s.applied = to;
    portEXIT_CRITICAL(&s_stats_lock);
    ESP_LOGI(TAG, "%s -> %s", power_state_name(from), power_state_name(to));
    apply_state(from, to);
    post_state(from);
}

static void wake(svc_power_wake_t reason, uint32_t now);

// --- Battery -----------------------------------------------------------------------

// Average current over each 1 % step of the fuel gauge while on battery. Coarse
// (1 % = 4 mAh) but needs no hardware; an idle-only step is the idle current.
static void drain_update(const svc_power_battery_t *b, uint32_t now)
{
    if (b->vbus || b->percent < 0) {
        s.seg_pct = -1;
        return;
    }
    if (s.seg_pct >= 0 && b->percent < s.seg_pct) {
        const uint32_t ms = now - s.seg_start_ms;
        const int drop = s.seg_pct - b->percent;
        if (s.seg_anchored && ms > 0) {
            const uint64_t ma_x10 = (uint64_t)drop * BATTERY_MAH * 10 * 3600000ull / ms;
            uint32_t share[POWER_STATE_COUNT];
            for (int i = 0; i < POWER_STATE_COUNT; i++) {
                share[i] = (uint32_t)((state_ms_now(i, now) - s.seg_state_ms[i]) * 100 / ms);
            }
            const uint32_t ls_pct = (uint32_t)((s_ls_us - s.seg_ls_us) / 10 / ms);
            s.last_drain_ma_x10 = (uint32_t)ma_x10;
            ESP_LOGI(TAG,
                     "drain %d%% -> %d%% in %lu s: ~%lu.%lu mA (on %lu%%, dim %lu%%, AOD %lu%%, off %lu%%, light "
                     "sleep %lu%%)",
                     s.seg_pct, b->percent, (unsigned long)(ms / 1000), (unsigned long)(ma_x10 / 10),
                     (unsigned long)(ma_x10 % 10), (unsigned long)share[POWER_STATE_ACTIVE],
                     (unsigned long)share[POWER_STATE_DIM], (unsigned long)share[POWER_STATE_AOD],
                     (unsigned long)(share[POWER_STATE_SLEEP] + share[POWER_STATE_SAVER]), (unsigned long)ls_pct);
        }
        s.seg_anchored = true;
    } else if (s.seg_pct >= 0 && b->percent == s.seg_pct) {
        return;
    } else {
        s.seg_anchored = false; // unplugged, boot, or the gauge went up
    }
    s.seg_pct = b->percent;
    s.seg_start_ms = now;
    for (int i = 0; i < POWER_STATE_COUNT; i++) {
        s.seg_state_ms[i] = state_ms_now(i, now);
    }
    s.seg_ls_us = s_ls_us;
}

static void read_battery(uint32_t now)
{
    s.battery_next_ms = now + BATTERY_POLL_MS;
    hal_battery_t hb;
    if (hal_pmu_read_battery(&hb) != ESP_OK) {
        return;
    }
    const svc_power_battery_t b = {.percent = hb.percent, .mv = hb.mv, .charging = hb.charging, .vbus = hb.vbus};
    const bool changed = !s.battery_valid || b.percent != s.battery.percent || b.charging != s.battery.charging ||
                         b.vbus != s.battery.vbus;
    drain_update(&b, now);
    portENTER_CRITICAL(&s_stats_lock);
    s.battery = b;
    s.battery_valid = true;
    battery_hist_add(&s.hist, uptime_s(), b.percent, b.charging);
    portEXIT_CRITICAL(&s_stats_lock);
    if (changed) {
        ESP_LOGI(TAG, "battery %d%% %u mV%s%s", b.percent, b.mv, b.charging ? ", charging" : "",
                 b.vbus ? ", USB" : "");
        post_event(SVC_POWER_EVT_BATTERY, &b, sizeof b);
    }

    if (b.vbus || b.percent < 0) {
        s.low_reported = 100;
    } else {
        for (size_t i = 0; i < sizeof k_low_thresholds; i++) {
            const uint8_t t = k_low_thresholds[i];
            if (b.percent <= t) {
                if (t < s.low_reported) {
                    s.low_reported = t;
                    const svc_power_evt_battery_low_t evt = {.percent = b.percent, .threshold = t};
                    ESP_LOGW(TAG, "battery low: %d%% (<= %u%%)", b.percent, t);
                    post_event(SVC_POWER_EVT_BATTERY_LOW, &evt, sizeof evt);
                    if (t <= LOW_WAKE_PCT) {
                        wake(SVC_POWER_WAKE_BATTERY, now); // its alert needs to be seen
                    }
                }
                break;
            }
        }
    }
    power_fsm_battery(&s.fsm, b.percent, b.vbus);
}

// --- Settings ----------------------------------------------------------------------

// Sleep and theater modes (dark) keep the screen dark: no AOD, no tap or raise wake.
// Battery saver: no AOD (power_fsm), no raise (svc_sensors), brightness and timeout capped.
static power_policy_t load_settings(void)
{
    const bool dark = s.modes & MODES_DARK;
    const bool saver = svc_settings_get_bool(S3W_SETTING_BATTERY_SAVER);
    int bright = svc_settings_get_int(S3W_SETTING_DISPLAY_BRIGHTNESS);
    int timeout_s = svc_settings_get_int(S3W_SETTING_SCREEN_TIMEOUT_S);
    if (saver) {
        bright = bright < SAVER_BRIGHTNESS_PCT ? bright : SAVER_BRIGHTNESS_PCT;
        timeout_s = timeout_s < SAVER_TIMEOUT_S ? timeout_s : SAVER_TIMEOUT_S;
    }
    s.brightness = pct_to_level(bright);
    s.wake_on_tap = svc_settings_get_bool(S3W_SETTING_WAKE_ON_TAP) && !dark;
    s.wake_on_raise = svc_settings_get_bool(S3W_SETTING_RAISE_TO_WAKE) && !dark;
    return (power_policy_t){
        .timeout_ms = (uint32_t)timeout_s * 1000u,
        .aod = svc_settings_get_bool(S3W_SETTING_AOD) && !dark,
        .saver = saver,
    };
}

// --- Task --------------------------------------------------------------------------

// Modes (svc_modes): quiet = no notification or low-battery wake; dark = no tap or
// raise wake. Buttons, alarms, the charger and the console always wake.
static bool wake_blocked(svc_power_wake_t reason)
{
    switch (reason) {
    case SVC_POWER_WAKE_NOTIFY:
    case SVC_POWER_WAKE_BATTERY:
        return s.modes & MODES_QUIET;
    case SVC_POWER_WAKE_TOUCH:
    case SVC_POWER_WAKE_RAISE:
        return s.modes & MODES_DARK;
    default:
        return false;
    }
}

static void wake(svc_power_wake_t reason, uint32_t now)
{
    if (!power_state_screen_on(s.fsm.state) && wake_blocked(reason)) {
        ESP_LOGD(TAG, "%s wake ignored (modes 0x%x)", svc_power_wake_name(reason), s.modes);
        return;
    }
    if (!power_state_screen_on(s.fsm.state) && s.fsm.state != POWER_STATE_WATCH_ONLY &&
        s.fsm.state != POWER_STATE_OFF) {
        s.wake_t0_us = esp_timer_get_time();
        s.wake_reason = reason;
        if (reason != SVC_POWER_WAKE_ALARM) {
            s_alarm_boot = false; // the user took over: stay on after the alarm
        }
        s.wakes[reason]++;
        s.raise_unused = reason == SVC_POWER_WAKE_RAISE;
    } else {
        s.raise_unused = false;
    }
    power_fsm_activity(&s.fsm, now);
}

static void handle(const msg_t *m, uint32_t now)
{
    switch ((msg_type_t)m->type) {
    case MSG_WAKE:
        wake(m->a < SVC_POWER_WAKE_COUNT ? (svc_power_wake_t)m->a : SVC_POWER_WAKE_CONSOLE, now);
        break;
    case MSG_ACTIVITY:
        if (power_state_screen_on(s.fsm.state)) {
            s.raise_unused = false;
            power_fsm_activity(&s.fsm, now);
        }
        break;
    case MSG_TOUCH:
        if (power_state_screen_on(s.fsm.state)) {
            s.raise_unused = false;
            power_fsm_activity(&s.fsm, now);
        } else if (s.wake_on_tap) {
            wake(SVC_POWER_WAKE_TOUCH, now);
        }
        break;
    case MSG_SCREEN_OFF:
        power_fsm_screen_off(&s.fsm, now);
        break;
    case MSG_HOLD:
        power_fsm_hold(&s.fsm, m->a, now);
        break;
    case MSG_PMU:
        // PWR short/long come from the SYS_OUT button GPIO; the rest changes the battery.
        if (m->a == HAL_PMU_EVT_PKEY_SHORT || m->a == HAL_PMU_EVT_PKEY_LONG) {
            break;
        }
        read_battery(now);
        if (m->a == HAL_PMU_EVT_VBUS_IN) {
            wake(SVC_POWER_WAKE_CHARGER, now);
            if (s.fsm.policy.saver) {
                svc_settings_set_bool(S3W_SETTING_BATTERY_SAVER, false); // charger ends saver (docs/02 §7)
            }
        }
        break;
    case MSG_MODES:
        if ((m->a & MODES_THEATER) && !(s.modes & MODES_THEATER)) {
            power_fsm_screen_off(&s.fsm, now); // theater mode: screen off at once
        }
        s.modes = m->a;
        __attribute__((fallthrough)); // AOD and wake inputs follow the dark flag
    case MSG_SETTINGS: {
        const bool saver_was = s.fsm.policy.saver;
        const power_policy_t p = load_settings();
        power_fsm_set_policy(&s.fsm, &p);
        if (power_state_screen_on(s.applied)) {
            hal_display_set_brightness(level_for(s.applied));
        } else if (s.applied == s.fsm.state && s.applied != POWER_STATE_WATCH_ONLY && s.applied != POWER_STATE_OFF) {
            hal_power_arm_wake(wake_sources()); // wake on tap / raise changed while off
        }
        if (p.saver != saver_was) {
            ESP_LOGI(TAG, "battery saver %s", p.saver ? "on" : "off");
            if (s.fsm.state == s.applied) {
                post_state(s.applied); // same state, new saver flag
            }
        }
        break;
    }
    case MSG_WATCH_ONLY:
        power_fsm_enter(&s.fsm, POWER_STATE_WATCH_ONLY);
        break;
    case MSG_SHUTDOWN:
        power_fsm_enter(&s.fsm, POWER_STATE_OFF);
        break;
    case MSG_RESTART:
        ESP_LOGW(TAG, "restart");
        if (power_state_panel_on(s.applied)) {
            ui_mode(SVC_POWER_UI_OFF);
            hal_display_set_power(false);
        }
        esp_restart();
        break;
    }
}

static void tick(uint32_t now)
{
    if ((int32_t)(now - s.battery_next_ms) >= 0) {
        read_battery(now);
    }
    if (power_fsm_ms_to_next(&s.fsm, now) == 0) {
        uint16_t x;
        uint16_t y;
        if (hal_touch_read(&x, &y)) {
            power_fsm_activity(&s.fsm, now); // a finger still down (scrolling) is activity
        } else {
            power_fsm_tick(&s.fsm, now);
        }
    }
}

static uint32_t next_wait_ms(uint32_t now)
{
    uint32_t wait = power_fsm_ms_to_next(&s.fsm, now);
    const int32_t to_battery = (int32_t)(s.battery_next_ms - now);
    if (to_battery < (int32_t)wait) {
        wait = to_battery > 0 ? (uint32_t)to_battery : 0;
    }
    return wait;
}

static void power_task(void *arg)
{
    (void)arg;
    for (;;) {
        const uint32_t wait = next_wait_ms(now_ms());
        msg_t m;
        if (xQueueReceive(s.queue, &m, wait == POWER_FSM_NO_DEADLINE ? portMAX_DELAY : pdMS_TO_TICKS(wait))) {
            handle(&m, now_ms());
        }
        const uint32_t now = now_ms();
        tick(now);
        apply(now);
    }
}

// --- API ---------------------------------------------------------------------------

void svc_power_set_ui_hook(svc_power_ui_hook_t hook)
{
    s.ui_hook = hook;
}

esp_err_t svc_power_start(void)
{
    ESP_RETURN_ON_FALSE(!s.queue, ESP_ERR_INVALID_STATE, TAG, "already started");
    ESP_RETURN_ON_ERROR(esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "screen_cpu", &s.lock_cpu), TAG, "lock");
    ESP_RETURN_ON_ERROR(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "screen_on", &s.lock_awake), TAG, "lock");
    ESP_RETURN_ON_ERROR(hal_power_init(), TAG, "hal_power");

    const esp_pm_config_t pm = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = PM_MIN_MHZ,
        .light_sleep_enable = true,
    };
    ESP_RETURN_ON_ERROR(esp_pm_configure(&pm), TAG, "pm");
#if CONFIG_PM_LIGHT_SLEEP_CALLBACKS
    esp_pm_sleep_cbs_register_config_t cbs = {.exit_cb = on_light_sleep_exit};
    ESP_RETURN_ON_ERROR(esp_pm_light_sleep_register_cbs(&cbs), TAG, "sleep cbs");
#endif

    // Woken from WATCH-ONLY for the alarm: keep the screen off until it rings.
    s_alarm_boot = svc_power_boot_kind() == SVC_POWER_BOOT_ALARM;
    s_rtc_wo.magic = 0;

    modes_state_t modes;
    svc_modes_get(&modes);
    s.modes = modes_bits(&modes);
    const uint32_t now = now_ms();
    const power_policy_t policy = load_settings();
    power_fsm_init(&s.fsm, &policy, now);
    s.applied = POWER_STATE_ACTIVE;
    s.state_since_ms = now;
    s.battery_next_ms = now; // first read on the first loop
    s.low_reported = 100;
    s.seg_pct = -1;
    battery_hist_init(&s.hist);
    locks(true);
    hal_display_set_brightness(level_for(POWER_STATE_ACTIVE));

    s.queue = xQueueCreate(QUEUE_LEN, sizeof(msg_t));
    ESP_RETURN_ON_FALSE(s.queue, ESP_ERR_NO_MEM, TAG, "queue");
    const s3w_task_cfg_t task = {
        .name = "svc_power",
        .fn = power_task,
        .stack_bytes = S3W_STACK_POWER,
        .prio = S3W_PRIO_POWER,
        .core = S3W_CORE_SERVICES,
    };
    ESP_RETURN_ON_ERROR(s3w_task_create(&task, &s.task), TAG, "task");

    hal_touch_set_down_cb(on_touch_down, NULL);
    ESP_RETURN_ON_ERROR(hal_pmu_set_event_cb(on_pmu, NULL), TAG, "pmu");
    ESP_RETURN_ON_ERROR(s3w_event_subscribe(SVC_SETTINGS_EVENT, ESP_EVENT_ANY_ID, on_setting, NULL, NULL), TAG,
                        "settings");
    ESP_RETURN_ON_ERROR(s3w_event_subscribe(SVC_MODES_EVENT, SVC_MODES_EVT_CHANGED, on_modes, NULL, NULL), TAG,
                        "modes");

    if (s_alarm_boot) {
        post(MSG_SCREEN_OFF, 0, 0);
    }
    ESP_LOGI(TAG, "started: timeout %lu s, AOD %d, saver %d, DFS %d-%d MHz + auto light sleep%s",
             (unsigned long)(policy.timeout_ms / 1000), policy.aod, policy.saver, PM_MIN_MHZ, pm.max_freq_mhz,
             s_alarm_boot ? " (alarm wake from watch-only: screen off)" : "");
    return ESP_OK;
}

esp_err_t svc_power_wake(svc_power_wake_t reason)
{
    return post(MSG_WAKE, (uint8_t)reason, 0);
}

esp_err_t svc_power_user_activity(void)
{
    return post(MSG_ACTIVITY, 0, 0);
}

esp_err_t svc_power_screen_off(void)
{
    return post(MSG_SCREEN_OFF, 0, 0);
}

esp_err_t svc_power_hold_screen(bool hold)
{
    return post(MSG_HOLD, hold, 0);
}

esp_err_t svc_power_set_saver(bool on)
{
    return svc_settings_set_bool(S3W_SETTING_BATTERY_SAVER, on); // svc_power follows the change event
}

void svc_power_set_alarm_wake(int64_t utc)
{
    portENTER_CRITICAL(&s_stats_lock);
    s_alarm_wake = utc > 0 ? utc : 0;
    portEXIT_CRITICAL(&s_stats_lock);
}

bool svc_power_alarm_boot(void)
{
    return s_alarm_boot;
}

svc_power_boot_t svc_power_boot_kind(void)
{
    static bool s_known;
    static svc_power_boot_t s_kind;
    if (s_known) {
        return s_kind;
    }
    s_known = true;
    s_kind = SVC_POWER_BOOT_NORMAL;
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER && s_rtc_wo.magic == WATCH_ONLY_MAGIC) {
        s_alarm_wake = s_rtc_wo.alarm_utc;
        const bool alarm = !s_rtc_wo.ticks || watch_only_wake_kind(utc_ms() / 1000, s_alarm_wake) == WATCH_ONLY_WAKE_ALARM;
        s_kind = alarm ? SVC_POWER_BOOT_ALARM : SVC_POWER_BOOT_WATCH_TICK;
    }
    return s_kind;
}

void svc_power_watch_only_sleep(bool dark)
{
    if (dark) {
        hal_display_set_power(false);
    }
    watch_only_deep_sleep(dark);
    ESP_LOGE(TAG, "deep sleep failed");
}

void svc_power_watch_only_exit(void)
{
    s_rtc_wo.magic = 0;
}

esp_err_t svc_power_enter_watch_only(void)
{
    return post(MSG_WATCH_ONLY, 0, 0);
}

esp_err_t svc_power_shutdown(void)
{
    return post(MSG_SHUTDOWN, 0, 0);
}

esp_err_t svc_power_restart(void)
{
    return post(MSG_RESTART, 0, 0);
}

power_state_t svc_power_state(void)
{
    return s.applied;
}

bool svc_power_saver(void)
{
    return svc_settings_get_bool(S3W_SETTING_BATTERY_SAVER);
}

esp_err_t svc_power_battery(svc_power_battery_t *out)
{
    portENTER_CRITICAL(&s_stats_lock);
    const bool valid = s.battery_valid;
    *out = s.battery;
    portEXIT_CRITICAL(&s_stats_lock);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t svc_power_battery_info(svc_power_battery_info_t *out)
{
    const uint32_t now_s = uptime_s();
    portENTER_CRITICAL(&s_stats_lock);
    const bool valid = s.battery_valid;
    out->battery = s.battery;
    out->minutes = battery_hist_minutes(&s.hist, now_s, s.battery.percent, s.battery.charging);
    battery_hist_get(&s.hist, now_s, out->history);
    portEXIT_CRITICAL(&s_stats_lock);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

void svc_power_get_stats(svc_power_stats_t *out)
{
    const uint32_t now = now_ms();
    memset(out, 0, sizeof *out);
    portENTER_CRITICAL(&s_stats_lock);
    out->state = s.applied;
    out->saver = s.fsm.policy.saver;
    out->holds = s.fsm.holds;
    out->timeout_ms = s.fsm.policy.timeout_ms;
    for (int i = 0; i < POWER_STATE_COUNT; i++) {
        out->state_ms[i] = state_ms_now(i, now);
    }
    memcpy(out->wakes, s.wakes, sizeof out->wakes);
    out->raise_unused = s.raise_unused_count;
    out->seg_start_pct = s.seg_pct;
    out->seg_ms = s.seg_pct >= 0 ? now - s.seg_start_ms : 0;
    out->last_drain_ma_x10 = s.last_drain_ma_x10;
    out->light_sleeps = s_ls_count;
    out->light_sleep_us = s_ls_us;
    portEXIT_CRITICAL(&s_stats_lock);
    out->stack_free = s.task ? uxTaskGetStackHighWaterMark(s.task) : 0;
}

const char *svc_power_wake_name(svc_power_wake_t reason)
{
    static const char *const k_names[] = {
        [SVC_POWER_WAKE_TOUCH] = "touch",   [SVC_POWER_WAKE_BUTTON] = "button", [SVC_POWER_WAKE_CHARGER] = "charger",
        [SVC_POWER_WAKE_ALARM] = "alarm",   [SVC_POWER_WAKE_NOTIFY] = "notify", [SVC_POWER_WAKE_RAISE] = "raise",
        [SVC_POWER_WAKE_CONSOLE] = "console", [SVC_POWER_WAKE_BATTERY] = "battery",
    };
    return (unsigned)reason < SVC_POWER_WAKE_COUNT ? k_names[reason] : "?";
}
