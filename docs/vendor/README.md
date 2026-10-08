# Vendor references

Vendor files are **not** committed (size and licence). Fetch them when you need to re-check a value:

| What | Where | Licence |
|---|---|---|
| Schematic `ESP32-S3-Touch-AMOLED-2.06-Schematic-V1.0.pdf` | `Schematic/` in https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.06 | Waveshare |
| Arduino demos + libs (Arduino_GFX `Arduino_CO5300`, Arduino_DriveBus `FT3x68`, SensorLib `QMI8658`/`PCF85063`, XPowersLib `AXP2101`) | `examples/arduino/` in the same repo | MIT / BSD (per library) |
| ESP-IDF BSP `waveshare/esp32_s3_touch_amoled_2_06` v2.0.0 | https://components.espressif.com/components/waveshare/esp32_s3_touch_amoled_2_06 | Apache-2.0 |

## Values taken from vendor sources (do not change without re-checking)

| Value | Used in | Source |
|---|---|---|
| CO5300 init sequence (`0x11` +120 ms, `0xC4 0x80`, `0x44 0x01 0xD1`, `0x35 0x00`, `0x53 0x20`, `0x63 0xFF`, `0x51 0x00`, `0x29`) | `drv_co5300` | BSP `lcd_init_cmds[]` |
| Panel gap x = 0x16 (22), y = 0 | `bsp_s3w_pins.h` | BSP `esp_lcd_panel_set_gap(panel, 0x16, 0)`; Arduino `col_offset1 = 22` |
| QSPI opcodes: params `0x02`, pixels `0x32` (quad), cmd in bits 15:8 | `drv_co5300` | BSP brightness write `0x02 << 24 \| 0x51 << 8` |
| Draw-area rounding: even x1/y1, odd x2/y2 | `ui_framework/lvgl_port` | BSP `rounder_event_cb` |
| FT3168 is register-compatible with FT5x06 for touch reads; power mode reg `0xA5` (0 active, 1 monitor, 2 standby, 3 hibernate) | `drv_ft3168` | BSP uses `esp_lcd_touch_ft5x06`; Arduino_DriveBus `Arduino_FT3x68` |
| ES8311 `pa_pin` GPIO46 active high, `use_mclk = true`, ES7210 at 0x40 | `drv_audio` | BSP `bsp_audio_codec_*_init` |
| AXP2101: TS pin must be disabled (no NTC on this board) or charging misbehaves | `drv_axp2101` | Vendor `port_axp2101.cpp` |

## Power rails (schematic page 1, AXP2101 table)

| Rail | Net | Voltage | Feeds |
|---|---|---|---|
| DCDC1 | VCC3V3 | 3.3 V | ESP32-S3, flash/PSRAM, AMOLED logic, FT3168, QMI8658, TF card, ES8311/ES7210 digital |
| ALDO1 | A3V3 | 3.3 V | Audio analog (codec AVDD, mic bias) |
| RTCLDO | VCC-RTC | — | PCF85063 (keeps time while the PMU is on battery) |
| DCDC2–4, ALDO2–4, BLDO1–2, CPUSLDO, DLDO1–2 | (labels only) | — | Not used by any part on this board |
