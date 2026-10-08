// AXP2101 register encoding (components/drv_axp2101/axp2101_codec.c).
#include <gtest/gtest.h>

#include "axp2101_codec.h"

TEST(Axp2101Rails, KnownVoltages)
{
    uint8_t code = 0;
    ASSERT_TRUE(axp2101_rail_mv_to_code(AXP2101_DCDC1, 3300, &code));
    EXPECT_EQ(code, 18);
    EXPECT_EQ(axp2101_rail_code_to_mv(AXP2101_DCDC1, 18), 3300);

    ASSERT_TRUE(axp2101_rail_mv_to_code(AXP2101_ALDO1, 3300, &code));
    EXPECT_EQ(code, 28);
    ASSERT_TRUE(axp2101_rail_mv_to_code(AXP2101_CPUSLDO, 1200, &code));
    EXPECT_EQ(code, 14);
}

TEST(Axp2101Rails, Dcdc3PiecewiseSegments)
{
    EXPECT_EQ(axp2101_rail_code_to_mv(AXP2101_DCDC3, 0), 500);
    EXPECT_EQ(axp2101_rail_code_to_mv(AXP2101_DCDC3, 70), 1200);
    EXPECT_EQ(axp2101_rail_code_to_mv(AXP2101_DCDC3, 71), 1220);
    EXPECT_EQ(axp2101_rail_code_to_mv(AXP2101_DCDC3, 87), 1540);
    EXPECT_EQ(axp2101_rail_code_to_mv(AXP2101_DCDC3, 88), 1600);
    EXPECT_EQ(axp2101_rail_code_to_mv(AXP2101_DCDC3, 106), 3400);
    EXPECT_EQ(axp2101_rail_code_to_mv(AXP2101_DCDC3, 107), 0); // reserved code
}

TEST(Axp2101Rails, RejectsUnrepresentableVoltages)
{
    uint8_t code = 0;
    EXPECT_FALSE(axp2101_rail_mv_to_code(AXP2101_DCDC1, 1400, &code)); // below range
    EXPECT_FALSE(axp2101_rail_mv_to_code(AXP2101_DCDC1, 3350, &code)); // off-step
    EXPECT_FALSE(axp2101_rail_mv_to_code(AXP2101_DCDC2, 1210, &code)); // gap between segments
    EXPECT_FALSE(axp2101_rail_mv_to_code(AXP2101_ALDO1, 3600, &code));
    EXPECT_FALSE(axp2101_rail_mv_to_code(AXP2101_RAIL_COUNT, 3300, &code));
}

TEST(Axp2101Rails, EveryValidCodeRoundTrips)
{
    for (int r = 0; r < AXP2101_RAIL_COUNT; r++) {
        const auto rail = static_cast<axp2101_rail_t>(r);
        const axp2101_rail_info_t *ri = axp2101_rail_info(rail);
        ASSERT_NE(ri, nullptr);
        for (int c = 0; c <= ri->vol_mask; c++) {
            const uint16_t mv = axp2101_rail_code_to_mv(rail, static_cast<uint8_t>(c));
            if (mv == 0) {
                continue;
            }
            uint8_t back = 0xFF;
            ASSERT_TRUE(axp2101_rail_mv_to_code(rail, mv, &back)) << ri->name << " " << mv;
            EXPECT_EQ(back, c) << ri->name;
        }
    }
}

TEST(Axp2101Charger, ConstantCurrentCodes)
{
    EXPECT_EQ(axp2101_ichg_code(200), 8); // 400 mAh cell: 0.5 C
    EXPECT_EQ(axp2101_ichg_ma(8), 200);
    EXPECT_EQ(axp2101_ichg_code(100), 4);
    EXPECT_EQ(axp2101_ichg_code(210), 8); // rounds down, never above request
    EXPECT_EQ(axp2101_ichg_code(300), 9);
    EXPECT_EQ(axp2101_ichg_code(1000), 16);
    EXPECT_EQ(axp2101_ichg_code(5000), 16);
    EXPECT_EQ(axp2101_ichg_ma(16), 1000);
    for (uint16_t ma = 0; ma <= 1000; ma += 5) {
        EXPECT_LE(axp2101_ichg_ma(axp2101_ichg_code(ma)), ma);
    }
}

TEST(Axp2101Charger, TerminationAndVoltage)
{
    EXPECT_EQ(axp2101_i25_code(25), 1);
    EXPECT_EQ(axp2101_i25_code(50), 2);
    EXPECT_EQ(axp2101_i25_code(60), 2);
    EXPECT_EQ(axp2101_i25_code(500), 8);
    EXPECT_EQ(axp2101_i25_ma(1), 25);
    uint8_t code = 0;
    ASSERT_TRUE(axp2101_vterm_code(4200, &code));
    EXPECT_EQ(code, 3);
    EXPECT_EQ(axp2101_vterm_mv(3), 4200);
    EXPECT_FALSE(axp2101_vterm_code(4250, &code));
    EXPECT_EQ(axp2101_vterm_mv(7), 0);
}

TEST(Axp2101Adc, CombinesAndMasksHighBits)
{
    EXPECT_EQ(axp2101_adc14(0xFF, 0x34, 5), 0x1F34);
    EXPECT_EQ(axp2101_adc14(0xFF, 0x34, 6), 0x3F34);
    EXPECT_EQ(axp2101_adc14(0x10, 0x6A, 6), 0x106A); // 4202 mV
}

TEST(Axp2101Adc, DieTemperature)
{
    EXPECT_EQ(axp2101_die_temp_dc(7274), 220); // 22.0 °C
    EXPECT_EQ(axp2101_die_temp_dc(7074), 320); // 32.0 °C
    EXPECT_EQ(axp2101_die_temp_dc(7474), 120); // 12.0 °C
}
