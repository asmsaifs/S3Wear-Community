#include "drv_qmi8658.h"

#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "drv_qmi8658";

#define REG_WHO_AM_I    0x00
#define REG_REVISION    0x01
#define REG_CTRL1       0x02
#define REG_CTRL2       0x03
#define REG_CTRL3       0x04
#define REG_CTRL7       0x08
#define REG_CTRL8       0x09
#define REG_CTRL9       0x0A
#define REG_CAL1_L      0x0B
#define REG_FIFO_WTM    0x13
#define REG_FIFO_CTRL   0x14
#define REG_FIFO_CNT    0x15
#define REG_FIFO_STATUS 0x16
#define REG_FIFO_DATA   0x17
#define REG_STATUSINT   0x2D
#define REG_STATUS1     0x2F
#define REG_TEMP_L      0x33
#define REG_AX_L        0x35
#define REG_RST_RESULT  0x4D
#define REG_DVX_L       0x51
#define REG_TAP_STATUS  0x59
#define REG_STEP_L      0x5A
#define REG_RESET       0x60

#define WHO_AM_I_VAL    0x05
#define RESET_CMD       0xB0
#define RESET_DONE      0x80

#define CTRL1_ADDR_AI   0x40
#define CTRL1_INT1_EN   0x08
#define CTRL1_FIFO_INT1 0x04
#define CTRL7_AEN       0x01
#define CTRL7_GEN       0x02
#define CTRL7_NO_DRDY   0x20
#define CTRL8_HANDSHAKE 0x80 // CTRL9 handshake on STATUSINT.bit7
#define CTRL8_ACT_INT1  0x40
#define CTRL8_PEDO      0x10
#define CTRL8_TAP       0x01
#define STATUSINT_AVAIL 0x01
#define STATUSINT_CMD   0x80
#define FIFO_MODE_STREAM 0x02
#define FIFO_RD_MODE    0x80

#define CMD_ACK          0x00
#define CMD_RST_FIFO     0x04
#define CMD_REQ_FIFO     0x05
#define CMD_WOM_SETTING  0x08
#define CMD_CONFIG_TAP   0x0C
#define CMD_CONFIG_PEDO  0x0D
#define CMD_RESET_PEDO   0x0F

#define I2C_TIMEOUT_MS   50
#define CMD_TIMEOUT_MS   100
#define SELFTEST_TIMEOUT_MS 1000

struct qmi8658_s {
    i2c_master_dev_handle_t dev;
    qmi8658_acc_range_t acc_range;
    qmi8658_gyr_range_t gyr_range;
    bool acc_on;
    bool gyr_on;
    uint8_t fifo_ctrl;
};

static esp_err_t rd(qmi8658_handle_t h, uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(h->dev, &reg, 1, data, len, I2C_TIMEOUT_MS);
}

static esp_err_t rd8(qmi8658_handle_t h, uint8_t reg, uint8_t *v)
{
    return rd(h, reg, v, 1);
}

static esp_err_t wr8(qmi8658_handle_t h, uint8_t reg, uint8_t v)
{
    const uint8_t buf[2] = {reg, v};
    return i2c_master_transmit(h->dev, buf, sizeof buf, I2C_TIMEOUT_MS);
}

static esp_err_t update(qmi8658_handle_t h, uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t v;
    ESP_RETURN_ON_ERROR(rd8(h, reg, &v), TAG, "rd 0x%02X", reg);
    return wr8(h, reg, (uint8_t)((v & ~mask) | (value & mask)));
}

static esp_err_t wait_statusint(qmi8658_handle_t h, uint8_t bit, bool set, uint32_t timeout_ms)
{
    const int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    for (;;) {
        uint8_t v = 0;
        ESP_RETURN_ON_ERROR(rd8(h, REG_STATUSINT, &v), TAG, "statusint");
        if (((v & bit) != 0) == set) {
            return ESP_OK;
        }
        if (esp_timer_get_time() > deadline) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// CTRL9 command with the STATUSINT.bit7 handshake: write cmd, wait done, ACK, wait clear.
static esp_err_t ctrl9(qmi8658_handle_t h, uint8_t cmd)
{
    ESP_RETURN_ON_ERROR(wr8(h, REG_CTRL9, cmd), TAG, "ctrl9 0x%02X", cmd);
    ESP_RETURN_ON_ERROR(wait_statusint(h, STATUSINT_CMD, true, CMD_TIMEOUT_MS), TAG, "cmd 0x%02X done", cmd);
    ESP_RETURN_ON_ERROR(wr8(h, REG_CTRL9, CMD_ACK), TAG, "ack");
    return wait_statusint(h, STATUSINT_CMD, false, CMD_TIMEOUT_MS);
}

static esp_err_t write_cal(qmi8658_handle_t h, const uint8_t cal[8])
{
    for (int i = 0; i < 8; i++) {
        ESP_RETURN_ON_ERROR(wr8(h, REG_CAL1_L + i, cal[i]), TAG, "cal");
    }
    return ESP_OK;
}

static esp_err_t apply_enable(qmi8658_handle_t h)
{
    const uint8_t v = CTRL7_NO_DRDY | (h->acc_on ? CTRL7_AEN : 0) | (h->gyr_on ? CTRL7_GEN : 0);
    return wr8(h, REG_CTRL7, v);
}

esp_err_t qmi8658_new(i2c_master_bus_handle_t bus, uint8_t addr, uint32_t scl_hz, qmi8658_handle_t *out)
{
    ESP_RETURN_ON_FALSE(bus && out, ESP_ERR_INVALID_ARG, TAG, "args");
    struct qmi8658_s *h = calloc(1, sizeof *h);
    ESP_RETURN_ON_FALSE(h, ESP_ERR_NO_MEM, TAG, "no mem");
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = scl_hz,
    };
    esp_err_t ret = i2c_master_bus_add_device(bus, &cfg, &h->dev);
    if (ret != ESP_OK) {
        free(h);
        return ret;
    }
    ESP_GOTO_ON_ERROR(qmi8658_reset(h), fail, TAG, "reset");
    *out = h;
    return ESP_OK;
fail:
    i2c_master_bus_rm_device(h->dev);
    free(h);
    return ret;
}

esp_err_t qmi8658_reset(qmi8658_handle_t h)
{
    ESP_RETURN_ON_ERROR(wr8(h, REG_RESET, RESET_CMD), TAG, "reset");
    // Reset takes up to 15 ms; RST_RESULT reads 0x80 when done.
    uint8_t v = 0;
    for (int i = 0; i < 10 && v != RESET_DONE; i++) {
        vTaskDelay(pdMS_TO_TICKS(5));
        rd8(h, REG_RST_RESULT, &v);
    }
    ESP_RETURN_ON_FALSE(v == RESET_DONE, ESP_ERR_TIMEOUT, TAG, "reset result 0x%02X", v);
    uint8_t id = 0;
    ESP_RETURN_ON_ERROR(rd8(h, REG_WHO_AM_I, &id), TAG, "who_am_i");
    ESP_RETURN_ON_FALSE(id == WHO_AM_I_VAL, ESP_ERR_NOT_FOUND, TAG, "who_am_i 0x%02X", id);

    ESP_RETURN_ON_ERROR(wr8(h, REG_CTRL1, CTRL1_ADDR_AI), TAG, "ctrl1"); // little-endian, auto-increment
    ESP_RETURN_ON_ERROR(wr8(h, REG_CTRL8, CTRL8_HANDSHAKE), TAG, "ctrl8");
    h->acc_on = false;
    h->gyr_on = false;
    h->fifo_ctrl = 0;
    h->acc_range = QMI8658_ACC_2G;
    h->gyr_range = QMI8658_GYR_16DPS;
    return apply_enable(h);
}

esp_err_t qmi8658_read_id(qmi8658_handle_t h, uint8_t *who_am_i, uint8_t *revision)
{
    ESP_RETURN_ON_ERROR(rd8(h, REG_WHO_AM_I, who_am_i), TAG, "id");
    return rd8(h, REG_REVISION, revision);
}

esp_err_t qmi8658_config_accel(qmi8658_handle_t h, qmi8658_acc_range_t range, qmi8658_odr_t odr)
{
    ESP_RETURN_ON_ERROR(wr8(h, REG_CTRL2, (uint8_t)((range << 4) | odr)), TAG, "ctrl2");
    h->acc_range = range;
    return ESP_OK;
}

esp_err_t qmi8658_config_gyro(qmi8658_handle_t h, qmi8658_gyr_range_t range, qmi8658_odr_t odr)
{
    ESP_RETURN_ON_FALSE(odr <= QMI8658_ODR_31_25HZ, ESP_ERR_INVALID_ARG, TAG, "gyro has no low-power ODR");
    ESP_RETURN_ON_ERROR(wr8(h, REG_CTRL3, (uint8_t)((range << 4) | odr)), TAG, "ctrl3");
    h->gyr_range = range;
    return ESP_OK;
}

esp_err_t qmi8658_enable(qmi8658_handle_t h, bool accel, bool gyro)
{
    h->acc_on = accel;
    h->gyr_on = gyro;
    return apply_enable(h);
}

esp_err_t qmi8658_read(qmi8658_handle_t h, int32_t acc_mg[3], int32_t gyr_mdps[3])
{
    uint8_t b[12];
    ESP_RETURN_ON_ERROR(rd(h, REG_AX_L, b, sizeof b), TAG, "data");
    if (acc_mg) {
        const qmi8658_raw3_t a = qmi8658_unpack3(b);
        acc_mg[0] = qmi8658_acc_mg(a.x, h->acc_range);
        acc_mg[1] = qmi8658_acc_mg(a.y, h->acc_range);
        acc_mg[2] = qmi8658_acc_mg(a.z, h->acc_range);
    }
    if (gyr_mdps) {
        const qmi8658_raw3_t g = qmi8658_unpack3(b + 6);
        gyr_mdps[0] = qmi8658_gyr_mdps(g.x, h->gyr_range);
        gyr_mdps[1] = qmi8658_gyr_mdps(g.y, h->gyr_range);
        gyr_mdps[2] = qmi8658_gyr_mdps(g.z, h->gyr_range);
    }
    return ESP_OK;
}

esp_err_t qmi8658_read_temp(qmi8658_handle_t h, int16_t *cdeg)
{
    uint8_t b[2];
    ESP_RETURN_ON_ERROR(rd(h, REG_TEMP_L, b, 2), TAG, "temp");
    *cdeg = qmi8658_temp_cdeg(b[0], b[1]);
    return ESP_OK;
}

// --- FIFO ----------------------------------------------------------------------

esp_err_t qmi8658_fifo_config(qmi8658_handle_t h, qmi8658_fifo_size_t size, uint8_t watermark, bool int1)
{
    const bool acc = h->acc_on;
    const bool gyr = h->gyr_on;
    ESP_RETURN_ON_ERROR(qmi8658_enable(h, false, false), TAG, "off");
    ESP_RETURN_ON_ERROR(ctrl9(h, CMD_RST_FIFO), TAG, "rst fifo");
    h->fifo_ctrl = (uint8_t)((size << 2) | FIFO_MODE_STREAM);
    ESP_RETURN_ON_ERROR(wr8(h, REG_FIFO_CTRL, h->fifo_ctrl), TAG, "fifo ctrl");
    ESP_RETURN_ON_ERROR(wr8(h, REG_FIFO_WTM, watermark), TAG, "wtm");
    if (int1) {
        ESP_RETURN_ON_ERROR(
            update(h, REG_CTRL1, CTRL1_FIFO_INT1 | CTRL1_INT1_EN, CTRL1_FIFO_INT1 | CTRL1_INT1_EN), TAG, "int1");
    } else {
        // FIFO interrupt to INT2, which is neither enabled nor wired: polled. INT1 keeps
        // whatever else uses it (wake-on-motion).
        ESP_RETURN_ON_ERROR(update(h, REG_CTRL1, CTRL1_FIFO_INT1, 0), TAG, "int2");
    }
    return qmi8658_enable(h, acc, gyr);
}

esp_err_t qmi8658_fifo_read(qmi8658_handle_t h, uint8_t *buf, size_t cap, size_t *len, uint8_t *status)
{
    *len = 0;
    uint8_t cnt[2]; // FIFO_SMPL_CNT, FIFO_STATUS
    ESP_RETURN_ON_ERROR(rd(h, REG_FIFO_CNT, cnt, 2), TAG, "fifo cnt");
    if (status) {
        *status = cnt[1];
    }
    if (!(cnt[1] & QMI8658_FIFO_NOT_EMPTY)) {
        return ESP_OK;
    }
    size_t n = qmi8658_fifo_bytes(cnt[0], cnt[1]);
    if (n > cap) {
        n = cap;
    }
    ESP_RETURN_ON_ERROR(ctrl9(h, CMD_REQ_FIFO), TAG, "req fifo"); // enters FIFO read mode
    esp_err_t err = rd(h, REG_FIFO_DATA, buf, n);
    // Leave read mode so new samples are stored again.
    esp_err_t err2 = wr8(h, REG_FIFO_CTRL, h->fifo_ctrl & ~FIFO_RD_MODE);
    ESP_RETURN_ON_ERROR(err, TAG, "fifo data");
    ESP_RETURN_ON_ERROR(err2, TAG, "fifo ctrl");
    *len = n;
    return ESP_OK;
}

esp_err_t qmi8658_fifo_disable(qmi8658_handle_t h)
{
    h->fifo_ctrl = 0;
    ESP_RETURN_ON_ERROR(wr8(h, REG_FIFO_CTRL, 0), TAG, "fifo bypass");
    return update(h, REG_CTRL1, CTRL1_FIFO_INT1, 0);
}

// --- Wake on motion --------------------------------------------------------------

static esp_err_t wom_write(qmi8658_handle_t h, uint8_t threshold_mg)
{
    // CAL1_L = threshold (1 mg/LSB, 0 = WoM off); CAL1_H[7:6] = 0b10 -> INT1, initial
    // level high; CAL1_H[5:0] = blanking samples ignored after enabling: 4 (~190 ms at
    // 21 Hz), so raise to wake is not blind for long after each re-arm.
    const uint8_t cal[8] = {threshold_mg, 0x80 | 0x04, 0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 2; i++) {
        ESP_RETURN_ON_ERROR(wr8(h, REG_CAL1_L + i, cal[i]), TAG, "cal");
    }
    return ctrl9(h, CMD_WOM_SETTING);
}

esp_err_t qmi8658_wom_enable(qmi8658_handle_t h, uint8_t threshold_mg, qmi8658_odr_t lp_odr)
{
    ESP_RETURN_ON_FALSE(threshold_mg > 0 && lp_odr >= QMI8658_ODR_LP_128HZ, ESP_ERR_INVALID_ARG, TAG, "args");
    ESP_RETURN_ON_ERROR(qmi8658_enable(h, false, false), TAG, "off");
    ESP_RETURN_ON_ERROR(qmi8658_config_accel(h, QMI8658_ACC_8G, lp_odr), TAG, "accel");
    ESP_RETURN_ON_ERROR(wom_write(h, threshold_mg), TAG, "wom");
    ESP_RETURN_ON_ERROR(update(h, REG_CTRL1, CTRL1_INT1_EN, CTRL1_INT1_EN), TAG, "int1");
    return qmi8658_enable(h, true, false);
}

esp_err_t qmi8658_wom_disable(qmi8658_handle_t h)
{
    ESP_RETURN_ON_ERROR(qmi8658_enable(h, false, false), TAG, "off");
    return wom_write(h, 0);
}

// --- Tap / pedometer engines --------------------------------------------------------

esp_err_t qmi8658_tap_enable(qmi8658_handle_t h)
{
    // Parameters from the vendor (SensorLib) example, all in samples @ 500 Hz.
    const uint8_t peak_window = 20;
    const uint16_t tap_window = 50;
    const uint16_t dtap_window = 250;
    const uint8_t priority = 0; // X > Y > Z
    const uint8_t alpha = qmi8658_u0_7(0.0625f);
    const uint8_t gamma = qmi8658_u0_7(0.25f);
    const uint16_t peak_mag = qmi8658_g2_units(0.8f);
    const uint16_t udm = qmi8658_g2_units(0.4f);

    ESP_RETURN_ON_ERROR(qmi8658_enable(h, false, false), TAG, "off");
    ESP_RETURN_ON_ERROR(qmi8658_config_accel(h, QMI8658_ACC_4G, QMI8658_ODR_500HZ), TAG, "accel");
    const uint8_t page1[8] = {peak_window, priority, tap_window & 0xFF, tap_window >> 8,
                              dtap_window & 0xFF, dtap_window >> 8, 0, 0x01};
    ESP_RETURN_ON_ERROR(write_cal(h, page1), TAG, "tap p1");
    ESP_RETURN_ON_ERROR(ctrl9(h, CMD_CONFIG_TAP), TAG, "tap cfg 1");
    const uint8_t page2[8] = {alpha, gamma, peak_mag & 0xFF, peak_mag >> 8, udm & 0xFF, udm >> 8, 0, 0x02};
    ESP_RETURN_ON_ERROR(write_cal(h, page2), TAG, "tap p2");
    ESP_RETURN_ON_ERROR(ctrl9(h, CMD_CONFIG_TAP), TAG, "tap cfg 2");

    ESP_RETURN_ON_ERROR(update(h, REG_CTRL8, CTRL8_ACT_INT1 | CTRL8_TAP, CTRL8_ACT_INT1 | CTRL8_TAP), TAG, "ctrl8");
    ESP_RETURN_ON_ERROR(update(h, REG_CTRL1, CTRL1_INT1_EN, CTRL1_INT1_EN), TAG, "int1");
    return qmi8658_enable(h, true, false);
}

esp_err_t qmi8658_tap_read(qmi8658_handle_t h, qmi8658_tap_t *tap, char *axis, bool *negative)
{
    uint8_t v = 0;
    ESP_RETURN_ON_ERROR(rd8(h, REG_TAP_STATUS, &v), TAG, "tap");
    *tap = (qmi8658_tap_t)(v & 0x03);
    static const char k_axis[] = {'-', 'x', 'y', 'z'};
    *axis = k_axis[(v >> 4) & 0x03];
    *negative = (v & 0x80) != 0;
    return ESP_OK;
}

esp_err_t qmi8658_pedometer_enable(qmi8658_handle_t h)
{
    // Vendor example values for 62.5 Hz: 50-sample window, 200 mg peak-to-peak,
    // 100 mg peak, 200-sample (3.2 s) step timeout, 20-sample min step time,
    // 10 steps to start counting, registers updated every 4 steps.
    const uint16_t sample_cnt = 50;
    const uint16_t peak2peak = 200;
    const uint16_t peak = 100;
    const uint16_t time_up = 200;
    const uint8_t time_low = 20;
    const uint8_t entry_cnt = 10;
    const uint8_t precision = 0;
    const uint8_t sig_count = 4;

    ESP_RETURN_ON_ERROR(qmi8658_enable(h, false, false), TAG, "off");
    ESP_RETURN_ON_ERROR(qmi8658_config_accel(h, QMI8658_ACC_2G, QMI8658_ODR_62_5HZ), TAG, "accel");
    const uint8_t page1[8] = {sample_cnt & 0xFF, sample_cnt >> 8, peak2peak & 0xFF, peak2peak >> 8,
                              peak & 0xFF,       peak >> 8,       0x02,             0x01};
    ESP_RETURN_ON_ERROR(write_cal(h, page1), TAG, "ped p1");
    ESP_RETURN_ON_ERROR(ctrl9(h, CMD_CONFIG_PEDO), TAG, "ped cfg 1");
    const uint8_t page2[8] = {time_up & 0xFF, time_up >> 8, time_low, entry_cnt, precision, sig_count, 0x02, 0x02};
    ESP_RETURN_ON_ERROR(write_cal(h, page2), TAG, "ped p2");
    ESP_RETURN_ON_ERROR(ctrl9(h, CMD_CONFIG_PEDO), TAG, "ped cfg 2");

    ESP_RETURN_ON_ERROR(update(h, REG_CTRL8, CTRL8_ACT_INT1 | CTRL8_PEDO, CTRL8_ACT_INT1 | CTRL8_PEDO), TAG, "ctrl8");
    ESP_RETURN_ON_ERROR(update(h, REG_CTRL1, CTRL1_INT1_EN, CTRL1_INT1_EN), TAG, "int1");
    return qmi8658_enable(h, true, false);
}

esp_err_t qmi8658_pedometer_read(qmi8658_handle_t h, uint32_t *steps)
{
    uint8_t b[3];
    ESP_RETURN_ON_ERROR(rd(h, REG_STEP_L, b, 3), TAG, "steps");
    *steps = b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16);
    return ESP_OK;
}

esp_err_t qmi8658_pedometer_reset(qmi8658_handle_t h)
{
    return ctrl9(h, CMD_RESET_PEDO);
}

esp_err_t qmi8658_engines_disable(qmi8658_handle_t h)
{
    return wr8(h, REG_CTRL8, CTRL8_HANDSHAKE);
}

esp_err_t qmi8658_read_irq(qmi8658_handle_t h, qmi8658_irq_t *irq)
{
    ESP_RETURN_ON_ERROR(rd8(h, REG_STATUSINT, &irq->status_int), TAG, "statusint");
    ESP_RETURN_ON_ERROR(rd8(h, REG_STATUS1, &irq->status1), TAG, "status1");
    return rd8(h, REG_FIFO_STATUS, &irq->fifo);
}

// --- Self-test -------------------------------------------------------------------

static esp_err_t selftest_run(qmi8658_handle_t h, uint8_t reg, uint8_t value, qmi8658_raw3_t *dv)
{
    ESP_RETURN_ON_ERROR(qmi8658_enable(h, false, false), TAG, "off");
    ESP_RETURN_ON_ERROR(wr8(h, reg, value), TAG, "st start");
    ESP_RETURN_ON_ERROR(wait_statusint(h, STATUSINT_AVAIL, true, SELFTEST_TIMEOUT_MS), TAG, "st done");
    ESP_RETURN_ON_ERROR(wr8(h, reg, value & 0x7F), TAG, "st clear");
    ESP_RETURN_ON_ERROR(wait_statusint(h, STATUSINT_AVAIL, false, SELFTEST_TIMEOUT_MS), TAG, "st ack");
    uint8_t b[6];
    ESP_RETURN_ON_ERROR(rd(h, REG_DVX_L, b, sizeof b), TAG, "dv");
    *dv = qmi8658_unpack3(b);
    return ESP_OK;
}

esp_err_t qmi8658_selftest_accel(qmi8658_handle_t h, bool *pass, int32_t mg[3])
{
    qmi8658_raw3_t dv;
    // aST (bit 7) with 1 kHz ODR, as the datasheet procedure requires.
    ESP_RETURN_ON_ERROR(selftest_run(h, REG_CTRL2, 0x80 | (h->acc_range << 4) | QMI8658_ODR_1000HZ, &dv), TAG,
                        "accel");
    *pass = qmi8658_acc_selftest_pass(&dv, mg);
    return ESP_OK;
}

esp_err_t qmi8658_selftest_gyro(qmi8658_handle_t h, bool *pass, int32_t dps[3])
{
    qmi8658_raw3_t dv;
    ESP_RETURN_ON_ERROR(selftest_run(h, REG_CTRL3, 0x80 | (h->gyr_range << 4) | QMI8658_ODR_1000HZ, &dv), TAG,
                        "gyro");
    *pass = qmi8658_gyr_selftest_pass(&dv, dps);
    return ESP_OK;
}

esp_err_t qmi8658_reg_read(qmi8658_handle_t h, uint8_t reg, uint8_t *data, size_t len)
{
    return rd(h, reg, data, len);
}

esp_err_t qmi8658_reg_write(qmi8658_handle_t h, uint8_t reg, uint8_t value)
{
    return wr8(h, reg, value);
}
