#include "s3w_lvgl_port.h"

#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "s3w_task.h"
#include "s3w_ui.h"
#include "sdkconfig.h"
#include "src/core/lv_global.h"
#include "src/display/lv_display_private.h"

static const char *TAG = "lvgl_port";

#define TE_TIMEOUT_MS       20  // > one 60 Hz panel frame
// Upper bound on UI task sleep. lv_timer_handler() returns the time to the next
// LVGL timer and s3w_ui_post() wakes the task early; the 500 ms cap only catches
// LVGL timers created from other tasks under lv_lock(). With the screen off or in
// AOD (s3w_lvgl_port_set_low_power) there is no cap: the task sleeps until the
// next LVGL timer (AOD: the minute tick) or a post.
#define UI_TASK_MAX_SLEEP_MS 500

#if CONFIG_S3W_LVGL_RENDER_DIRECT
#define RENDER_MODE_NAME "direct (PSRAM frame buffer)"
#else
#define RENDER_MODE_NAME "partial"
#endif

typedef struct {
    s3w_lvgl_port_cfg_t cfg;
    lv_display_t *disp;
    bool frame_open;
    bool thread_check; // armed once init is done
    bool output_off;   // panel asleep: drop frames (s3w_lvgl_port_set_output)
    volatile bool low_power;
    // Partial mode: both draw buffers in one internal DMA block (buf, buf + buf_bytes). Single
    // (s3w_lvgl_port_set_single_buffer) shrinks the block in place to the first; two again grows it
    // back in place when the space after it is still free (else moves it, else retries at screen-on).
    uint8_t *buf;
    size_t buf_bytes;
    bool single;      // one draw buffer now
    bool want_double; // a regrow failed: retry

#if CONFIG_PM_ENABLE
    esp_pm_lock_handle_t frame_lock; // no light sleep while a frame is sent
#endif
#if CONFIG_S3W_LVGL_RENDER_DIRECT
    uint16_t *bounce[2]; // internal DMA, cfg.buf_lines lines each
    uint8_t bounce_idx;
#endif
    // Screenshot (s3w_lvgl_port_capture): the UI task copies every flushed area into cap_dst.
    uint16_t *cap_dst;
    SemaphoreHandle_t cap_done;
    esp_err_t cap_result;
    s3w_lvgl_port_stats_t stats;
} port_t;

static port_t s_port;
static StaticSemaphore_t s_cap_sem_buf;
static StaticSemaphore_t s_cap_lock_buf;
static SemaphoreHandle_t s_cap_lock; // one capture at a time (console and worker task)

// --- Debug: LVGL must only be used with lv_lock() held (the UI task holds it) ----

#if CONFIG_S3W_LVGL_THREAD_CHECK
static void check_lvgl_thread(const char *what)
{
    if (!s_port.thread_check) {
        return;
    }
    TaskHandle_t holder = xSemaphoreGetMutexHolder(LV_GLOBAL_DEFAULT()->lv_general_mutex.xMutex);
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    if (holder == self || strcmp(pcTaskGetName(self), "swdraw") == 0) { // LVGL's own render thread
        return;
    }
    ESP_LOGE(TAG, "LVGL %s from task '%s' without lv_lock() (use s3w_ui_post or lv_lock)", what,
             pcTaskGetName(self));
    abort();
}
#else
static inline void check_lvgl_thread(const char *what)
{
    (void)what;
}
#endif

// --- LVGL heap in PSRAM (LV_USE_CUSTOM_MALLOC) ------------------------------
// Kept in this file so the linker pulls it in together with s3w_lvgl_port_init().

#define LV_HEAP_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

void lv_mem_init(void)
{
}

void lv_mem_deinit(void)
{
}

lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes)
{
    LV_UNUSED(mem);
    LV_UNUSED(bytes);
    return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool)
{
    LV_UNUSED(pool);
}

void *lv_malloc_core(size_t size)
{
    check_lvgl_thread("malloc");
    return heap_caps_malloc(size, LV_HEAP_CAPS);
}

void *lv_realloc_core(void *p, size_t new_size)
{
    check_lvgl_thread("realloc");
    return heap_caps_realloc(p, new_size, LV_HEAP_CAPS);
}

void lv_free_core(void *p)
{
    check_lvgl_thread("free");
    heap_caps_free(p);
}

void lv_mem_monitor_core(lv_mem_monitor_t *mon)
{
    multi_heap_info_t info;
    heap_caps_get_info(&info, LV_HEAP_CAPS);
    memset(mon, 0, sizeof *mon);
    mon->total_size = info.total_free_bytes + info.total_allocated_bytes;
    mon->free_size = info.total_free_bytes;
    mon->free_biggest_size = info.largest_free_block;
    mon->max_used = mon->total_size - info.minimum_free_bytes;
    mon->used_cnt = info.allocated_blocks;
    mon->free_cnt = info.free_blocks;
    mon->used_pct = mon->total_size ? 100 - (100 * mon->free_size) / mon->total_size : 0;
    mon->frag_pct = info.total_free_bytes
                        ? 100 - (100 * info.largest_free_block) / info.total_free_bytes
                        : 0;
}

lv_result_t lv_mem_test_core(void)
{
    return heap_caps_check_integrity(LV_HEAP_CAPS, true) ? LV_RESULT_OK : LV_RESULT_INVALID;
}

// --- Display ------------------------------------------------------------------

static uint32_t tick_cb(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// CO5300: partial updates must start on an even pixel and end on an odd one.
static void rounder_cb(lv_event_t *e)
{
    check_lvgl_thread("invalidate"); // every visible change to an object lands here
    lv_area_t *a = lv_event_get_param(e);
    a->x1 &= ~1;
    a->y1 &= ~1;
    a->x2 |= 1;
    a->y2 |= 1;
}

#if CONFIG_S3W_LVGL_RENDER_PARTIAL
static bool on_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *ctx)
{
    (void)io;
    (void)edata;
    lv_display_flush_ready((lv_display_t *)ctx);
    return false;
}

// LVGL rendered `area` into px (one band of an internal DMA buffer). Swap in place
// and hand the buffer to DMA; on_trans_done releases it to LVGL.
static void flush_area(port_t *p, const lv_area_t *area, uint8_t *px)
{
    int64_t t = esp_timer_get_time();
    lv_draw_sw_rgb565_swap(px, lv_area_get_size(area)); // panel wants big-endian RGB565
    const int64_t t_swap = esp_timer_get_time();
    p->stats.swap_us += t_swap - t;
    const esp_err_t err =
        esp_lcd_panel_draw_bitmap(p->cfg.panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px);
    p->stats.bus_wait_us += esp_timer_get_time() - t_swap;
    if (err != ESP_OK) {
        lv_display_flush_ready(p->disp);
    }
}
#else
// LVGL rendered `area` in place into the PSRAM frame buffer fb. Copy it row by row,
// byte-swapped, into the two internal bounce buffers in turn and send each. Every
// draw_bitmap first sends CASET/RASET, and esp_lcd finishes all queued pixel DMA
// before a parameter write, so the other bounce buffer is always free to refill
// while this one is on the bus. The frame buffer is not read after we return.
static void flush_area(port_t *p, const lv_area_t *area, uint8_t *fb)
{
    const int32_t w = lv_area_get_width(area);
    const int32_t rows_max = (int32_t)p->cfg.buf_lines * p->cfg.hres / w;
    const uint16_t *src = (const uint16_t *)fb + (size_t)area->y1 * p->cfg.hres + area->x1;
    for (int32_t y = area->y1; y <= area->y2;) {
        const int32_t rows = LV_MIN(rows_max, area->y2 - y + 1);
        uint16_t *dst = p->bounce[p->bounce_idx];
        p->bounce_idx ^= 1;

        int64_t t = esp_timer_get_time();
        for (int32_t r = 0; r < rows; r++, src += p->cfg.hres) {
            for (int32_t x = 0; x < w; x++) {
                dst[r * w + x] = __builtin_bswap16(src[x]);
            }
        }
        const int64_t t_copy = esp_timer_get_time();
        p->stats.swap_us += t_copy - t;
        const esp_err_t err = esp_lcd_panel_draw_bitmap(p->cfg.panel, area->x1, y, area->x2 + 1, y + rows, dst);
        p->stats.bus_wait_us += esp_timer_get_time() - t_copy;
        if (err != ESP_OK) {
            break;
        }
        y += rows;
    }
    lv_display_flush_ready(p->disp);
}
#endif

// UI task, before the pixels are swapped for the panel: keep a copy for the screenshot.
static void capture_area(port_t *p, const lv_area_t *area, const uint8_t *px)
{
    const int32_t w = lv_area_get_width(area);
    const int32_t h = lv_area_get_height(area);
#if CONFIG_S3W_LVGL_RENDER_DIRECT
    const uint16_t *src = (const uint16_t *)px + (size_t)area->y1 * p->cfg.hres + area->x1;
    const size_t stride = p->cfg.hres;
#else
    const uint16_t *src = (const uint16_t *)px;
    const size_t stride = (size_t)w;
#endif
    for (int32_t r = 0; r < h; r++) {
        memcpy(p->cap_dst + (size_t)(area->y1 + r) * p->cfg.hres + area->x1, src + r * stride, (size_t)w * 2);
    }
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    port_t *p = lv_display_get_driver_data(disp);
    if (p->cap_dst) {
        capture_area(p, area, px);
        if (lv_display_flush_is_last(disp)) {
            p->cap_dst = NULL; // complete: the caller may read it once it is given the semaphore
            xSemaphoreGive(p->cap_done);
        }
    }
    if (p->output_off) {
        lv_display_flush_ready(disp);
        return;
    }

    // Start each frame on V-blank so the scan-out and our write move in step.
    if (!p->frame_open) {
        p->frame_open = true;
        p->stats.frames++;
#if CONFIG_PM_ENABLE
        esp_pm_lock_acquire(p->frame_lock);
#endif
        if (p->cfg.te_wait) {
            const int64_t t = esp_timer_get_time();
            p->cfg.te_wait(TE_TIMEOUT_MS);
            p->stats.te_wait_us += esp_timer_get_time() - t;
        }
    }
    const bool last = lv_display_flush_is_last(disp);
    p->stats.flushes++;
    p->stats.px += lv_area_get_size(area);
    flush_area(p, area, px);
    if (last) {
        p->frame_open = false;
#if CONFIG_PM_ENABLE
        esp_pm_lock_release(p->frame_lock); // queued DMA holds the SPI driver's own lock
#endif
    }
}

static void ui_task(void *arg)
{
    (void)arg;
    s3w_ui_bind_task(xTaskGetCurrentTaskHandle());
    for (;;) {
        lv_lock();
        s3w_ui_drain();                        // mailbox work runs before the next frame
        uint32_t wait_ms = lv_timer_handler(); // re-takes the (recursive) lock itself
        lv_unlock();
        TickType_t wait;
        if (s_port.low_power) {
            wait = wait_ms == LV_NO_TIMER_READY ? portMAX_DELAY : pdMS_TO_TICKS(wait_ms);
        } else {
            wait = pdMS_TO_TICKS(wait_ms == LV_NO_TIMER_READY || wait_ms > UI_TASK_MAX_SLEEP_MS ? UI_TASK_MAX_SLEEP_MS
                                                                                                : wait_ms);
        }
        ulTaskNotifyTakeIndexed(S3W_UI_NOTIFY_INDEX, pdTRUE, wait ? wait : 1);
    }
}

esp_err_t s3w_lvgl_port_init(const s3w_lvgl_port_cfg_t *cfg)
{
    ESP_RETURN_ON_FALSE(cfg && cfg->panel && cfg->io && cfg->buf_lines, ESP_ERR_INVALID_ARG, TAG, "cfg");
    ESP_RETURN_ON_FALSE(!s_port.disp, ESP_ERR_INVALID_STATE, TAG, "already init");
    s_port.cfg = *cfg;
    s_port.cap_done = xSemaphoreCreateBinaryStatic(&s_cap_sem_buf);
    s_cap_lock = xSemaphoreCreateMutexStatic(&s_cap_lock_buf);

    lv_init();
    // LV_USE_TJPGD is for svc_media's album art, which calls TJpgDec directly. lv_init() also
    // registers LVGL's JPEG image decoder, whose info callback keeps a 4 KB work buffer on the
    // stack and runs first for every image header lookup: on the 5 KB swdraw stack that
    // overflowed (crash in canvas apps). No image here is a JPEG file, so drop the decoder.
    lv_tjpgd_deinit();
    lv_tick_set_cb(tick_cb);

    const size_t buf_bytes = S3W_LVGL_PORT_BUF_BYTES(cfg->hres, cfg->buf_lines);
    uint8_t *bufs = heap_caps_malloc(2 * buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(bufs, ESP_ERR_NO_MEM, TAG, "draw buffers (2 x %u B internal DMA)", (unsigned)buf_bytes);
    void *buf1 = bufs;
    void *buf2 = bufs + buf_bytes;

    lv_display_t *disp = lv_display_create(cfg->hres, cfg->vres);
    ESP_RETURN_ON_FALSE(disp, ESP_ERR_NO_MEM, TAG, "display");
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
#if CONFIG_S3W_LVGL_RENDER_PARTIAL
    lv_display_set_buffers(disp, buf1, buf2, buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    s_port.buf = bufs;
    s_port.buf_bytes = buf_bytes;
    const esp_lcd_panel_io_callbacks_t cbs = {.on_color_trans_done = on_trans_done};
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_register_event_callbacks(cfg->io, &cbs, disp), TAG, "io cbs");
#else
    // One frame buffer is enough: the flush copies out synchronously, so LVGL may
    // render the next frame into it as soon as flush_cb returns.
    const size_t fb_bytes = S3W_LVGL_PORT_BUF_BYTES(cfg->hres, cfg->vres);
    void *fb = heap_caps_aligned_alloc(64, fb_bytes, MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(fb, ESP_ERR_NO_MEM, TAG, "frame buffer (%u B PSRAM)", (unsigned)fb_bytes);
    lv_display_set_buffers(disp, fb, NULL, fb_bytes, LV_DISPLAY_RENDER_MODE_DIRECT);
    s_port.bounce[0] = buf1;
    s_port.bounce[1] = buf2;
#endif
#if CONFIG_PM_ENABLE
    ESP_RETURN_ON_ERROR(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "lvgl_frame", &s_port.frame_lock), TAG, "pm");
#endif
    lv_display_set_driver_data(disp, &s_port);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_add_event_cb(disp, rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);

    s_port.disp = disp;

    const s3w_task_cfg_t task = {
        .name = "ui",
        .fn = ui_task,
        .stack_bytes = S3W_STACK_UI,
        .prio = S3W_PRIO_UI,
        .core = S3W_CORE_UI,
    };
    ESP_RETURN_ON_ERROR(s3w_task_create(&task, NULL), TAG, "ui task");
    s_port.thread_check = true;

    ESP_LOGI(TAG, "LVGL %d.%d.%d, %ux%u, %s, 2 x %u B internal DMA", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR,
             LVGL_VERSION_PATCH, cfg->hres, cfg->vres,
             RENDER_MODE_NAME, (unsigned)buf_bytes);
    return ESP_OK;
}

lv_display_t *s3w_lvgl_port_display(void)
{
    return s_port.disp;
}

#if CONFIG_S3W_LVGL_RENDER_PARTIAL
// lv_lock() held, no flush in flight. Grow the block back to two buffers.
static esp_err_t regrow(void)
{
    uint8_t *b = heap_caps_realloc(s_port.buf, 2 * s_port.buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!b) {
        s_port.want_double = true;
        return ESP_ERR_NO_MEM;
    }
    s_port.buf = b;
    s_port.single = false;
    s_port.want_double = false;
    lv_display_set_buffers(s_port.disp, b, b + s_port.buf_bytes, s_port.buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    return ESP_OK;
}

static void wait_flush_done(void)
{
    // The last band of the previous refresh may still be on the bus (on_trans_done clears it).
    while (s_port.disp->flushing) {
        vTaskDelay(1);
    }
}
#endif

void s3w_lvgl_port_set_output(bool on)
{
    if (s_port.output_off == !on) {
        return;
    }
    s_port.output_off = !on;
#if CONFIG_S3W_LVGL_RENDER_PARTIAL
    if (on && s_port.disp && s_port.want_double) {
        wait_flush_done();
        if (regrow() == ESP_OK) {
            ESP_LOGI(TAG, "two draw buffers again");
        }
    }
#endif
    if (on && s_port.disp) {
        // Everything drawn while off was dropped: repaint the whole panel.
        lv_obj_invalidate(lv_display_get_screen_active(s_port.disp));
    }
}

bool s3w_lvgl_port_output_is_on(void)
{
    return !s_port.output_off;
}

// UI task: start the capture with a full repaint (screen and the top and system layers).
static void capture_arm(void *arg)
{
    uint16_t *dst = arg;
    if (s_port.output_off) { // frames are dropped while the panel sleeps
        s_port.cap_result = ESP_ERR_INVALID_STATE;
        xSemaphoreGive(s_port.cap_done);
        return;
    }
    s_port.cap_dst = dst;
    lv_obj_invalidate(lv_display_get_screen_active(s_port.disp));
}

esp_err_t s3w_lvgl_port_capture(uint16_t *dst, uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(s_port.disp && dst, ESP_ERR_INVALID_STATE, TAG, "not init");
    ESP_RETURN_ON_FALSE(!s3w_ui_is_ui_task(), ESP_ERR_INVALID_STATE, TAG, "would deadlock");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(s_cap_lock, 0) == pdTRUE, ESP_ERR_INVALID_STATE, TAG, "busy");
    s_port.cap_result = ESP_OK;
    xSemaphoreTake(s_port.cap_done, 0);
    esp_err_t err = s3w_ui_post(capture_arm, dst);
    if (err == ESP_OK && xSemaphoreTake(s_port.cap_done, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        lv_lock(); // give up: the UI task must not write into dst after we return
        s_port.cap_dst = NULL;
        lv_unlock();
        err = xSemaphoreTake(s_port.cap_done, 0) == pdTRUE ? s_port.cap_result : ESP_ERR_TIMEOUT;
    } else if (err == ESP_OK) {
        err = s_port.cap_result;
    }
    xSemaphoreGive(s_cap_lock);
    return err;
}

void s3w_lvgl_port_set_low_power(bool low_power)
{
    s_port.low_power = low_power;
}

esp_err_t s3w_lvgl_port_set_single_buffer(bool single)
{
#if CONFIG_S3W_LVGL_RENDER_PARTIAL
    ESP_RETURN_ON_FALSE(s_port.disp, ESP_ERR_INVALID_STATE, TAG, "not init");
    esp_err_t err = ESP_OK;
    lv_lock(); // no rendering while the buffers change
    wait_flush_done();
    if (single && !s_port.single) {
        s_port.want_double = false;
        lv_display_set_buffers(s_port.disp, s_port.buf, NULL, s_port.buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
        // Shrinking keeps the block where it is and frees its second half.
        uint8_t *b = heap_caps_realloc(s_port.buf, s_port.buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (b && b != s_port.buf) {
            lv_display_set_buffers(s_port.disp, b, NULL, s_port.buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
        }
        s_port.buf = b ? b : s_port.buf;
        s_port.single = true;
        ESP_LOGI(TAG, "one draw buffer (%u B internal freed)", (unsigned)s_port.buf_bytes);
    } else if (!single && s_port.single) {
        err = regrow();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "two draw buffers again");
        } else {
            ESP_LOGW(TAG, "second draw buffer: no %u B internal DMA block, retrying at screen-on",
                     (unsigned)s_port.buf_bytes);
        }
    } else if (single) {
        s_port.want_double = false;
    }
    lv_unlock();
    return err;
#else
    (void)single;
    return ESP_ERR_NOT_SUPPORTED; // direct mode streams through both bounce buffers
#endif
}

void s3w_lvgl_port_stats(s3w_lvgl_port_stats_t *out, bool reset)
{
    lv_lock();
    if (out) {
        *out = s_port.stats;
    }
    if (reset) {
        memset(&s_port.stats, 0, sizeof s_port.stats);
    }
    lv_unlock();
}
