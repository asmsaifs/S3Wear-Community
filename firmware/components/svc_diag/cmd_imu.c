// Console: `imu info|stream|steps|tap|wom|fifo|selftest|off` — QMI8658 bring-up (P1-06).
// Interrupt-driven modes log from the INT1 handler (FreeRTOS timer task).
// The commands take the IMU from svc_sensors (raise to wake is off meanwhile); one
// that finishes gives it back, `imu off` gives it back after tap/wom/steps.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp_s3w.h"
#include "esp_console.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "svc_diag_priv.h"
#include "svc_sensors.h"

static const char *TAG = "cmd_imu";

#define FIFO_FRAMES    QMI8658_FIFO_64
#define FIFO_WATERMARK 32  // frames: at 62.5 Hz the INT fires about every 0.5 s
#define FIFO_BUF_BYTES (128 * 12)

static uint8_t s_fifo_buf[FIFO_BUF_BYTES];
static bool s_fifo_acc;
static bool s_fifo_gyr;

static void on_imu_int(void *ctx)
{
    (void)ctx;
    qmi8658_handle_t imu = bsp_imu_handle();
    qmi8658_irq_t irq;
    if (qmi8658_read_irq(imu, &irq) != ESP_OK) {
        return;
    }
    const uint8_t known = QMI8658_ST1_WOM | QMI8658_ST1_TAP | QMI8658_ST1_PEDO;
    if (!(irq.status1 & known) && !(irq.fifo & (QMI8658_FIFO_WTM | QMI8658_FIFO_FULL))) {
        ESP_LOGI(TAG, "INT1 edge, no known source: statusint 0x%02X status1 0x%02X fifo 0x%02X", irq.status_int,
                 irq.status1, irq.fifo);
    }
    if (irq.status1 & QMI8658_ST1_WOM) {
        ESP_LOGI(TAG, "wake-on-motion IRQ");
    }
    if (irq.status1 & QMI8658_ST1_TAP) {
        qmi8658_tap_t tap;
        char axis;
        bool neg;
        if (qmi8658_tap_read(imu, &tap, &axis, &neg) == ESP_OK && tap != QMI8658_TAP_NONE) {
            ESP_LOGI(TAG, "%s tap on %c%c", tap == QMI8658_TAP_DOUBLE ? "double" : "single", neg ? '-' : '+', axis);
        }
    }
    if (irq.status1 & QMI8658_ST1_PEDO) {
        uint32_t steps = 0;
        if (qmi8658_pedometer_read(imu, &steps) == ESP_OK) {
            ESP_LOGI(TAG, "pedometer IRQ: %lu steps", (unsigned long)steps);
        }
    }
    if (irq.fifo & (QMI8658_FIFO_WTM | QMI8658_FIFO_FULL)) {
        size_t len = 0;
        if (qmi8658_fifo_read(imu, s_fifo_buf, sizeof s_fifo_buf, &len) == ESP_OK) {
            qmi8658_raw3_t acc;
            const size_t frames = qmi8658_fifo_parse(s_fifo_buf, len, s_fifo_acc, s_fifo_gyr, &acc, NULL, 1);
            const size_t frame_bytes = (s_fifo_acc ? 6 : 0) + (s_fifo_gyr ? 6 : 0);
            ESP_LOGI(TAG, "FIFO watermark: %u frames%s, first accel raw %d %d %d",
                     (unsigned)(frame_bytes ? len / frame_bytes : 0), irq.fifo & QMI8658_FIFO_OVERFLOW ? " (overflow)" : "",
                     frames ? acc.x : 0, frames ? acc.y : 0, frames ? acc.z : 0);
        }
    }
}

static int imu_off(qmi8658_handle_t imu)
{
    // Reset puts every engine, FIFO and WoM back to defaults with both sensors off.
    // INT1 glitches during the reset; don't report those edges.
    bsp_imu_set_int_cb(NULL, NULL);
    const esp_err_t err = qmi8658_reset(imu);
    vTaskDelay(pdMS_TO_TICKS(20));
    bsp_imu_set_int_cb(on_imu_int, NULL);
    return err == ESP_OK ? 0 : 1;
}

static int imu_info(qmi8658_handle_t imu)
{
    uint8_t id = 0;
    uint8_t rev = 0;
    int16_t cdeg = 0;
    // The temperature register only updates while a sensor runs: sample with accel on.
    esp_err_t err = qmi8658_read_id(imu, &id, &rev);
    if (err == ESP_OK) {
        err = qmi8658_enable(imu, true, false);
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    if (err == ESP_OK) {
        err = qmi8658_read_temp(imu, &cdeg);
    }
    qmi8658_enable(imu, false, false);
    if (err != ESP_OK) {
        printf("QMI8658 not responding\n");
        return 1;
    }
    printf("QMI8658 who_am_i 0x%02X rev 0x%02X, temp %d.%02d C\n", id, rev, cdeg / 100, abs(cdeg % 100));
    return 0;
}

static int imu_stream(qmi8658_handle_t imu, int seconds)
{
    if (qmi8658_reset(imu) != ESP_OK || qmi8658_config_accel(imu, QMI8658_ACC_4G, QMI8658_ODR_62_5HZ) != ESP_OK ||
        qmi8658_config_gyro(imu, QMI8658_GYR_512DPS, QMI8658_ODR_62_5HZ) != ESP_OK ||
        qmi8658_enable(imu, true, true) != ESP_OK) {
        printf("config failed\n");
        return 1;
    }
    vTaskDelay(pdMS_TO_TICKS(100)); // gyro start-up
    printf("   ax mg    ay mg    az mg |  gx dps  gy dps  gz dps\n");
    for (int i = 0; i < seconds * 10; i++) {
        int32_t a[3];
        int32_t g[3];
        if (qmi8658_read(imu, a, g) == ESP_OK) {
            printf("%8ld %8ld %8ld | %7ld %7ld %7ld\n", (long)a[0], (long)a[1], (long)a[2], (long)(g[0] / 1000),
                   (long)(g[1] / 1000), (long)(g[2] / 1000));
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return imu_off(imu);
}

static int imu_steps(qmi8658_handle_t imu, int seconds)
{
    if (qmi8658_reset(imu) != ESP_OK || qmi8658_pedometer_enable(imu) != ESP_OK ||
        qmi8658_pedometer_reset(imu) != ESP_OK) {
        printf("pedometer config failed\n");
        return 1;
    }
    printf("pedometer on for %d s: walk (counting starts after 10 steps, updates every 4)\n", seconds);
    uint32_t last = UINT32_MAX;
    for (int i = 0; i < seconds; i++) {
        uint32_t steps = 0;
        if (qmi8658_pedometer_read(imu, &steps) == ESP_OK && steps != last) {
            printf("steps: %lu\n", (unsigned long)steps);
            last = steps;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    printf("pedometer stays on; `imu off` to stop\n");
    return 0;
}

static int imu_fifo(qmi8658_handle_t imu, int seconds)
{
    s_fifo_acc = true;
    s_fifo_gyr = true;
    if (qmi8658_reset(imu) != ESP_OK || qmi8658_config_accel(imu, QMI8658_ACC_4G, QMI8658_ODR_62_5HZ) != ESP_OK ||
        qmi8658_config_gyro(imu, QMI8658_GYR_512DPS, QMI8658_ODR_62_5HZ) != ESP_OK ||
        qmi8658_enable(imu, true, true) != ESP_OK || qmi8658_fifo_config(imu, FIFO_FRAMES, FIFO_WATERMARK) != ESP_OK) {
        printf("FIFO config failed\n");
        return 1;
    }
    printf("FIFO: 64 frames, watermark %d, INT1 -> drain for %d s\n", FIFO_WATERMARK, seconds);
    vTaskDelay(pdMS_TO_TICKS(seconds * 1000));
    return imu_off(imu);
}

static int imu_selftest(qmi8658_handle_t imu)
{
    bool acc_pass = false;
    bool gyr_pass = false;
    int32_t mg[3] = {0};
    int32_t dps[3] = {0};
    if (qmi8658_reset(imu) != ESP_OK || qmi8658_selftest_accel(imu, &acc_pass, mg) != ESP_OK) {
        printf("accel self-test did not complete\n");
        return 1;
    }
    printf("accel self-test: %s (dV %ld %ld %ld mg, need > 200)\n", acc_pass ? "PASS" : "FAIL", (long)mg[0],
           (long)mg[1], (long)mg[2]);
    if (qmi8658_reset(imu) != ESP_OK || qmi8658_selftest_gyro(imu, &gyr_pass, dps) != ESP_OK) {
        printf("gyro self-test did not complete\n");
        return 1;
    }
    printf("gyro  self-test: %s (dV %ld %ld %ld dps, need > 300)\n", gyr_pass ? "PASS" : "FAIL", (long)dps[0],
           (long)dps[1], (long)dps[2]);
    imu_off(imu);
    return acc_pass && gyr_pass ? 0 : 1;
}

static int cmd_imu(int argc, char **argv)
{
    qmi8658_handle_t imu = bsp_imu_handle();
    if (!imu) {
        printf("IMU not initialised\n");
        return 1;
    }
    const char *sub = argc >= 2 ? argv[1] : "";
    const int arg = argc >= 3 ? atoi(argv[2]) : 0;
    static const char *const k_subs[] = {"info", "stream", "steps", "tap", "wom", "fifo", "selftest", "off"};
    for (size_t i = 0; i < sizeof k_subs / sizeof k_subs[0]; i++) {
        if (strcmp(sub, k_subs[i]) == 0) {
            svc_sensors_suspend(true); // fails only if svc_sensors is not running
            break;
        }
    }
    bsp_imu_set_int_cb(on_imu_int, NULL);
    if (strcmp(sub, "info") == 0) {
        const int ret = imu_info(imu);
        svc_sensors_suspend(false);
        return ret;
    }
    if (strcmp(sub, "stream") == 0) {
        const int ret = imu_stream(imu, arg > 0 ? arg : 5);
        svc_sensors_suspend(false);
        return ret;
    }
    if (strcmp(sub, "steps") == 0) {
        return imu_steps(imu, arg > 0 ? arg : 60);
    }
    if (strcmp(sub, "tap") == 0) {
        if (qmi8658_reset(imu) != ESP_OK || qmi8658_tap_enable(imu) != ESP_OK) {
            printf("tap config failed\n");
            return 1;
        }
        printf("tap detection on (INT1); tap the watch. `imu off` to stop\n");
        return 0;
    }
    if (strcmp(sub, "wom") == 0) {
        const int mg = arg > 0 && arg < 256 ? arg : 100;
        if (qmi8658_reset(imu) != ESP_OK || qmi8658_wom_enable(imu, (uint8_t)mg, QMI8658_ODR_LP_21HZ) != ESP_OK) {
            printf("WoM config failed\n");
            return 1;
        }
        printf("wake-on-motion on (%d mg, accel low-power 21 Hz); move the watch. `imu off` to stop\n", mg);
        return 0;
    }
    if (strcmp(sub, "fifo") == 0) {
        const int ret = imu_fifo(imu, arg > 0 ? arg : 5);
        svc_sensors_suspend(false);
        return ret;
    }
    if (strcmp(sub, "selftest") == 0) {
        const int ret = imu_selftest(imu);
        svc_sensors_suspend(false);
        return ret;
    }
    if (strcmp(sub, "reg") == 0 && argc >= 3) {
        const uint8_t reg = (uint8_t)strtol(argv[2], NULL, 16);
        if (argc >= 4) {
            return qmi8658_reg_write(imu, reg, (uint8_t)strtol(argv[3], NULL, 16)) == ESP_OK ? 0 : 1;
        }
        uint8_t v[16];
        const int n = 16;
        if (qmi8658_reg_read(imu, reg, v, n) != ESP_OK) {
            return 1;
        }
        printf("0x%02X:", reg);
        for (int i = 0; i < n; i++) {
            printf(" %02X", v[i]);
        }
        printf("\n");
        return 0;
    }
    if (strcmp(sub, "off") == 0) {
        const int ret = imu_off(imu);
        svc_sensors_suspend(false);
        return ret;
    }
    printf("usage: imu info | stream [s] | steps [s] | tap | wom [mg] | fifo [s] | selftest | off | reg <hex> [val]\n");
    return 1;
}

esp_err_t diag_register_imu(void)
{
    const esp_console_cmd_t cmd = {
        .command = "imu",
        .help = "QMI8658: info, stream [s] (10 Hz print), steps [s] (pedometer), tap, wom [mg], fifo [s] "
                "(watermark IRQ), selftest, off",
        .hint = "info|stream|steps|tap|wom|fifo|selftest|off",
        .func = cmd_imu,
    };
    return esp_console_cmd_register(&cmd);
}
