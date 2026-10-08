# 01 — Hardware

Board: **Waveshare ESP32-S3-Touch-AMOLED-2.06** (sold as a watch kit with case and 400 mAh battery).
Sources: Waveshare wiki and docs, board schematic V1.0 and vendor demo code — see [vendor/README.md](vendor/README.md) for where to get them and the values taken from them. Rows confirmed against the schematic in P1-01 are marked ✔; corrections are marked **(corrected P1-xx)**.

## 1. Inventory and what we use it for

| Component | Part | Bus | Used for |
|---|---|---|---|
| SoC | ESP32-S3R8, dual LX7 @ 240 MHz, 512 KB SRAM | — | Everything; core 0 = radio/services, core 1 = UI/apps |
| PSRAM | 8 MB octal (in package) | OPI | LVGL heap, app heaps, frame buffers, audio buffers |
| Flash | 32 MB external | QSPI | Firmware A/B, LittleFS, coredump |
| Display | 2.06" AMOLED 410×502, CO5300 driver, 600 nits | QSPI (4-line) | UI, watch faces, AOD (low brightness, sparse pixels) |
| Touch | FT3168 capacitive | I2C + INT | Taps, swipes, long press, palm-cover gesture, tap-to-wake |
| IMU | QMI8658 6-axis accel+gyro | I2C + INT1 | Steps, raise-to-wake, sleep, tap, gestures, workouts, games (tilt), level tool |
| RTC | PCF85063 | I2C + INT | Timekeeping across resets/deep sleep, alarm interrupt |
| PMU | AXP2101 | I2C | Battery %, charging, voltage rails, power key, die temperature, shutdown |
| Audio out | ES8311 codec + NS4150B class-D amp + speaker | I2C + I2S | Alarms, notification sounds, find watch, games, voice replies, local playback |
| Audio in | ES7210 ADC + dual microphones | I2C + I2S | Voice memos, wake word, assistant, sound meter; AEC/beamforming via ESP-SR |
| Storage | TF (microSD) slot | SPI | Voice memos, apps cache, music, offline maps, logs, wallpapers, export |
| Radio | Wi-Fi 4 2.4 GHz, BLE 5 (incl. 2M PHY, long range) | — | Companion link, OTA, store, weather, Home Assistant |
| USB | Native USB-C (USB-Serial-JTAG / TinyUSB OTG) | — | Flash, console, mass storage (SD), charging |
| Buttons | PWR (via AXP2101 + GPIO10 SYS_OUT), BOOT (GPIO0) | — | Wake/sleep, back, app shortcuts, SOS triple-press, power off |
| Battery | 3.7 V Li-Po, MX1.25, 400 mAh recommended | — | — |

Not present: heart-rate sensor, GNSS, magnetometer, barometer, NFC, ambient light sensor. See PLAN.md "Out of scope" for mitigations.
**(corrected P1-01)** The schematic has a "Motor" block: GPIO18 drives a low-side MOSFET to the MOTOR pads (P1/P2 on the back). Check whether your unit has a motor fitted; if not, a coin vibration motor can be soldered to the pads without wiring changes (feature flag `HAPTICS_EXT`).

## 2. GPIO map

| GPIO | Signal | Device | Notes |
|---|---|---|---|
| 0 | BOOT | Button | Strapping pin. Active low. Use as secondary button after boot |
| 1 | SD_MOSI | TF card | SPI |
| 2 | SD_SCK | TF card | SPI |
| 3 | SD_MISO | TF card | SPI |
| 4–7 | QSPI SIO0–3 | CO5300 | |
| 8 | LCD_RESET | CO5300 | |
| 9 | TP_RESET | FT3168 | |
| 10 | SYS_OUT | PWR key | High while pressed (AXP2101 also reports short/long press via IRQ status). **The AXP2101 IRQ pin is not routed to any ESP32 GPIO** (schematic net AXP_IRQ → EXIO5 only), so PMU IRQ status is read on SYS_OUT edges and by slow polling |
| 11 | QSPI_SCL | CO5300 | |
| 12 | LCD_CS | CO5300 | |
| 13 | LCD_TE | CO5300 | Tearing effect: sync flush to avoid tearing |
| 14 | I2C SCL | shared | FT3168, AXP2101, QMI8658, PCF85063, ES8311, ES7210 |
| 15 | I2C SDA | shared | |
| 16 | I2S_MCLK | ES8311/ES7210 | |
| 17 | SD_CS | TF card | ✔ schematic net SDCS → card is wired for **SPI** (the Waveshare IDF BSP uses SDMMC 1-bit on 1/2/3; we use SDSPI) |
| 18 | MOTOR | Q1 MOSFET | **(corrected P1-01)** vibration motor driver (motor may be unpopulated) |
| 19/20 | USB D-/D+ | native USB | |
| 21 | QMI_INT1 | QMI8658 | RTC-IO → can wake from **deep** sleep (ext0/ext1). Only INT1 is wired, so FIFO watermark, tap/pedometer (activity) and wake-on-motion all route to INT1; read STATUSINT/STATUS1/FIFO_STATUS to tell them apart. WoM toggles the level, so the GPIO uses any-edge |
| 38 | TP_INT | FT3168 | Not RTC-IO → light-sleep wake only |
| 39 | RTC_INT | PCF85063 | Not RTC-IO → light-sleep wake only |
| 40 | I2S_DSDIN | ES8311 | Playback data |
| 41 | I2S_SCLK | ES8311/ES7210 | |
| 42 | I2S_ASDOUT | ES7210 | Capture data |
| 43/44 | U0TXD/U0RXD | pads | Free if console is on USB-Serial-JTAG → use for add-ons |
| 45 | I2S_LRCK | audio | Strapping pin (VDD_SPI); fine after boot |
| 46 | PA_CTRL | NS4150B | Strapping pin; keep low at boot; high = speaker amp on |

## 3. I2C device map (verify with `i2c scan` console command, task P1-01)

| Device | 7-bit address | Status |
|---|---|---|
| FT3168 | 0x38 | ✔ vendor BSP/demo |
| AXP2101 | 0x34 | ✔ vendor demo |
| QMI8658 | 0x6B | ✔ schematic note "0X6B" (SA0 high) |
| PCF85063 | 0x51 | ✔ vendor demo |
| ES8311 | 0x18 | ✔ vendor BSP (`ES8311_CODEC_DEFAULT_ADDR`) |
| ES7210 | 0x40 | ✔ vendor BSP (`ES7210_CODEC_DEFAULT_ADDR`, 0x80 as 8-bit) |

Hardware check (🔧): `i2c scan` must list all six and print `6/6 expected on-board devices present`.
✔ 2026-10-02, dev board (BT MAC 80:b5:4e:da:6f:a6): all six present. Each scan also showed one spurious ACK at a random address (0x65, then 0x5F) — not a device; ignore unknown addresses in `i2c scan`.

Bus speed: 400 kHz. One bus, one mutex inside the IDF driver. Touch reads are on the hot path during interaction (60 Hz); IMU FIFO reads are batched to stay off the bus during scrolling.

### IMU notes (P1-06, hardware)
- QMI8658 WHO_AM_I 0x05, revision 0x7C. Self-test, FIFO watermark, tap and wake-on-motion all work on INT1.
- ✔ 2026-10-02: the built-in **pedometer does not work on this board**. Config reads back exactly as written (CTRL2 0x07, CTRL7 0x21, CTRL8 0xD0, CAL page 2 as written) but STEP_CNT stays 0 after ~40 real steps on battery (no reboot in between), and INT1 pulses every ~2.2 s with empty STATUS1. The part is marked QMI8658**C** on the schematic; the pedometer engine appears to be a QMI8658A feature. **Decision: P5-01 uses the software step detector on accelerometer FIFO data; do not rely on the hardware step counter.** Tap and wake-on-motion do work.
- The temperature register only updates while a sensor runs.

## 4. Power tree (from schematic V1.0, P1-05)
AXP2101 provides DCDC1 (3.3 V system) and several LDOs. The schematic's rail table and nets show only two rails in use; everything else on the board hangs off VCC3V3:

| Rail | Net | Voltage | Feeds | Can switch off when |
|---|---|---|---|---|
| DCDC1 | VCC3V3 | 3.3 V | ESP32-S3, flash/PSRAM, AMOLED (panel module makes its own high voltages), FT3168, QMI8658, TF card, codec digital supplies | never (driver refuses) |
| ALDO1 | A3V3 | 3.3 V | ES8311/ES7210 analog supplies, mic bias | no audio for 5 s (P3-11 / P7: verify codec re-init cost) |
| RTCLDO | VCC-RTC | — | PCF85063 | never (always on while the PMU has power) |
| DCDC2–5, ALDO2–4, BLDO1–2, CPUSLDO, DLDO1–2 | labels only | — | nothing on this board | candidates to switch off in P10 power tuning — verify with `pmu rail <name> off` first |

✔ Measured on the dev board (`pmu`, 2026-10-02): the PMU OTP leaves almost everything **on** — DCDC1 3300, DCDC2 900, DCDC3 1200, DCDC4 1800 mV, DCDC5 off, ALDO1 3300, ALDO2 3300, ALDO3 3000, ALDO4 1800, BLDO1 1200, BLDO2 2800, CPUSLDO 1200, DLDO1/2 500 mV. Switching the unused ones off is a free power win (P10).

The AMOLED, touch and SD card are **not** separately switchable: use panel sleep (0x10), touch monitor/hibernate, and SD unmount instead.
🔧 Verify on hardware: `pmu` lists DCDC1 ON 3300 mV and ALDO1 ON 3300 mV; `pmu rail ALDO1 off` must silence audio and nothing else.

The AXP2101 IRQ output is not connected to the SoC. `bsp_s3w` reads the IRQ status registers on every PWR-key (GPIO10) edge and every 2 s, and posts `BSP_PMU_EVENT` (VBUS in/out, battery in/out, charge start/done, PWR short/long, low battery, over-temperature) on the default event loop.

Charger settings for a 400 mAh cell: constant current ≤ 200 mA (0.5C), termination 25 mA, precharge 50 mA, charge voltage 4.2 V (set by `bsp_pmu_start()`). The kit battery has no NTC, so the TS input is disabled (vendor note: leaving it enabled breaks charging). Low-battery warning at 15 %, shutdown at 3.3 V (AXP2101 fuel gauge handles %) — wired up in P3-09. Holding PWR for 6 s is a hardware power-off (REG 0x27/0x22).

## 5. Display facts
- 410×502, RGB565 → 411,640 bytes per full frame.
- QSPI at 40 MHz (try 80 MHz in P1; keep 40 if artifacts) → full frame ≈ 21 ms at 40 MHz. Use partial rendering; full-screen animations limited to ~30–45 fps.
  Clock is `CONFIG_S3W_LCD_PCLK_MHZ` (menuconfig → S3Wear board, default 40). ✔ P1-02 on hardware: `lcd fps` = 29.8 fps at 40 MHz and 29.9 fps at 80 MHz. Full-screen frames are locked to every second TE pulse (60 Hz panel) and limited by render + byte swap, not the bus, so 80 MHz buys nothing yet; stay at 40 MHz. P2-04 revisits (direct mode, render cost).
  ✔ P2-04 on hardware (`lcd bench scroll|swipe`, full-screen list scroll and page swipe, every frame redraws all 205,820 px): **29.9 fps** for both, locked to every second TE pulse, in dev and release builds. Per frame ≈ 23 ms render + 4 ms byte swap + 2 ms DMA wait at 40 MHz; 60 fps would need < 16.7 ms. Decision: partial rendering into 2 × 40-line internal DMA buffers, QSPI 40 MHz, 32 KB instruction cache, LVGL always compiled -O2. See [02 §6](02-firmware-architecture.md#6-memory-budget) for the full comparison.
- TE: the vendor init enables TE (0x35 0x00) with scan line 465 (0x44 0x01D1). The LVGL port waits for a TE rising edge (GPIO13) before the first chunk of every frame; the TE interrupt is armed only while waiting.
- **Alignment**: CO5300 requires partial updates with even x1/y1 and odd x2/y2 (2-pixel granularity). Implement the LVGL invalidate-area rounder.
- **Offset**: the 410-wide visible area sits inside a larger driver RAM. Copy the column/row gap from the Waveshare demo (`esp_lcd_panel_set_gap`).
  ✔ P1-02: gap x = 0x16 (22), y = 0 (`BSP_LCD_X_GAP/Y_GAP`). The vendor init sequence also sets CASET 0x16..0x1AF / RASET 0..0x1F5.
- Brightness: DCS command `0x51` (0–255). AOD uses 5–15 % brightness and < 10 % lit pixels.
- Power: Sleep In `0x10` / Display Off `0x28` when screen off; Sleep Out `0x11` needs 120 ms before Display On — budget this in wake latency (target < 150 ms raise-to-visible).
- Burn-in: AOD shifts content by ±4 px every minute (P3-05: 2 px steps, docs/04 §4a); avoid static white elements.
- Black is free on AMOLED: dark UI is the default and the only theme in v1.

## 6. Board-specific gotchas
1. Strapping pins 0/45/46: PA_CTRL (46) must be low during reset; do not add pull-ups.
2. Touch and RTC interrupts cannot wake deep sleep. Design: the watch uses **light sleep** as its normal idle state (BLE stays connected). Deep sleep only in "watch-only power saver" mode, woken by IMU INT1 (GPIO21), PWR key (via AXP2101 IRQ if routed to an RTC-IO — check schematic) or ESP timer.
3. FT3168 has its own low-power "monitor" mode; enter it when the screen is off so tap-to-wake works at ~µA level. Power mode register 0xA5: 0 active, 1 monitor, 2 standby, 3 hibernate (hibernate needs TP_RESET to wake). Console `touch monitor` exercises screen-off → tap → screen-on (P1-03). ✔ On hardware: the controller needs ~150 ms after reset before registers are valid, and by default it drops into monitor mode after 2 s idle and then NACKs I2C until touched; `drv_ft3168` waits after reset and clears ID_G_CTRL (0x86). ✔ P2-06: even so it stops answering I2C after ~25 s without a touch, with the screen on or off, until the next touch (this is the FT3168 NACK seen in `factory test` and `touch info` after idle). INT and touch reads work throughout, so touch is unaffected; `bsp_touch_set_low_power()` probes the address first and skips the mode write when there is no ACK.
4. ES8311/ES7210 share MCLK/BCLK/LRCK → configure one I2S peripheral in full-duplex (TX + RX) mode with the same sample rate (16 kHz for voice, 44.1/48 kHz for playback requires reconfig). Implemented in `drv_audio` (P1-07): I2S0 std Philips 16-bit stereo, MCLK = 256 × fs; esp_codec_dev enables the channels and the PA on open and powers the codecs down on close. Console `audio loop` plays a 1 kHz tone and checks both mics with a Goertzel detector.
5. USB console: use USB-Serial-JTAG (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG`) so UART0 pads are free. Light sleep disables USB-Serial-JTAG. ✔ P2-06: `CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION` blocks automatic light sleep while a USB host is connected (it watches SOF frames, not VBUS), so the console works at the desk and a plain charger does not stop sleep.
6. Watch case: buttons and USB position are fixed; the TF card may be inaccessible when assembled — features must degrade gracefully with no SD card. The card has no card-detect pin: `svc_storage` mounts it in the background at boot (SPI3, never formats), and `svc_storage_sd_check()` unmounts and posts `SVC_STORAGE_EVT_SD_REMOVED` once the card stops answering.
