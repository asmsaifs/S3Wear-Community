// Board support for the Waveshare ESP32-S3-Touch-AMOLED-2.06.
// Owns the shared I2C bus, the two buttons and safe pin states at boot.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "drv_ft3168.h"
#include "drv_axp2101.h"
#include "drv_pcf85063.h"
#include "drv_qmi8658.h"
#include "esp_event.h"
#include "sdmmc_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * First boot step (docs/02-firmware-architecture.md §2): PA_CTRL low, I2C master
 * bus at 400 kHz, button GPIOs with debounce. Idempotent.
 */
esp_err_t bsp_init_early(void);

/** The one shared I2C bus handle (NULL before bsp_init_early()). */
i2c_master_bus_handle_t bsp_i2c_bus(void);

/** Add a device on the shared bus at BSP_I2C_FREQ_HZ; each driver owns its handle. */
esp_err_t bsp_i2c_add_device(uint8_t addr_7bit, i2c_master_dev_handle_t *out_dev);

/** Human-readable name of a known on-board I2C address, or NULL. */
const char *bsp_i2c_device_name(uint8_t addr_7bit);

/** Number of I2C devices populated on the board. */
#define BSP_I2C_DEVICE_COUNT 6

/** 7-bit address of on-board I2C device idx (0..BSP_I2C_DEVICE_COUNT-1). */
uint8_t bsp_i2c_device_addr(size_t idx);

typedef enum {
    BSP_BUTTON_BOOT = 0, // GPIO0
    BSP_BUTTON_PWR,      // GPIO10 SYS_OUT mirror of the AXP2101 power key
    BSP_BUTTON_COUNT,
} bsp_button_t;

/**
 * Called after debounce on every stable level change. Runs in the FreeRTOS timer
 * service task: keep it short and never block (post an event instead).
 */
typedef void (*bsp_button_cb_t)(bsp_button_t button, bool pressed, void *ctx);

esp_err_t bsp_buttons_set_callback(bsp_button_cb_t cb, void *ctx);

/** Debounced state. */
bool bsp_button_is_pressed(bsp_button_t button);

const char *bsp_button_name(bsp_button_t button);

// --- Display (CO5300 AMOLED over QSPI) ---------------------------------------

typedef struct {
    esp_lcd_panel_io_color_trans_done_cb_t on_color_trans_done; // ISR context
    void *user_ctx;
} bsp_display_cbs_t;

/**
 * Create the QSPI bus, panel IO and CO5300 panel; reset + init the panel (display
 * on, brightness 0). max_transfer_bytes = largest draw_bitmap the caller will send.
 * keep_frame: attach to a panel left on through deep sleep (bsp_deep_sleep_start()
 * with keep_display): no reset and no init, it keeps its frame and brightness.
 */
esp_err_t bsp_display_new(size_t max_transfer_bytes, const bsp_display_cbs_t *cbs, bool keep_frame,
                          esp_lcd_panel_handle_t *out_panel, esp_lcd_panel_io_handle_t *out_io);

/** Panel handle after bsp_display_new(), else NULL. */
esp_lcd_panel_handle_t bsp_display_panel(void);

/**
 * Block until the next LCD_TE rising edge (panel V-blank). ESP_ERR_TIMEOUT if none.
 * Single consumer: the LVGL flush uses it; diagnostics use bsp_display_te_count_edges().
 */
esp_err_t bsp_display_te_wait(uint32_t timeout_ms);

/** Diagnostics: rising edges seen on LCD_TE by busy-polling for window_ms. */
int bsp_display_te_count_edges(uint32_t window_ms);

/** Panel brightness 0..255 (DCS 0x51). */
esp_err_t bsp_display_brightness_set(uint8_t level);

/**
 * Off: display off (0x28) + sleep in (0x10). On: sleep out (0x11, then the panel's
 * 120 ms wake time) + display on (0x29). Blocks; no LVGL flush may run meanwhile.
 */
esp_err_t bsp_display_power(bool on);

// --- Touch (FT3168) ----------------------------------------------------------

/** Create the FT3168 and its INT-driven reader task (core 0). After bsp_init_early(). */
esp_err_t bsp_touch_start(void);

/** Latest touch state; never blocks, never touches I2C (safe from the UI task). */
bool bsp_touch_get(uint16_t *x, uint16_t *y);

/** Called from the touch task on every touch-down (also the first touch in monitor mode). */
typedef void (*bsp_touch_event_cb_t)(uint16_t x, uint16_t y, void *ctx);
void bsp_touch_set_event_cb(bsp_touch_event_cb_t cb, void *ctx);
void bsp_touch_get_event_cb(bsp_touch_event_cb_t *cb, void **ctx);

/** One touch sample while in contact, and a final one with points = 0 on release. */
typedef struct {
    uint8_t points;   // FT3168 point count as reported (more than 2 = large contact)
    uint8_t area_max; // largest per-point contact area (0..15)
    uint16_t x_min;   // bounding box of the tracked points
    uint16_t y_min;
    uint16_t x_max;
    uint16_t y_max;
} bsp_touch_contact_t;

/** Called from the touch task for every sample (every 10 ms while touched): short. */
typedef void (*bsp_touch_contact_cb_t)(const bsp_touch_contact_t *c, void *ctx);
void bsp_touch_set_contact_cb(bsp_touch_contact_cb_t cb, void *ctx);

/**
 * true: monitor mode (slow scan, a touch raises INT and the controller goes active
 * by itself). false: active. Blocking I2C.
 * The FT3168 also enters monitor mode by itself after ~25 s without a touch, even
 * with ID_G_CTRL = 0, and then ignores I2C until touched. INT and touches keep
 * working, so when it does not ACK this does nothing and returns ESP_OK.
 */
esp_err_t bsp_touch_set_low_power(bool low_power);

/** Driver handle for power modes / ID (NULL before bsp_touch_start()). */
ft3168_handle_t bsp_touch_handle(void);

// --- RTC (PCF85063) ----------------------------------------------------------

/** Create the RTC driver and hook the alarm INT. After bsp_init_early(). */
esp_err_t bsp_rtc_start(void);

pcf85063_handle_t bsp_rtc_handle(void);

/** Alarm fired (AF already cleared). Runs in the FreeRTOS timer task: keep it short. */
typedef void (*bsp_rtc_alarm_cb_t)(void *ctx);
void bsp_rtc_set_alarm_cb(bsp_rtc_alarm_cb_t cb, void *ctx);

// --- PMU (AXP2101) ------------------------------------------------------------

/** Events on the default esp_event loop (no payload; query bsp_pmu_handle() for values). */
ESP_EVENT_DECLARE_BASE(BSP_PMU_EVENT);
typedef enum {
    BSP_PMU_EVT_VBUS_IN,
    BSP_PMU_EVT_VBUS_OUT,
    BSP_PMU_EVT_BAT_IN,
    BSP_PMU_EVT_BAT_OUT,
    BSP_PMU_EVT_CHG_START,
    BSP_PMU_EVT_CHG_DONE,
    BSP_PMU_EVT_PKEY_SHORT,
    BSP_PMU_EVT_PKEY_LONG,
    BSP_PMU_EVT_BAT_LOW,
    BSP_PMU_EVT_OVERTEMP,
} bsp_pmu_event_t;

/**
 * Create the AXP2101 driver; charger 200 mA CC / 25 mA term / 4.2 V, TS input off,
 * ADCs + fuel gauge on, PWR held 6 s = hard power-off, start IRQ polling.
 * Creates the default event loop if needed. After bsp_init_early().
 */
esp_err_t bsp_pmu_start(void);

axp2101_handle_t bsp_pmu_handle(void);

const char *bsp_pmu_event_name(bsp_pmu_event_t evt);

// --- IMU (QMI8658) ------------------------------------------------------------

/** Create the IMU driver (reset, sensors off) and hook INT1. After bsp_init_early(). */
esp_err_t bsp_imu_start(void);

qmi8658_handle_t bsp_imu_handle(void);

/**
 * IMU axes in the watch frame: watch axis i = sign[i] * IMU axis map[i], with the
 * watch frame +x toward 3 o'clock, +y toward 12 o'clock, +z out of the screen
 * (lying face up at rest: z = +1000 mg). Check with console `sensors accel`.
 * Measured on the watch: the QMI8658 sits face down with its +x toward 12 o'clock
 * (face up: chip z = -1000 mg; 12 o'clock up: chip x = +1000 mg), so watch x = chip y,
 * watch y = chip x, watch z = -chip z.
 */
#define BSP_IMU_AXIS_MAP  {1, 0, 2}
#define BSP_IMU_AXIS_SIGN {1, 1, -1}

/** INT1 edge. Runs in the FreeRTOS timer task; read qmi8658_read_irq() there or hand off.
 *  INT1 is re-armed as a light-sleep wake source after the callback returns. */
typedef void (*bsp_imu_int_cb_t)(void *ctx);
void bsp_imu_set_int_cb(bsp_imu_int_cb_t cb, void *ctx);

// --- Audio (ES8311 + ES7210, use drv_audio.h after start) -----------------------

/** Create the I2S port and both codecs (powered down until opened). After bsp_init_early(). */
esp_err_t bsp_audio_start(void);
bool bsp_audio_ready(void);

// --- TF card (SPI) ------------------------------------------------------------

/** Init SPI3 (once) and mount FAT at base_path. Never formats. Fails if no card. */
esp_err_t bsp_sdcard_mount(const char *base_path, sdmmc_card_t **out_card);
esp_err_t bsp_sdcard_unmount(void);
/** Mounted card or NULL. */
sdmmc_card_t *bsp_sdcard(void);
/** Mounted and still answering (CMD13). */
bool bsp_sdcard_present(void);

// --- Sleep (bsp_sleep.c) ---------------------------------------------------------

/** Wake inputs for bsp_wake_arm(). */
enum {
    BSP_WAKE_TOUCH = 1u << 0,   // TP_INT (GPIO38)
    BSP_WAKE_BUTTONS = 1u << 1, // BOOT (GPIO0) and PWR SYS_OUT (GPIO10)
    BSP_WAKE_RTC = 1u << 2,     // RTC_INT (GPIO39)
    BSP_WAKE_IMU = 1u << 3,     // IMU INT1 (GPIO21), either level change (wake-on-motion toggles it)
};

/**
 * Keep the pins whose level matters (PA_CTRL low, LCD/touch reset and chip
 * selects, I2C) out of the automatic GPIO isolation in light sleep, and enable
 * GPIO wake-up. After bsp_init_early().
 */
esp_err_t bsp_sleep_init(void);

/**
 * Switch the sources' pins to level interrupts with light-sleep wake-up. The ISR
 * of a pin that fires puts it back to its edge interrupt (so a held level cannot
 * storm) and the BSP re-arms it once the event is handled, until bsp_wake_disarm().
 */
esp_err_t bsp_wake_arm(uint32_t sources);
void bsp_wake_disarm(void);

/** Deep sleep with ext1 wake-up on PWR SYS_OUT high and, if timer_us > 0, the RTC
 *  timer after timer_us (WATCH-ONLY minute ticks, alarms); PA_CTRL held low.
 *  keep_display: LCD reset and CS held high (RTC IOs) so the panel keeps scanning
 *  its frame; bsp_display_new(keep_frame) releases them. No return. */
esp_err_t bsp_deep_sleep_start(uint64_t timer_us, bool keep_display);

#ifdef __cplusplus
}
#endif
