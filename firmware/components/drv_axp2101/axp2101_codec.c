#include "axp2101_codec.h"

#include <stddef.h>

// Piecewise-linear voltage segments: codes [code_lo, code_lo + n) map to
// mv_lo + i * step.
typedef struct {
    uint8_t code_lo;
    uint8_t n;
    uint16_t mv_lo;
    uint16_t step;
} seg_t;

typedef struct {
    axp2101_rail_info_t info;
    seg_t seg[3];
    uint8_t nseg;
} rail_t;

// clang-format off
static const rail_t k_rails[AXP2101_RAIL_COUNT] = {
    [AXP2101_DCDC1]   = {{"DCDC1",   0x80, 0, 0x82, 0x1F}, {{0, 20, 1500, 100}}, 1},
    [AXP2101_DCDC2]   = {{"DCDC2",   0x80, 1, 0x83, 0x7F}, {{0, 71, 500, 10}, {71, 17, 1220, 20}}, 2},
    [AXP2101_DCDC3]   = {{"DCDC3",   0x80, 2, 0x84, 0x7F}, {{0, 71, 500, 10}, {71, 17, 1220, 20}, {88, 19, 1600, 100}}, 3},
    [AXP2101_DCDC4]   = {{"DCDC4",   0x80, 3, 0x85, 0x7F}, {{0, 71, 500, 10}, {71, 32, 1220, 20}}, 2},
    [AXP2101_DCDC5]   = {{"DCDC5",   0x80, 4, 0x86, 0x1F}, {{0, 24, 1400, 100}}, 1},
    [AXP2101_ALDO1]   = {{"ALDO1",   0x90, 0, 0x92, 0x1F}, {{0, 31, 500, 100}}, 1},
    [AXP2101_ALDO2]   = {{"ALDO2",   0x90, 1, 0x93, 0x1F}, {{0, 31, 500, 100}}, 1},
    [AXP2101_ALDO3]   = {{"ALDO3",   0x90, 2, 0x94, 0x1F}, {{0, 31, 500, 100}}, 1},
    [AXP2101_ALDO4]   = {{"ALDO4",   0x90, 3, 0x95, 0x1F}, {{0, 31, 500, 100}}, 1},
    [AXP2101_BLDO1]   = {{"BLDO1",   0x90, 4, 0x96, 0x1F}, {{0, 31, 500, 100}}, 1},
    [AXP2101_BLDO2]   = {{"BLDO2",   0x90, 5, 0x97, 0x1F}, {{0, 31, 500, 100}}, 1},
    [AXP2101_CPUSLDO] = {{"CPUSLDO", 0x90, 6, 0x98, 0x1F}, {{0, 19, 500, 50}}, 1},
    [AXP2101_DLDO1]   = {{"DLDO1",   0x90, 7, 0x99, 0x1F}, {{0, 30, 500, 100}}, 1},
    [AXP2101_DLDO2]   = {{"DLDO2",   0x91, 0, 0x9A, 0x1F}, {{0, 30, 500, 100}}, 1},
};
// clang-format on

const axp2101_rail_info_t *axp2101_rail_info(axp2101_rail_t rail)
{
    return rail < AXP2101_RAIL_COUNT ? &k_rails[rail].info : NULL;
}

uint16_t axp2101_rail_code_to_mv(axp2101_rail_t rail, uint8_t code)
{
    if (rail >= AXP2101_RAIL_COUNT) {
        return 0;
    }
    const rail_t *r = &k_rails[rail];
    for (uint8_t i = 0; i < r->nseg; i++) {
        const seg_t *s = &r->seg[i];
        if (code >= s->code_lo && code < s->code_lo + s->n) {
            return (uint16_t)(s->mv_lo + (code - s->code_lo) * s->step);
        }
    }
    return 0;
}

bool axp2101_rail_mv_to_code(axp2101_rail_t rail, uint16_t mv, uint8_t *code)
{
    if (rail >= AXP2101_RAIL_COUNT) {
        return false;
    }
    const rail_t *r = &k_rails[rail];
    for (uint8_t i = 0; i < r->nseg; i++) {
        const seg_t *s = &r->seg[i];
        const uint16_t mv_hi = (uint16_t)(s->mv_lo + (s->n - 1) * s->step);
        if (mv >= s->mv_lo && mv <= mv_hi && (mv - s->mv_lo) % s->step == 0) {
            *code = (uint8_t)(s->code_lo + (mv - s->mv_lo) / s->step);
            return true;
        }
    }
    return false;
}

uint8_t axp2101_ichg_code(uint16_t ma)
{
    if (ma <= 200) {
        return (uint8_t)(ma / 25);
    }
    if (ma >= 1000) {
        return 16;
    }
    return (uint8_t)(8 + (ma - 200) / 100);
}

uint16_t axp2101_ichg_ma(uint8_t code)
{
    if (code <= 8) {
        return (uint16_t)(code * 25);
    }
    if (code > 16) {
        code = 16;
    }
    return (uint16_t)(200 + (code - 8) * 100);
}

uint8_t axp2101_i25_code(uint16_t ma)
{
    return (uint8_t)(ma >= 200 ? 8 : ma / 25);
}

uint16_t axp2101_i25_ma(uint8_t code)
{
    return (uint16_t)((code > 8 ? 8 : code) * 25);
}

static const uint16_t k_vterm_mv[] = {0, 4000, 4100, 4200, 4350, 4400};

bool axp2101_vterm_code(uint16_t mv, uint8_t *code)
{
    for (uint8_t i = 1; i < sizeof k_vterm_mv / sizeof k_vterm_mv[0]; i++) {
        if (k_vterm_mv[i] == mv) {
            *code = i;
            return true;
        }
    }
    return false;
}

uint16_t axp2101_vterm_mv(uint8_t code)
{
    return code < sizeof k_vterm_mv / sizeof k_vterm_mv[0] ? k_vterm_mv[code] : 0;
}

uint16_t axp2101_adc14(uint8_t hi, uint8_t lo, uint8_t hi_bits)
{
    return (uint16_t)(((hi & ((1u << hi_bits) - 1)) << 8) | lo);
}

int16_t axp2101_die_temp_dc(uint16_t raw)
{
    return (int16_t)(220 + ((int32_t)7274 - (int32_t)raw) / 2);
}
