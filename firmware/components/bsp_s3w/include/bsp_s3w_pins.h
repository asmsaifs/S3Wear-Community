// Waveshare ESP32-S3-Touch-AMOLED-2.06 pin and I2C address map.
//
// This is the ONLY file in the firmware that names GPIO numbers or I2C addresses
// (CLAUDE.md "Firmware rules"). Source: docs/01-hardware.md §2/§3, cross-checked
// against the board schematic V1.0 and the Waveshare BSP/Arduino demos
// (docs/vendor/README.md).
#pragma once

#include "driver/gpio.h"

// --- Buttons ---------------------------------------------------------------
#define BSP_PIN_BTN_BOOT        GPIO_NUM_0   // strapping pin, active low
#define BSP_PIN_BTN_PWR_SYSOUT  GPIO_NUM_10  // AXP2101 PWR key mirror, high while pressed
#define BSP_BTN_BOOT_ACTIVE_LVL 0
#define BSP_BTN_PWR_ACTIVE_LVL  1

// --- Shared I2C bus (FT3168, AXP2101, QMI8658, PCF85063, ES8311, ES7210) ----
#define BSP_PIN_I2C_SCL         GPIO_NUM_14
#define BSP_PIN_I2C_SDA         GPIO_NUM_15
#define BSP_I2C_FREQ_HZ         400000

// 7-bit I2C addresses
#define BSP_I2C_ADDR_FT3168     0x38
#define BSP_I2C_ADDR_AXP2101    0x34
#define BSP_I2C_ADDR_QMI8658    0x6B  // SA0 high on this board (schematic)
#define BSP_I2C_ADDR_PCF85063   0x51
#define BSP_I2C_ADDR_ES8311     0x18
#define BSP_I2C_ADDR_ES7210     0x40

// --- AMOLED (CO5300, QSPI) -------------------------------------------------
#define BSP_PIN_LCD_SIO0        GPIO_NUM_4
#define BSP_PIN_LCD_SIO1        GPIO_NUM_5
#define BSP_PIN_LCD_SIO2        GPIO_NUM_6
#define BSP_PIN_LCD_SIO3        GPIO_NUM_7
#define BSP_PIN_LCD_RESET       GPIO_NUM_8
#define BSP_PIN_LCD_SCLK        GPIO_NUM_11
#define BSP_PIN_LCD_CS          GPIO_NUM_12
#define BSP_PIN_LCD_TE          GPIO_NUM_13
#define BSP_LCD_H_RES           410
#define BSP_LCD_V_RES           502
// Visible area offset inside CO5300 RAM. Copied from the Waveshare BSP
// (esp_lcd_panel_set_gap(panel, 0x16, 0)) and Arduino demo (col_offset1 = 22).
#define BSP_LCD_X_GAP           0x16
#define BSP_LCD_Y_GAP           0

// --- Touch (FT3168) --------------------------------------------------------
#define BSP_PIN_TP_RESET        GPIO_NUM_9
#define BSP_PIN_TP_INT          GPIO_NUM_38  // not RTC-IO: light-sleep wake only

// --- IMU / RTC interrupts --------------------------------------------------
#define BSP_PIN_IMU_INT1        GPIO_NUM_21  // RTC-IO: can wake deep sleep
#define BSP_PIN_RTC_INT         GPIO_NUM_39  // not RTC-IO: light-sleep wake only

// --- Audio (ES8311 out, ES7210 in, NS4150B amp) ----------------------------
#define BSP_PIN_I2S_MCLK        GPIO_NUM_16
#define BSP_PIN_I2S_BCLK        GPIO_NUM_41
#define BSP_PIN_I2S_WS          GPIO_NUM_45  // strapping pin (VDD_SPI); fine after boot
#define BSP_PIN_I2S_DOUT        GPIO_NUM_40  // to ES8311 DSDIN
#define BSP_PIN_I2S_DIN         GPIO_NUM_42  // from ES7210 ASDOUT
#define BSP_PIN_PA_CTRL         GPIO_NUM_46  // strapping pin; must stay low at reset

// --- TF card (SPI) ---------------------------------------------------------
#define BSP_PIN_SD_MOSI         GPIO_NUM_1
#define BSP_PIN_SD_SCK          GPIO_NUM_2
#define BSP_PIN_SD_MISO         GPIO_NUM_3
#define BSP_PIN_SD_CS           GPIO_NUM_17

// --- Optional add-ons ------------------------------------------------------
#define BSP_PIN_MOTOR           GPIO_NUM_18  // "MOTOR" net (Q1 low-side MOSFET), unpopulated motor pads P1/P2
