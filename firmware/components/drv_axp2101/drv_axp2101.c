#include "drv_axp2101.h"

#include <stdlib.h>

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "drv_axp2101";

#define REG_STATUS1       0x00
#define REG_STATUS2       0x01
#define REG_CHIP_ID       0x03
#define REG_COMMON_CFG    0x10
#define REG_GAUGE_WDT     0x18
#define REG_PWRON_SRC     0x20
#define REG_PWROFF_SRC    0x21
#define REG_PWROFF_EN     0x22
#define REG_PKEY_CFG      0x27
#define REG_ADC_EN        0x30
#define REG_ADC_VBAT_H    0x34
#define REG_ADC_VBUS_H    0x38
#define REG_ADC_VSYS_H    0x3A
#define REG_ADC_TDIE_H    0x3C
#define REG_INTEN1        0x40
#define REG_INTSTS1       0x48
#define REG_TS_CTRL       0x50
#define REG_IPRECHG       0x61
#define REG_ICC           0x62
#define REG_ITERM         0x63
#define REG_CV            0x64
#define REG_BAT_DET       0x68
#define REG_BAT_PCT       0xA4

#define CHIP_ID           0x4A
#define I2C_TIMEOUT_MS    50

struct axp2101_s {
    i2c_master_dev_handle_t dev;
};

static esp_err_t rd(axp2101_handle_t h, uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(h->dev, &reg, 1, data, len, I2C_TIMEOUT_MS);
}

static esp_err_t rd8(axp2101_handle_t h, uint8_t reg, uint8_t *v)
{
    return rd(h, reg, v, 1);
}

static esp_err_t wr8(axp2101_handle_t h, uint8_t reg, uint8_t v)
{
    const uint8_t buf[2] = {reg, v};
    return i2c_master_transmit(h->dev, buf, sizeof buf, I2C_TIMEOUT_MS);
}

static esp_err_t update(axp2101_handle_t h, uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t v;
    ESP_RETURN_ON_ERROR(rd8(h, reg, &v), TAG, "rd 0x%02X", reg);
    return wr8(h, reg, (uint8_t)((v & ~mask) | (value & mask)));
}

esp_err_t axp2101_new(i2c_master_bus_handle_t bus, uint8_t addr, uint32_t scl_hz, axp2101_handle_t *out)
{
    ESP_RETURN_ON_FALSE(bus && out, ESP_ERR_INVALID_ARG, TAG, "args");
    struct axp2101_s *h = calloc(1, sizeof *h);
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
    uint8_t id = 0;
    ESP_GOTO_ON_ERROR(rd8(h, REG_CHIP_ID, &id), fail, TAG, "not responding");
    ESP_GOTO_ON_FALSE(id == CHIP_ID, ESP_ERR_NOT_FOUND, fail, TAG, "chip id 0x%02X != 0x%02X", id, CHIP_ID);
    *out = h;
    return ESP_OK;
fail:
    i2c_master_bus_rm_device(h->dev);
    free(h);
    return ret;
}

esp_err_t axp2101_rail_get(axp2101_handle_t h, axp2101_rail_t rail, bool *on, uint16_t *mv)
{
    const axp2101_rail_info_t *ri = axp2101_rail_info(rail);
    ESP_RETURN_ON_FALSE(ri, ESP_ERR_INVALID_ARG, TAG, "rail");
    uint8_t en = 0;
    uint8_t vol = 0;
    ESP_RETURN_ON_ERROR(rd8(h, ri->en_reg, &en), TAG, "en");
    ESP_RETURN_ON_ERROR(rd8(h, ri->vol_reg, &vol), TAG, "vol");
    *on = (en >> ri->en_bit) & 1;
    *mv = axp2101_rail_code_to_mv(rail, vol & ri->vol_mask);
    return ESP_OK;
}

esp_err_t axp2101_rail_set(axp2101_handle_t h, axp2101_rail_t rail, bool on, uint16_t mv)
{
    const axp2101_rail_info_t *ri = axp2101_rail_info(rail);
    ESP_RETURN_ON_FALSE(ri, ESP_ERR_INVALID_ARG, TAG, "rail");
    ESP_RETURN_ON_FALSE(on || rail != AXP2101_DCDC1, ESP_ERR_NOT_ALLOWED, TAG, "DCDC1 powers the SoC");
    if (mv) {
        uint8_t code;
        ESP_RETURN_ON_FALSE(axp2101_rail_mv_to_code(rail, mv, &code), ESP_ERR_INVALID_ARG, TAG, "%u mV not valid for %s",
                            mv, ri->name);
        ESP_RETURN_ON_ERROR(update(h, ri->vol_reg, ri->vol_mask, code), TAG, "vol");
    }
    return update(h, ri->en_reg, 1u << ri->en_bit, on ? 1u << ri->en_bit : 0);
}

esp_err_t axp2101_charger_config(axp2101_handle_t h, const axp2101_charger_cfg_t *cfg)
{
    uint8_t cv;
    ESP_RETURN_ON_FALSE(axp2101_vterm_code(cfg->cv_mv, &cv), ESP_ERR_INVALID_ARG, TAG, "cv %u mV", cfg->cv_mv);
    ESP_RETURN_ON_ERROR(update(h, REG_IPRECHG, 0x0F, axp2101_i25_code(cfg->precharge_ma)), TAG, "iprechg");
    ESP_RETURN_ON_ERROR(update(h, REG_ICC, 0x1F, axp2101_ichg_code(cfg->cc_ma)), TAG, "icc");
    // Bit 4 enables charge termination at ITERM.
    ESP_RETURN_ON_ERROR(update(h, REG_ITERM, 0x1F, 0x10 | axp2101_i25_code(cfg->term_ma)), TAG, "iterm");
    return update(h, REG_CV, 0x07, cv);
}

esp_err_t axp2101_charger_get(axp2101_handle_t h, axp2101_charger_cfg_t *cfg)
{
    uint8_t pre, icc, iterm, cv;
    ESP_RETURN_ON_ERROR(rd8(h, REG_IPRECHG, &pre), TAG, "iprechg");
    ESP_RETURN_ON_ERROR(rd8(h, REG_ICC, &icc), TAG, "icc");
    ESP_RETURN_ON_ERROR(rd8(h, REG_ITERM, &iterm), TAG, "iterm");
    ESP_RETURN_ON_ERROR(rd8(h, REG_CV, &cv), TAG, "cv");
    cfg->precharge_ma = axp2101_i25_ma(pre & 0x0F);
    cfg->cc_ma = axp2101_ichg_ma(icc & 0x1F);
    cfg->term_ma = axp2101_i25_ma(iterm & 0x0F);
    cfg->cv_mv = axp2101_vterm_mv(cv & 0x07);
    return ESP_OK;
}

esp_err_t axp2101_adc_setup(axp2101_handle_t h, bool battery_ntc)
{
    if (!battery_ntc) {
        // No NTC: TS pin as external fixed input, else the charger sees a bogus temperature.
        ESP_RETURN_ON_ERROR(update(h, REG_TS_CTRL, 0x1F, 0x10), TAG, "ts");
    }
    // ADC enables: bit0 VBAT, bit1 TS, bit2 VBUS, bit3 VSYS, bit4 die temp.
    ESP_RETURN_ON_ERROR(update(h, REG_ADC_EN, 0x1F, 0x1D | (battery_ntc ? 0x02 : 0)), TAG, "adc");
    ESP_RETURN_ON_ERROR(update(h, REG_BAT_DET, 0x01, 0x01), TAG, "bat det");
    return update(h, REG_GAUGE_WDT, 0x08, 0x08); // fuel gauge on
}

esp_err_t axp2101_read_status(axp2101_handle_t h, axp2101_status_t *st)
{
    uint8_t s[2];
    ESP_RETURN_ON_ERROR(rd(h, REG_STATUS1, s, 2), TAG, "status");
    st->vbus_good = (s[0] >> 5) & 1;
    st->battery_present = (s[0] >> 3) & 1;
    const uint8_t dir = s[1] >> 5;
    st->charging = dir == 1;
    st->discharging = dir == 2;
    st->chg_state = (axp2101_chg_state_t)(s[1] & 0x07);
    if (st->chg_state > AXP2101_CHG_NOT_CHARGING) {
        st->chg_state = AXP2101_CHG_NOT_CHARGING;
    }

    uint8_t a[2];
    ESP_RETURN_ON_ERROR(rd(h, REG_ADC_VBAT_H, a, 2), TAG, "vbat");
    st->vbat_mv = st->battery_present ? axp2101_adc14(a[0], a[1], 5) : 0;
    ESP_RETURN_ON_ERROR(rd(h, REG_ADC_VBUS_H, a, 2), TAG, "vbus");
    st->vbus_mv = st->vbus_good ? axp2101_adc14(a[0], a[1], 6) : 0;
    ESP_RETURN_ON_ERROR(rd(h, REG_ADC_VSYS_H, a, 2), TAG, "vsys");
    st->vsys_mv = axp2101_adc14(a[0], a[1], 6);
    ESP_RETURN_ON_ERROR(rd(h, REG_ADC_TDIE_H, a, 2), TAG, "tdie");
    st->die_temp_dc = axp2101_die_temp_dc(axp2101_adc14(a[0], a[1], 6));

    uint8_t pct = 0;
    ESP_RETURN_ON_ERROR(rd8(h, REG_BAT_PCT, &pct), TAG, "pct");
    st->battery_pct = st->battery_present ? (int8_t)(pct > 100 ? 100 : pct) : -1;
    return ESP_OK;
}

esp_err_t axp2101_pkey_config(axp2101_handle_t h, axp2101_pkey_off_t off_time)
{
    // REG 0x27 bits 3:2 = OFFLEVEL time.
    ESP_RETURN_ON_ERROR(update(h, REG_PKEY_CFG, 0x0C, (uint8_t)(off_time << 2)), TAG, "pkey");
    // REG 0x22: bit1 = long press (> OFFLEVEL) powers off, bit0 = 0 -> off rather than restart.
    return update(h, REG_PWROFF_EN, 0x03, 0x02);
}

esp_err_t axp2101_pkey_get(axp2101_handle_t h, axp2101_pkey_off_t *off_time)
{
    uint8_t v;
    ESP_RETURN_ON_ERROR(rd8(h, REG_PKEY_CFG, &v), TAG, "pkey");
    *off_time = (axp2101_pkey_off_t)((v >> 2) & 0x03);
    return ESP_OK;
}

esp_err_t axp2101_irq_set_mask(axp2101_handle_t h, uint32_t mask)
{
    for (int i = 0; i < 3; i++) {
        ESP_RETURN_ON_ERROR(wr8(h, REG_INTEN1 + i, (uint8_t)(mask >> (8 * i))), TAG, "inten");
        ESP_RETURN_ON_ERROR(wr8(h, REG_INTSTS1 + i, 0xFF), TAG, "intsts");
    }
    return ESP_OK;
}

esp_err_t axp2101_irq_read_clear(axp2101_handle_t h, uint32_t *status)
{
    uint8_t s[3];
    ESP_RETURN_ON_ERROR(rd(h, REG_INTSTS1, s, 3), TAG, "intsts");
    *status = s[0] | ((uint32_t)s[1] << 8) | ((uint32_t)s[2] << 16);
    for (int i = 0; i < 3; i++) {
        if (s[i]) {
            ESP_RETURN_ON_ERROR(wr8(h, REG_INTSTS1 + i, s[i]), TAG, "clear"); // write-1-to-clear
        }
    }
    return ESP_OK;
}

esp_err_t axp2101_power_sources(axp2101_handle_t h, uint8_t *on_src, uint8_t *off_src)
{
    ESP_RETURN_ON_ERROR(rd8(h, REG_PWRON_SRC, on_src), TAG, "on src");
    return rd8(h, REG_PWROFF_SRC, off_src);
}

esp_err_t axp2101_shutdown(axp2101_handle_t h)
{
    return update(h, REG_COMMON_CFG, 0x01, 0x01);
}
