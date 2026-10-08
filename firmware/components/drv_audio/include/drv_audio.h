// Audio: ES8311 DAC (speaker via NS4150B amp) + ES7210 ADC (2 mics) on one
// full-duplex I2S port, through esp_codec_dev. Pin-agnostic singleton: the BSP passes
// pins and the shared I2C bus. Output and input share BCLK/LRCK, so while both are
// open they must use the same sample rate.
//
// Power: nothing runs until *_open(). *_close() stops the I2S channel, powers the
// codec down and (output) switches the speaker amp off. RAM: the I2S channels and their
// DMA buffers (~7.7 KB internal, 4 x 240 frames each way since P7-01) exist only while a
// direction is open (P9-01).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "audio_dsp.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    i2c_master_bus_handle_t bus;
    uint8_t es8311_addr; // 7-bit
    uint8_t es7210_addr; // 7-bit
    gpio_num_t mclk, bclk, ws, dout, din;
    gpio_num_t pa;       // speaker amp enable, active high
} drv_audio_config_t;

esp_err_t drv_audio_init(const drv_audio_config_t *cfg);

/** Interleaved 16-bit PCM. channels = 1 or 2. Volume persists across open/close. */
esp_err_t drv_audio_out_open(uint32_t rate, uint8_t channels);
esp_err_t drv_audio_out_write(const void *pcm, size_t bytes);
esp_err_t drv_audio_out_close(void);
esp_err_t drv_audio_set_volume(int percent);
esp_err_t drv_audio_set_mute(bool mute);

/** Mics: channels = 2 gives MIC1/MIC2 interleaved. gain_db applies to both. */
esp_err_t drv_audio_in_open(uint32_t rate, uint8_t channels, float gain_db);
esp_err_t drv_audio_in_read(void *pcm, size_t bytes);
esp_err_t drv_audio_in_close(void);

/**
 * Speaker -> mic loopback at 16 kHz: play a tone at `hz` for 1.2 s while recording
 * both mics, skip the first 200 ms, and run Goertzel tone detection on each mic
 * (>= 15 dB over off-tone probes, RMS >= 100). Opens and closes both directions.
 */
esp_err_t drv_audio_loopback(uint32_t hz, audio_dsp_detect_t result[2]);

#ifdef __cplusplus
}
#endif
