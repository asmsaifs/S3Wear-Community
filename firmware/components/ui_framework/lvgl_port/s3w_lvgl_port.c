#include "s3w_lvgl_port.h"

#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "s3w_task.h"
#include "s3w_ui.h"
#include "sdkconfig.h"
#include "src/core/lv_global.h"

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
#if CONFIG_PM_ENABLE
    esp_pm_lock_handle_t frame_lock; // no light sleep while a frame is sent
#endif
#if CONFIG_S3W_LVGL_RENDER_DIRECT
    uint16_t *bounce[2]; // internal DMA, cfg.buf_lines lines each
    uint8_t bounce_idx;
#endif
    s3w_lvgl_port_stats_t stats;
} port_t;

static port_t s_port;

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

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    port_t *p = lv_display_get_driver_data(disp);
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

    lv_init();
    lv_tick_set_cb(tick_cb);

    const size_t buf_bytes = S3W_LVGL_PORT_BUF_BYTES(cfg->hres, cfg->buf_lines);
    void *buf1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void *buf2 = heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!buf1 || !buf2) {
        heap_caps_free(buf1);
        heap_caps_free(buf2);
        ESP_LOGE(TAG, "draw buffers (2 x %u B internal DMA)", (unsigned)buf_bytes);
        return ESP_ERR_NO_MEM;
    }

    lv_display_t *disp = lv_display_create(cfg->hres, cfg->vres);
    ESP_RETURN_ON_FALSE(disp, ESP_ERR_NO_MEM, TAG, "display");
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
#if CONFIG_S3W_LVGL_RENDER_PARTIAL
    lv_display_set_buffers(disp, buf1, buf2, buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
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

void s3w_lvgl_port_set_output(bool on)
{
    if (s_port.output_off == !on) {
        return;
    }
    s_port.output_off = !on;
    if (on && s_port.disp) {
        // Everything drawn while off was dropped: repaint the whole panel.
        lv_obj_invalidate(lv_display_get_screen_active(s_port.disp));
    }
}

void s3w_lvgl_port_set_low_power(bool low_power)
{
    s_port.low_power = low_power;
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
