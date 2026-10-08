// QMI8658 conversions (components/drv_qmi8658/qmi8658_codec.c).
#include <gtest/gtest.h>

#include <vector>

#include "qmi8658_codec.h"

TEST(Qmi8658Scale, AccelFullScale)
{
    EXPECT_EQ(qmi8658_acc_mg(16384, QMI8658_ACC_2G), 1000); // 1 g at ±2 g
    EXPECT_EQ(qmi8658_acc_mg(8192, QMI8658_ACC_4G), 1000);
    EXPECT_EQ(qmi8658_acc_mg(-4096, QMI8658_ACC_8G), -1000);
    EXPECT_EQ(qmi8658_acc_mg(2048, QMI8658_ACC_16G), 1000);
    EXPECT_EQ(qmi8658_acc_mg(32767, QMI8658_ACC_16G), 15999);
}

TEST(Qmi8658Scale, GyroFullScale)
{
    EXPECT_EQ(qmi8658_gyr_mdps(32768 / 2, QMI8658_GYR_16DPS), 8000);
    EXPECT_EQ(qmi8658_gyr_mdps(-32768, QMI8658_GYR_2048DPS), -2048000);
    EXPECT_EQ(qmi8658_gyr_mdps(64, QMI8658_GYR_512DPS), 1000);
}

TEST(Qmi8658Data, UnpackLittleEndian)
{
    const uint8_t b[6] = {0x00, 0x40, 0xFF, 0xFF, 0x34, 0x12};
    const qmi8658_raw3_t r = qmi8658_unpack3(b);
    EXPECT_EQ(r.x, 0x4000);
    EXPECT_EQ(r.y, -1);
    EXPECT_EQ(r.z, 0x1234);
}

TEST(Qmi8658Data, Temperature)
{
    EXPECT_EQ(qmi8658_temp_cdeg(0x00, 25), 2500);
    EXPECT_EQ(qmi8658_temp_cdeg(0x80, 25), 2550);
    EXPECT_EQ(qmi8658_temp_cdeg(0x00, 0xFB), -500); // -5 °C
}

TEST(Qmi8658Fifo, ByteCountUsesTwoMsbBits)
{
    EXPECT_EQ(qmi8658_fifo_bytes(0x60, 0x00), 192u); // 16 frames x 12 B
    EXPECT_EQ(qmi8658_fifo_bytes(0x00, 0x03 | 0xF0), 1536u);
    EXPECT_EQ(qmi8658_fifo_bytes(0x10, 0x01), 2u * 0x110);
}

TEST(Qmi8658Fifo, ParsesSixDofFrames)
{
    std::vector<uint8_t> buf;
    for (int f = 0; f < 3; f++) {
        for (int i = 0; i < 6; i++) {
            buf.push_back(static_cast<uint8_t>(f * 10 + i)); // accel
            buf.push_back(0);
        }
    }
    buf.push_back(0xAA); // trailing partial frame is ignored
    qmi8658_raw3_t acc[4];
    qmi8658_raw3_t gyr[4];
    const size_t n = qmi8658_fifo_parse(buf.data(), buf.size(), true, true, acc, gyr, 4);
    ASSERT_EQ(n, 3u);
    EXPECT_EQ(acc[0].x, 0);
    EXPECT_EQ(acc[0].z, 2);
    EXPECT_EQ(gyr[0].x, 3);
    EXPECT_EQ(gyr[2].z, 25);
}

TEST(Qmi8658Fifo, AccelOnlyAndCap)
{
    const uint8_t buf[18] = {1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 7, 0, 8, 0, 9, 0};
    qmi8658_raw3_t acc[2];
    EXPECT_EQ(qmi8658_fifo_parse(buf, sizeof buf, true, false, acc, nullptr, 2), 2u);
    EXPECT_EQ(acc[1].x, 4);
    EXPECT_EQ(qmi8658_fifo_parse(buf, sizeof buf, false, false, acc, nullptr, 2), 0u);
}

TEST(Qmi8658Tap, FixedPointThresholds)
{
    EXPECT_EQ(qmi8658_g2_units(0.8f), 800);
    EXPECT_EQ(qmi8658_g2_units(0.4f), 400);
    EXPECT_EQ(qmi8658_u0_7(0.0625f), 8);
    EXPECT_EQ(qmi8658_u0_7(0.25f), 32);
}

TEST(Qmi8658SelfTest, Thresholds)
{
    int32_t out[3];
    const qmi8658_raw3_t acc_ok = {600, -600, 1000}; // ~293 mg, -293 mg, 488 mg
    EXPECT_TRUE(qmi8658_acc_selftest_pass(&acc_ok, out));
    EXPECT_EQ(out[2], 488);
    const qmi8658_raw3_t acc_bad = {600, 100, 1000};
    EXPECT_FALSE(qmi8658_acc_selftest_pass(&acc_bad, out));

    const qmi8658_raw3_t gyr_ok = {16 * 400, -16 * 350, 16 * 301};
    EXPECT_TRUE(qmi8658_gyr_selftest_pass(&gyr_ok, out));
    EXPECT_EQ(out[1], -350);
    const qmi8658_raw3_t gyr_bad = {16 * 400, 16 * 300, 16 * 400};
    EXPECT_FALSE(qmi8658_gyr_selftest_pass(&gyr_bad, out));
}

TEST(Qmi8658Odr, NominalRates)
{
    EXPECT_EQ(qmi8658_odr_mhz(QMI8658_ODR_62_5HZ), 62500u);
    EXPECT_EQ(qmi8658_odr_mhz(QMI8658_ODR_LP_21HZ), 21000u);
    EXPECT_EQ(qmi8658_odr_mhz(static_cast<qmi8658_odr_t>(9)), 0u);
}
