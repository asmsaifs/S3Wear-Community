// Sensor service: raise to wake, flip detection (svc_sensors.h, docs/03 F2).
//
// Wake-ups of the svc_sensors task: requests on its queue (IMU interrupt, power
// state, settings; event driven) and, only while a raise window is open (after a
// wake-on-motion interrupt, at most RAISE_MAX_MS), one accelerometer read every
// SAMPLE_MS. The gesture needs ~50 Hz to see the wrist settle quickly. While an
// alarm rings (flip watch), one read every FLIP_SAMPLE_MS: a flip takes ~1 s.
// While a batch client is set (svc_activity, step counting), one FIFO drain every
// FIFO_DRAIN_FRAMES sample periods (~4.6 s at 21 Hz): the FIFO holds 128 frames and steps
// need every sample, with no FIFO interrupt to spare (INT1 is wake-on-motion's).
#include "svc_sensors.h"

#include <math.h>
#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "hal.h"
#include "flip_detect.h"
#include "raise_detect.h"
#include "s3w_event.h"
#include "s3w_task.h"
#include "svc_modes.h"
#include "svc_power.h"
#include "svc_settings.h"

static const char *TAG = "svc_sensors";

ESP_EVENT_DEFINE_BASE(SVC_SENSORS_EVENT);

#define QUEUE_LEN     8
#define SAMPLE_MS     20  // raise window only (see above)
#define FLIP_SAMPLE_MS 100 // flip watch only (see above)
#define WOM_MG        80  // wake-on-motion threshold: a raise moves gravity by several hundred mg
// First sample after the accelerometer is switched on. The QMI8658's first ~3 samples are
// railed at full scale (measured: valid from 45-50 ms at 62.5 Hz), so 5 periods.
#define ACCEL_SETTLE_MS 80
#define REQUEST_TIMEOUT_MS 1000
#define FIFO_DRAIN_FRAMES 96 // drain with 32 of the 128 frames to spare (see above)

typedef enum {
    MSG_IRQ,
    MSG_POWER, // a = power_state_t
    MSG_SETTINGS,
    MSG_SUSPEND, // a = suspend
    MSG_READ_ACCEL,
    MSG_FLIP, // a = on
    MSG_STREAM, // the stream fields changed
    MSG_BATCH,  // the batch client changed
} msg_type_t;

typedef struct {
    uint8_t type;
    uint8_t a;
} msg_t;

static struct {
    QueueHandle_t queue;
    TaskHandle_t task;
    SemaphoreHandle_t req_lock; // one waiting request at a time
    SemaphoreHandle_t req_done;
    esp_err_t req_err;
    svc_sensors_accel_t req_accel;

    svc_sensors_mode_t mode;
    power_state_t power;
    bool present;
    bool raise_on;
    bool suspended;
    bool trace;
    uint8_t flip_watch; // svc_sensors_watch_flip() count
    // svc_sensors_stream(): written by the caller under s_stats_lock, then MSG_STREAM
    uint16_t stream_hz;
    svc_sensors_sample_cb_t stream_cb;
    void *stream_ctx;
    // svc_sensors_set_batch_cb(): written by the caller under s_stats_lock, then MSG_BATCH
    svc_sensors_batch_cb_t batch_cb;
    void *batch_ctx;
    bool fifo_on;
    bool fifo_restart;     // next batch starts after a gap
    uint8_t fifo_skip;     // first frames after a start (railed, ACCEL_SETTLE_MS)
    uint32_t fifo_period_us;
    uint32_t next_drain_ms;
    hal_accel_t *fifo_raw; // PSRAM, HAL_IMU_FIFO_FRAMES each
    svc_sensors_accel_t *fifo_out;
    raise_detect_t raise;
    flip_detect_t flip;
    uint32_t window_start_ms;
    uint32_t next_sample_ms;
    svc_sensors_stats_t st;
} s;

static portMUX_TYPE s_stats_lock = portMUX_INITIALIZER_UNLOCKED;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static esp_err_t post(msg_type_t type, uint8_t a)
{
    ESP_RETURN_ON_FALSE(s.queue, ESP_ERR_INVALID_STATE, TAG, "not started");
    const msg_t m = {.type = (uint8_t)type, .a = a};
    return xQueueSend(s.queue, &m, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

// Post and wait for the task to finish the request.
static esp_err_t request(msg_type_t type, uint8_t a)
{
    ESP_RETURN_ON_FALSE(s.queue, ESP_ERR_INVALID_STATE, TAG, "not started");
    xSemaphoreTake(s.req_lock, portMAX_DELAY);
    xSemaphoreTake(s.req_done, 0); // a late reply from an earlier timeout
    esp_err_t err = post(type, a);
    if (err == ESP_OK) {
        err = xSemaphoreTake(s.req_done, pdMS_TO_TICKS(REQUEST_TIMEOUT_MS)) == pdTRUE ? s.req_err : ESP_ERR_TIMEOUT;
    }
    xSemaphoreGive(s.req_lock);
    return err;
}

static void reply(esp_err_t err)
{
    s.req_err = err;
    xSemaphoreGive(s.req_done);
}

// --- Inputs (driver and event contexts: queue only) -------------------------------

static void on_imu_int(void *ctx)
{
    (void)ctx;
    post(MSG_IRQ, 0);
}

static void on_power(void *ctx, esp_event_base_t base, int32_t id, const void *data, size_t len)
{
    (void)ctx;
    (void)base;
    (void)id;
    if (len >= sizeof(svc_power_evt_state_t)) {
        post(MSG_POWER, ((const svc_power_evt_state_t *)data)->state);
    }
}

static void on_setting(void *ctx, esp_event_base_t base, int32_t id, const void *data, size_t len)
{
    (void)ctx;
    (void)base;
    if (id == SVC_SETTINGS_EVT_CHANGED && data && len >= sizeof(svc_settings_evt_changed_t)) {
        const s3w_setting_t sid = ((const svc_settings_evt_changed_t *)data)->id;
        if (sid != S3W_SETTING_RAISE_TO_WAKE && sid != S3W_SETTING_RAISE_SENSITIVITY) {
            return;
        }
    } else if (id != SVC_SETTINGS_EVT_RESET) {
        return;
    }
    post(MSG_SETTINGS, 0);
}

// Sleep and theater modes (dark) turn raise to wake off.
static void on_modes(void *ctx, esp_event_base_t base, int32_t id, const void *data, size_t len)
{
    (void)ctx;
    (void)base;
    (void)id;
    (void)data;
    (void)len;
    post(MSG_SETTINGS, 0);
}

// --- Modes ---------------------------------------------------------------------------

static void count_error(esp_err_t err)
{
    if (err != ESP_OK) {
        portENTER_CRITICAL(&s_stats_lock);
        s.st.errors++;
        portEXIT_CRITICAL(&s_stats_lock);
    }
}

static void set_mode(svc_sensors_mode_t mode)
{
    portENTER_CRITICAL(&s_stats_lock);
    s.mode = mode;
    portEXIT_CRITICAL(&s_stats_lock);
}

static bool batch_client(void)
{
    portENTER_CRITICAL(&s_stats_lock);
    const bool on = s.batch_cb != NULL;
    portEXIT_CRITICAL(&s_stats_lock);
    return on;
}

// --- FIFO (batches) ------------------------------------------------------------------

// Hand whatever the FIFO holds to the batch client.
static void fifo_drain(uint32_t now)
{
    if (!s.fifo_on) {
        return;
    }
    s.next_drain_ms = now + FIFO_DRAIN_FRAMES * (s.fifo_period_us / 1000);
    size_t n = 0;
    bool overflow = false;
    const esp_err_t err = hal_imu_fifo_read(s.fifo_raw, HAL_IMU_FIFO_FRAMES, &n, &overflow);
    count_error(err);
    if (err != ESP_OK || n == 0) {
        return;
    }
    size_t first = 0;
    if (s.fifo_skip) {
        first = s.fifo_skip < n ? s.fifo_skip : n;
        s.fifo_skip -= (uint8_t)first;
    }
    portENTER_CRITICAL(&s_stats_lock);
    const svc_sensors_batch_cb_t cb = s.batch_cb;
    void *ctx = s.batch_ctx;
    s.st.fifo_reads++;
    s.st.fifo_samples += n - first;
    s.st.fifo_overflows += overflow;
    portEXIT_CRITICAL(&s_stats_lock);
    if (!cb || first == n) {
        return;
    }
    for (size_t i = first; i < n; i++) {
        s.fifo_out[i - first] = (svc_sensors_accel_t){.x = s.fifo_raw[i].x, .y = s.fifo_raw[i].y, .z = s.fifo_raw[i].z};
    }
    const svc_sensors_batch_t b = {
        .s = s.fifo_out,
        .n = (uint16_t)(n - first),
        .restart = s.fifo_restart || overflow,
        .period_us = s.fifo_period_us,
        .end_ms = now,
    };
    s.fifo_restart = false;
    cb(ctx, &b);
}

// Drain, then forget the FIFO: the IMU is about to be reconfigured.
static void fifo_stop(void)
{
    fifo_drain(now_ms());
    s.fifo_on = false;
}

// After the accelerometer mode is set: keep its samples for the batch client.
static void fifo_start(uint32_t now)
{
    if (!batch_client() || !s.fifo_raw) {
        return;
    }
    uint32_t period_us = 0;
    const esp_err_t err = hal_imu_fifo_start(&period_us);
    count_error(err);
    if (err != ESP_OK || period_us < 1000) {
        return;
    }
    s.fifo_on = true;
    s.fifo_restart = true;
    s.fifo_period_us = period_us;
    s.fifo_skip = (uint8_t)((ACCEL_SETTLE_MS * 1000 + period_us - 1) / period_us);
    s.next_drain_ms = now + FIFO_DRAIN_FRAMES * (period_us / 1000);
    portENTER_CRITICAL(&s_stats_lock);
    s.st.fifo_restarts++;
    portEXIT_CRITICAL(&s_stats_lock);
}

// What the mode should be now (a window stays open until it ends).
static svc_sensors_mode_t wanted(void)
{
    if (s.suspended) {
        return SVC_SENSORS_MODE_SUSPENDED;
    }
    if (s.present && s.flip_watch) {
        return SVC_SENSORS_MODE_FLIP;
    }
    const bool on_screen = s.power == POWER_STATE_ACTIVE || s.power == POWER_STATE_DIM;
    if (s.present && s.stream_hz && on_screen) {
        return SVC_SENSORS_MODE_STREAM;
    }
    // SAVER: fewer wake sources (docs/02 §7). Sleep and theater modes: raise_on is off.
    const bool off_screen = s.power == POWER_STATE_AOD || s.power == POWER_STATE_SLEEP;
    if (s.present && s.raise_on && off_screen) {
        return SVC_SENSORS_MODE_WAIT;
    }
    return s.present && batch_client() ? SVC_SENSORS_MODE_STEPS : SVC_SENSORS_MODE_OFF;
}

// Put the IMU in the state a mode needs, from any state (a window, diagnostics).
static void configure(svc_sensors_mode_t mode)
{
    if (!s.present) {
        return;
    }
    fifo_stop();
    count_error(hal_imu_off());
    if (mode == SVC_SENSORS_MODE_WAIT) {
        count_error(hal_imu_motion_wake(WOM_MG));
    } else if (mode == SVC_SENSORS_MODE_FLIP) {
        count_error(hal_imu_accel_start());
        flip_init(&s.flip);
        s.next_sample_ms = now_ms() + ACCEL_SETTLE_MS;
    } else if (mode == SVC_SENSORS_MODE_STREAM) {
        count_error(hal_imu_accel_start());
        s.next_sample_ms = now_ms() + ACCEL_SETTLE_MS;
    } else if (mode == SVC_SENSORS_MODE_STEPS) {
        count_error(hal_imu_accel_lp_start());
    }
    if (mode != SVC_SENSORS_MODE_OFF && mode != SVC_SENSORS_MODE_SUSPENDED) {
        fifo_start(now_ms());
    }
}

// The pose when raise detection starts (the screen went off): a raise that follows is armed
// if the screen pointed away. Once per screen-off; the accelerometer is on for at most
// ACCEL_SETTLE_MS (blocks this task only).
static void take_pose(void)
{
    const bool on = s.mode == SVC_SENSORS_MODE_STREAM || s.mode == SVC_SENSORS_MODE_FLIP ||
                    s.mode == SVC_SENSORS_MODE_STEPS;
    if (!on) {
        fifo_stop(); // configure() follows and restarts it
        const esp_err_t err = hal_imu_accel_start();
        count_error(err);
        if (err != ESP_OK) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(ACCEL_SETTLE_MS));
    }
    hal_accel_t a;
    const esp_err_t err = hal_imu_read_accel(&a);
    count_error(err);
    if (err == ESP_OK) {
        raise_pose(&s.raise, a.x, a.y, a.z);
    }
}

static void apply(void)
{
    const svc_sensors_mode_t want = wanted();
    if (want == s.mode || (s.mode == SVC_SENSORS_MODE_WINDOW && want == SVC_SENSORS_MODE_WAIT)) {
        return; // a window runs to its end
    }
    raise_cancel(&s.raise);
    if (want == SVC_SENSORS_MODE_WAIT && s.present) {
        take_pose();
    }
    configure(want);
    // Suspended: diagnostics own the interrupt callback. Otherwise it is ours.
    hal_imu_set_int_cb(want == SVC_SENSORS_MODE_SUSPENDED || !s.present ? NULL : on_imu_int, NULL);
    ESP_LOGD(TAG, "%s -> %s", svc_sensors_mode_name(s.mode), svc_sensors_mode_name(want));
    set_mode(want);
}

static void load_settings(void)
{
    modes_state_t modes;
    svc_modes_get(&modes);
    s.raise_on = svc_settings_get_bool(S3W_SETTING_RAISE_TO_WAKE) && !modes.dark;
    const uint8_t cone = raise_cone_deg(svc_settings_get_int(S3W_SETTING_RAISE_SENSITIVITY));
    const bool had_pose = s.raise.cos_cone != 0; // initialised before: keep the last pose
    const float pose = s.raise.last_cos;
    raise_init(&s.raise, cone); // an open window ends at its next sample
    if (had_pose) {
        s.raise.last_cos = pose;
    }
    portENTER_CRITICAL(&s_stats_lock);
    s.st.raise_enabled = s.raise_on;
    s.st.cone_deg = cone;
    portEXIT_CRITICAL(&s_stats_lock);
}

static int deg_of(float c)
{
    c = c > 1.0f ? 1.0f : c < -1.0f ? -1.0f : c;
    return (int)lroundf(acosf(c) * 57.29578f);
}

static void window_open(uint32_t now)
{
    fifo_stop();
    const esp_err_t err = hal_imu_accel_start();
    count_error(err);
    fifo_start(now);
    if (err != ESP_OK) {
        return; // stay in WAIT; the next motion tries again
    }
    raise_motion(&s.raise, now);
    s.window_start_ms = now;
    s.next_sample_ms = now + ACCEL_SETTLE_MS;
    portENTER_CRITICAL(&s_stats_lock);
    s.st.windows++;
    portEXIT_CRITICAL(&s_stats_lock);
    set_mode(SVC_SENSORS_MODE_WINDOW);
}

// Counters and trace for a window that ended.
static uint32_t window_done(bool raised, uint32_t now)
{
    const uint32_t ms = now - s.window_start_ms;
    const int min_deg = deg_of(s.raise.min_cos);
    const int end_deg = deg_of(s.raise.last_cos);
    portENTER_CRITICAL(&s_stats_lock);
    s.st.samples += s.raise.samples;
    s.st.last_min_deg = (int16_t)min_deg;
    s.st.last_end_deg = (int16_t)end_deg;
    s.st.last_ms = (uint16_t)(ms > UINT16_MAX ? UINT16_MAX : ms);
    s.st.last_raise = raised;
    if (raised) {
        s.st.raises++;
    }
    portEXIT_CRITICAL(&s_stats_lock);
    if (s.trace) {
        ESP_LOGI(TAG, "window %lu ms, %u samples: away %d deg, end %d deg (cone %d) -> %s", (unsigned long)ms,
                 s.raise.samples, min_deg, end_deg, s.st.cone_deg, raised ? "RAISE" : "no");
    }
    return ms;
}

static void window_close(bool raised, uint32_t now)
{
    const uint32_t ms = window_done(raised, now);
    if (raised) {
        svc_power_wake(SVC_POWER_WAKE_RAISE);
        const svc_sensors_evt_raise_t evt = {.window_ms = (uint16_t)(ms > UINT16_MAX ? UINT16_MAX : ms)};
        if (s3w_event_post(SVC_SENSORS_EVENT, SVC_SENSORS_EVT_RAISE, &evt, sizeof evt) != ESP_OK) {
            ESP_LOGW(TAG, "event dropped");
        }
    }
    // Back to waiting for motion (off once svc_power reports the screen on). A
    // suspend or a screen-on would have ended the window in apply() already.
    const svc_sensors_mode_t want = wanted();
    configure(want);
    set_mode(want);
}

static void flip_step(uint32_t now)
{
    s.next_sample_ms = now + FLIP_SAMPLE_MS;
    hal_accel_t a;
    const esp_err_t err = hal_imu_read_accel(&a);
    count_error(err);
    if (err != ESP_OK || !flip_sample(&s.flip, a.x, a.y, a.z, now)) {
        return;
    }
    portENTER_CRITICAL(&s_stats_lock);
    s.st.flips++;
    portEXIT_CRITICAL(&s_stats_lock);
    if (s3w_event_post(SVC_SENSORS_EVENT, SVC_SENSORS_EVT_FLIP, NULL, 0) != ESP_OK) {
        ESP_LOGW(TAG, "event dropped");
    }
}

// App stream: one sample every 1000 / rate ms (the app asked for the rate; at most the
// sensor's own 62.5 Hz).
static void stream_step(uint32_t now)
{
    portENTER_CRITICAL(&s_stats_lock);
    const uint16_t hz = s.stream_hz;
    const svc_sensors_sample_cb_t cb = s.stream_cb;
    void *ctx = s.stream_ctx;
    portEXIT_CRITICAL(&s_stats_lock);
    const uint32_t period = hz ? 1000u / hz : 1000u;
    s.next_sample_ms = now + (period < 16 ? 16 : period);
    hal_accel_t a;
    const esp_err_t err = hal_imu_read_accel(&a);
    count_error(err);
    if (err != ESP_OK || !cb) {
        return;
    }
    const svc_sensors_accel_t out = {.x = a.x, .y = a.y, .z = a.z};
    cb(ctx, &out);
    portENTER_CRITICAL(&s_stats_lock);
    s.st.streamed++;
    portEXIT_CRITICAL(&s_stats_lock);
}

static void window_sample(uint32_t now)
{
    s.next_sample_ms = now + SAMPLE_MS;
    hal_accel_t a;
    const esp_err_t err = hal_imu_read_accel(&a);
    count_error(err);
    if (err != ESP_OK) {
        window_close(false, now);
        return;
    }
    const raise_result_t r = raise_sample(&s.raise, a.x, a.y, a.z, now);
    if (r == RAISE_END && s.fifo_on && now - s.raise.last_motion_ms < RAISE_QUIET_MS) {
        // Ended at RAISE_MAX_MS with the wrist still moving (walking): the next window opens
        // at once with the accelerometer and its FIFO left running. Going back to
        // wake-on-motion would lose the steps until it fires again: its FIFO stays empty.
        window_done(false, now);
        raise_motion(&s.raise, now);
        s.window_start_ms = now;
        portENTER_CRITICAL(&s_stats_lock);
        s.st.windows++;
        portEXIT_CRITICAL(&s_stats_lock);
    } else if (r != RAISE_CONTINUE) {
        window_close(r == RAISE_WAKE, now);
    }
}

// --- Task ----------------------------------------------------------------------------

static esp_err_t read_accel_now(svc_sensors_accel_t *out)
{
    ESP_RETURN_ON_FALSE(s.present, ESP_ERR_NOT_FOUND, TAG, "no IMU");
    ESP_RETURN_ON_FALSE(!s.suspended, ESP_ERR_INVALID_STATE, TAG, "suspended");
    const bool streaming = s.mode == SVC_SENSORS_MODE_WINDOW || s.mode == SVC_SENSORS_MODE_STREAM;
    if (!streaming) {
        fifo_stop(); // configure() below restarts it
        ESP_RETURN_ON_ERROR(hal_imu_accel_start(), TAG, "accel on");
        vTaskDelay(pdMS_TO_TICKS(ACCEL_SETTLE_MS)); // console request only
    }
    hal_accel_t a;
    const esp_err_t err = hal_imu_read_accel(&a);
    if (!streaming) {
        configure(s.mode); // back to off or wake-on-motion
    }
    ESP_RETURN_ON_ERROR(err, TAG, "read");
    *out = (svc_sensors_accel_t){.x = a.x, .y = a.y, .z = a.z};
    return ESP_OK;
}

static void handle(const msg_t *m, uint32_t now)
{
    switch ((msg_type_t)m->type) {
    case MSG_IRQ: {
        if (s.mode != SVC_SENSORS_MODE_WAIT && s.mode != SVC_SENSORS_MODE_WINDOW) {
            break;
        }
        bool motion = false;
        const esp_err_t err = hal_imu_read_irq(&motion);
        count_error(err);
        if (err != ESP_OK || !motion) {
            break; // e.g. the INT1 level change when wake-on-motion is switched on
        }
        portENTER_CRITICAL(&s_stats_lock);
        s.st.motion_irqs++;
        portEXIT_CRITICAL(&s_stats_lock);
        if (s.mode == SVC_SENSORS_MODE_WAIT) {
            window_open(now);
        }
        break;
    }
    case MSG_POWER:
        s.power = (power_state_t)m->a; // screen on during a window: apply() ends it
        apply();
        break;
    case MSG_SETTINGS:
        load_settings();
        apply();
        break;
    case MSG_SUSPEND:
        s.suspended = m->a; // resuming reconfigures the IMU from whatever diagnostics left
        apply();
        reply(ESP_OK);
        break;
    case MSG_READ_ACCEL:
        reply(read_accel_now(&s.req_accel));
        break;
    case MSG_FLIP:
        s.flip_watch = m->a ? s.flip_watch + 1 : s.flip_watch ? s.flip_watch - 1 : 0;
        apply();
        break;
    case MSG_STREAM:
        apply();
        break;
    case MSG_BATCH:
        if (s.mode == SVC_SENSORS_MODE_WINDOW) {
            break; // the window's end configures the FIFO
        }
        if (wanted() == s.mode) {
            configure(s.mode); // same mode, FIFO on or off
        } else {
            apply();
        }
        break;
    }
}

static void sensors_task(void *arg)
{
    (void)arg;
    for (;;) {
        TickType_t wait = portMAX_DELAY;
        const uint32_t before = now_ms();
        if (s.mode == SVC_SENSORS_MODE_WINDOW || s.mode == SVC_SENSORS_MODE_FLIP ||
            s.mode == SVC_SENSORS_MODE_STREAM) {
            const int32_t ms = (int32_t)(s.next_sample_ms - before);
            wait = ms > 0 ? pdMS_TO_TICKS(ms) : 0;
        }
        if (s.fifo_on) {
            const int32_t ms = (int32_t)(s.next_drain_ms - before);
            const TickType_t w = ms > 0 ? pdMS_TO_TICKS(ms) : 0;
            wait = w < wait ? w : wait;
        }
        msg_t m;
        if (xQueueReceive(s.queue, &m, wait)) {
            handle(&m, now_ms());
        }
        const uint32_t now = now_ms();
        if (s.mode == SVC_SENSORS_MODE_WINDOW && (int32_t)(now - s.next_sample_ms) >= 0) {
            window_sample(now);
        } else if (s.mode == SVC_SENSORS_MODE_FLIP && (int32_t)(now - s.next_sample_ms) >= 0) {
            flip_step(now);
        } else if (s.mode == SVC_SENSORS_MODE_STREAM && (int32_t)(now - s.next_sample_ms) >= 0) {
            stream_step(now);
        }
        if (s.fifo_on && (int32_t)(now_ms() - s.next_drain_ms) >= 0) {
            fifo_drain(now_ms());
        }
    }
}

// --- API ---------------------------------------------------------------------------

esp_err_t svc_sensors_start(void)
{
    ESP_RETURN_ON_FALSE(!s.queue, ESP_ERR_INVALID_STATE, TAG, "already started");
    s.present = hal_imu_present();
    s.power = svc_power_state();
    s.mode = SVC_SENSORS_MODE_OFF;
    load_settings();
    s.st.present = s.present;

    s.fifo_raw = heap_caps_malloc(HAL_IMU_FIFO_FRAMES * sizeof *s.fifo_raw, MALLOC_CAP_SPIRAM);
    s.fifo_out = heap_caps_malloc(HAL_IMU_FIFO_FRAMES * sizeof *s.fifo_out, MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(s.fifo_raw && s.fifo_out, ESP_ERR_NO_MEM, TAG, "fifo buffers");
    s.req_lock = xSemaphoreCreateMutex();
    s.req_done = xSemaphoreCreateBinary();
    s.queue = xQueueCreate(QUEUE_LEN, sizeof(msg_t));
    ESP_RETURN_ON_FALSE(s.req_lock && s.req_done && s.queue, ESP_ERR_NO_MEM, TAG, "queue");
    const s3w_task_cfg_t task = {
        .name = "svc_sensors",
        .fn = sensors_task,
        .stack_bytes = S3W_STACK_SENSORS,
        .prio = S3W_PRIO_SENSORS,
        .core = S3W_CORE_SERVICES,
        .stack_psram = true, // no flash writes, no ISR work
    };
    ESP_RETURN_ON_ERROR(s3w_task_create(&task, &s.task), TAG, "task");
    ESP_RETURN_ON_ERROR(s3w_event_subscribe(SVC_POWER_EVENT, SVC_POWER_EVT_STATE, on_power, NULL, NULL), TAG,
                        "power");
    ESP_RETURN_ON_ERROR(s3w_event_subscribe(SVC_SETTINGS_EVENT, ESP_EVENT_ANY_ID, on_setting, NULL, NULL), TAG,
                        "settings");
    ESP_RETURN_ON_ERROR(s3w_event_subscribe(SVC_MODES_EVENT, SVC_MODES_EVT_CHANGED, on_modes, NULL, NULL), TAG,
                        "modes");
    if (s.present) {
        post(MSG_SETTINGS, 0); // first apply() on the task
    }
    ESP_LOGI(TAG, "started: IMU %s, raise to wake %d (cone %u deg)", s.present ? "found" : "missing", s.raise_on,
             s.st.cone_deg);
    return ESP_OK;
}

esp_err_t svc_sensors_suspend(bool suspend)
{
    return request(MSG_SUSPEND, suspend);
}

esp_err_t svc_sensors_watch_flip(bool on)
{
    return post(MSG_FLIP, on);
}

esp_err_t svc_sensors_stream(uint16_t rate_hz, svc_sensors_sample_cb_t cb, void *ctx)
{
    ESP_RETURN_ON_FALSE(rate_hz == 0 || cb, ESP_ERR_INVALID_ARG, TAG, "cb");
    portENTER_CRITICAL(&s_stats_lock);
    s.stream_hz = rate_hz > 62 ? 62 : rate_hz;
    s.stream_cb = rate_hz ? cb : NULL;
    s.stream_ctx = rate_hz ? ctx : NULL;
    portEXIT_CRITICAL(&s_stats_lock);
    return post(MSG_STREAM, 0);
}

esp_err_t svc_sensors_set_batch_cb(svc_sensors_batch_cb_t cb, void *ctx)
{
    portENTER_CRITICAL(&s_stats_lock);
    s.batch_cb = cb;
    s.batch_ctx = cb ? ctx : NULL;
    portEXIT_CRITICAL(&s_stats_lock);
    return post(MSG_BATCH, 0);
}

void svc_sensors_set_trace(bool on)
{
    s.trace = on;
}

esp_err_t svc_sensors_read_accel(svc_sensors_accel_t *out)
{
    const esp_err_t err = request(MSG_READ_ACCEL, 0);
    if (err == ESP_OK) {
        *out = s.req_accel;
    }
    return err;
}

void svc_sensors_get_stats(svc_sensors_stats_t *out)
{
    portENTER_CRITICAL(&s_stats_lock);
    *out = s.st;
    out->mode = s.mode;
    out->fifo_period_us = s.fifo_on ? s.fifo_period_us : 0;
    portEXIT_CRITICAL(&s_stats_lock);
    out->stack_free = s.task ? uxTaskGetStackHighWaterMark(s.task) : 0;
}

const char *svc_sensors_mode_name(svc_sensors_mode_t mode)
{
    static const char *const k_names[] = {
        [SVC_SENSORS_MODE_OFF] = "off",
        [SVC_SENSORS_MODE_WAIT] = "waiting for motion",
        [SVC_SENSORS_MODE_WINDOW] = "raise window",
        [SVC_SENSORS_MODE_SUSPENDED] = "suspended (diagnostics)",
        [SVC_SENSORS_MODE_FLIP] = "flip watch",
        [SVC_SENSORS_MODE_STREAM] = "app stream",
        [SVC_SENSORS_MODE_STEPS] = "steps only",
    };
    return (unsigned)mode <= SVC_SENSORS_MODE_STEPS ? k_names[mode] : "?";
}
