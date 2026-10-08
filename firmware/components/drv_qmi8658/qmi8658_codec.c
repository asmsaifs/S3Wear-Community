#include "qmi8658_codec.h"

#include <stdlib.h>

int32_t qmi8658_acc_mg(int16_t raw, qmi8658_acc_range_t range)
{
    const int32_t fs_g = 2 << (int)range; // 2, 4, 8, 16
    return (int32_t)(((int64_t)raw * fs_g * 1000) / 32768);
}

int32_t qmi8658_gyr_mdps(int16_t raw, qmi8658_gyr_range_t range)
{
    const int32_t fs_dps = 16 << (int)range; // 16 .. 2048
    return (int32_t)(((int64_t)raw * fs_dps * 1000) / 32768);
}

uint32_t qmi8658_odr_mhz(qmi8658_odr_t odr)
{
    switch (odr) {
    case QMI8658_ODR_1000HZ:
        return 1000000;
    case QMI8658_ODR_500HZ:
        return 500000;
    case QMI8658_ODR_250HZ:
        return 250000;
    case QMI8658_ODR_125HZ:
        return 125000;
    case QMI8658_ODR_62_5HZ:
        return 62500;
    case QMI8658_ODR_31_25HZ:
        return 31250;
    case QMI8658_ODR_LP_128HZ:
        return 128000;
    case QMI8658_ODR_LP_21HZ:
        return 21000;
    case QMI8658_ODR_LP_11HZ:
        return 11000;
    case QMI8658_ODR_LP_3HZ:
        return 3000;
    default:
        return 0;
    }
}

qmi8658_raw3_t qmi8658_unpack3(const uint8_t *p)
{
    const qmi8658_raw3_t r = {
        .x = (int16_t)(p[0] | (p[1] << 8)),
        .y = (int16_t)(p[2] | (p[3] << 8)),
        .z = (int16_t)(p[4] | (p[5] << 8)),
    };
    return r;
}

int16_t qmi8658_temp_cdeg(uint8_t lo, uint8_t hi)
{
    const int16_t raw = (int16_t)(lo | (hi << 8));
    return (int16_t)(((int32_t)raw * 100) / 256);
}

size_t qmi8658_fifo_bytes(uint8_t cnt_lsb, uint8_t status)
{
    return 2u * (((size_t)(status & 0x03) << 8) | cnt_lsb);
}

size_t qmi8658_fifo_parse(const uint8_t *data, size_t len, bool acc_on, bool gyr_on, qmi8658_raw3_t *acc,
                          qmi8658_raw3_t *gyr, size_t max_frames)
{
    const size_t frame = (acc_on ? 6u : 0u) + (gyr_on ? 6u : 0u);
    if (frame == 0) {
        return 0;
    }
    size_t n = len / frame;
    if (n > max_frames) {
        n = max_frames;
    }
    for (size_t i = 0; i < n; i++) {
        const uint8_t *p = data + i * frame;
        if (acc_on) {
            if (acc) {
                acc[i] = qmi8658_unpack3(p);
            }
            p += 6;
        }
        if (gyr_on && gyr) {
            gyr[i] = qmi8658_unpack3(p);
        }
    }
    return n;
}

uint16_t qmi8658_g2_units(float g_squared)
{
    const float u = g_squared * 1000.0f + 0.5f;
    return u >= 65535.0f ? 65535 : (uint16_t)u;
}

uint8_t qmi8658_u0_7(float ratio)
{
    const float u = ratio * 128.0f + 0.5f;
    return u >= 255.0f ? 255 : (uint8_t)u;
}

bool qmi8658_acc_selftest_pass(const qmi8658_raw3_t *dv, int32_t mg_out[3])
{
    const int16_t v[3] = {dv->x, dv->y, dv->z};
    bool pass = true;
    for (int i = 0; i < 3; i++) {
        mg_out[i] = (int32_t)v[i] * 1000 / 2048;
        pass &= abs(mg_out[i]) > 200;
    }
    return pass;
}

bool qmi8658_gyr_selftest_pass(const qmi8658_raw3_t *dv, int32_t dps_out[3])
{
    const int16_t v[3] = {dv->x, dv->y, dv->z};
    bool pass = true;
    for (int i = 0; i < 3; i++) {
        dps_out[i] = (int32_t)v[i] / 16;
        pass &= abs(dps_out[i]) > 300;
    }
    return pass;
}
