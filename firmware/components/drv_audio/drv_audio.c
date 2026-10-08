#include "drv_audio.h"

#include "driver/i2s_std.h"
#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"

static const char *TAG = "drv_audio";

#define I2S_PORT         I2S_NUM_0
#define DEFAULT_RATE     16000
#define DEFAULT_VOLUME   70

// The I2S channels (and their DMA buffers, ~7.7 KB of internal RAM: 4 x 240 frames each way)
// exist only while a direction is open: created by the first open, deleted when both are closed.
// esp_codec_dev keeps its data interface; it is re-bound to the new channels on every create.
static struct {
    drv_audio_config_t cfg;
    const audio_codec_data_if_t *data_if;
    i2s_chan_handle_t tx; // NULL while both directions are closed
    i2s_chan_handle_t rx;
    esp_codec_dev_handle_t out;
    esp_codec_dev_handle_t in;
    bool out_open;
    bool in_open;
    int volume;
} s_audio;

static esp_err_t i2s_init(const drv_audio_config_t *cfg)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true; // underrun plays silence, not stale data
    // 4 x 240 frames (60 ms each way) instead of IDF's 6: internal DMA RAM is short with BLE, the
    // UI and svc_memo running (P7-01: opens failed with ~15 KB free). 60 ms still covers a flash
    // sector erase (cache off, ~45 ms) while a voice memo records to /flash.
    chan_cfg.dma_desc_num = 4;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_audio.tx, &s_audio.rx), TAG, "i2s channels");

    // Both directions share MCLK/BCLK/WS. esp_codec_dev reconfigures rate/slots on open
    // and enables/disables the channels itself.
    const i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(DEFAULT_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = cfg->mclk,
            .bclk = cfg->bclk,
            .ws = cfg->ws,
            .dout = cfg->dout,
            .din = cfg->din,
        },
    };
    esp_err_t err = i2s_channel_init_std_mode(s_audio.tx, &std_cfg);
    if (err == ESP_OK) {
        err = i2s_channel_init_std_mode(s_audio.rx, &std_cfg);
    }
    if (err != ESP_OK) {
        i2s_del_channel(s_audio.tx);
        i2s_del_channel(s_audio.rx);
        s_audio.tx = s_audio.rx = NULL;
        ESP_LOGE(TAG, "i2s std mode: %s", esp_err_to_name(err));
    }
    return err;
}

// Before an open: the channels, bound to esp_codec_dev's data interface.
static esp_err_t chans_up(void)
{
    if (s_audio.tx) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(i2s_init(&s_audio.cfg), TAG, "i2s");
    audio_codec_i2s_cfg_t c = {.port = I2S_PORT, .rx_handle = s_audio.rx, .tx_handle = s_audio.tx};
    if (s_audio.data_if->open(s_audio.data_if, &c, sizeof c) != ESP_CODEC_DEV_OK) {
        i2s_del_channel(s_audio.tx);
        i2s_del_channel(s_audio.rx);
        s_audio.tx = s_audio.rx = NULL;
        ESP_LOGE(TAG, "i2s data if");
        return ESP_FAIL;
    }
    return ESP_OK;
}

// After a close: both directions closed (esp_codec_dev disabled the channels) -> delete them.
static void chans_down(void)
{
    if (!s_audio.tx || s_audio.out_open || s_audio.in_open) {
        return;
    }
    s_audio.data_if->close(s_audio.data_if);
    // A mic-only open leaves the TX channel enabled after its close (esp_codec_dev runs both
    // directions of the full-duplex port), and an enabled channel cannot be deleted: the next open
    // would then find no free I2S controller. Disable both first; one that is not enabled only
    // logs an error from i2s_common, hidden like in codec_open().
    const esp_log_level_t level = esp_log_level_get("i2s_common");
    esp_log_level_set("i2s_common", ESP_LOG_NONE);
    i2s_channel_disable(s_audio.tx);
    i2s_channel_disable(s_audio.rx);
    esp_log_level_set("i2s_common", level);
    i2s_del_channel(s_audio.tx);
    i2s_del_channel(s_audio.rx);
    s_audio.tx = s_audio.rx = NULL;
}

static const audio_codec_ctrl_if_t *i2c_ctrl(i2c_master_bus_handle_t bus, uint8_t addr_7bit)
{
    audio_codec_i2c_cfg_t c = {
        .port = I2C_NUM_0,
        .addr = (uint8_t)(addr_7bit << 1), // esp_codec_dev takes 8-bit addresses
        .bus_handle = bus,
    };
    return audio_codec_new_i2c_ctrl(&c);
}

esp_err_t drv_audio_init(const drv_audio_config_t *cfg)
{
    ESP_RETURN_ON_FALSE(cfg && cfg->bus, ESP_ERR_INVALID_ARG, TAG, "args");
    ESP_RETURN_ON_FALSE(!s_audio.out, ESP_ERR_INVALID_STATE, TAG, "already init");
    s_audio.cfg = *cfg;
    ESP_RETURN_ON_ERROR(i2s_init(cfg), TAG, "i2s");

    audio_codec_i2s_cfg_t i2s_cfg = {.port = I2S_PORT, .rx_handle = s_audio.rx, .tx_handle = s_audio.tx};
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    ESP_RETURN_ON_FALSE(data_if, ESP_FAIL, TAG, "i2s data if");
    s_audio.data_if = data_if;

    // ES8311 DAC + speaker amp. Hardware gain values from the Waveshare BSP.
    const audio_codec_ctrl_if_t *out_ctrl = i2c_ctrl(cfg->bus, cfg->es8311_addr);
    ESP_RETURN_ON_FALSE(out_ctrl, ESP_FAIL, TAG, "es8311 ctrl");
    es8311_codec_cfg_t es8311 = {
        .ctrl_if = out_ctrl,
        .gpio_if = audio_codec_new_gpio(),
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = cfg->pa,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .hw_gain = {.pa_voltage = 5.0, .codec_dac_voltage = 3.3},
    };
    const audio_codec_if_t *es8311_if = es8311_codec_new(&es8311);
    ESP_RETURN_ON_FALSE(es8311_if, ESP_FAIL, TAG, "es8311");
    esp_codec_dev_cfg_t out_cfg = {.dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = es8311_if, .data_if = data_if};
    s_audio.out = esp_codec_dev_new(&out_cfg);
    ESP_RETURN_ON_FALSE(s_audio.out, ESP_FAIL, TAG, "out dev");

    // ES7210 ADC, MIC1 + MIC2.
    const audio_codec_ctrl_if_t *in_ctrl = i2c_ctrl(cfg->bus, cfg->es7210_addr);
    ESP_RETURN_ON_FALSE(in_ctrl, ESP_FAIL, TAG, "es7210 ctrl");
    es7210_codec_cfg_t es7210 = {
        .ctrl_if = in_ctrl,
        .master_mode = false,
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2,
    };
    const audio_codec_if_t *es7210_if = es7210_codec_new(&es7210);
    ESP_RETURN_ON_FALSE(es7210_if, ESP_FAIL, TAG, "es7210");
    esp_codec_dev_cfg_t in_cfg = {.dev_type = ESP_CODEC_DEV_TYPE_IN, .codec_if = es7210_if, .data_if = data_if};
    s_audio.in = esp_codec_dev_new(&in_cfg);
    ESP_RETURN_ON_FALSE(s_audio.in, ESP_FAIL, TAG, "in dev");

    // Power the codecs down whenever a direction is closed.
    esp_codec_set_disable_when_closed(s_audio.out, true);
    esp_codec_set_disable_when_closed(s_audio.in, true);
    s_audio.volume = DEFAULT_VOLUME;
    chans_down(); // nothing open yet: no DMA buffers until the first sound
    return ESP_OK;
}

static esp_err_t codec_err(int ret, const char *what)
{
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "%s failed (%d)", what, ret);
        return ESP_FAIL;
    }
    return ESP_OK;
}

// esp_codec_dev (1.6.2, audio_codec_data_i2s.c _i2s_data_set_fmt) stops the I2S channel
// before reconfiguring it on every open, also when it is not running (always, after our
// close). IDF then logs "i2s_channel_disable(...): the channel has not been enabled yet";
// esp_codec_dev ignores the result, so it is noise. Hide the i2s_common log during the open
// only: drv_audio is the only I2S user, and a real failure still comes back as an error.
static int codec_open(esp_codec_dev_handle_t dev, esp_codec_dev_sample_info_t *fs)
{
    const esp_log_level_t level = esp_log_level_get("i2s_common");
    esp_log_level_set("i2s_common", ESP_LOG_NONE);
    const int ret = esp_codec_dev_open(dev, fs);
    esp_log_level_set("i2s_common", level);
    return ret;
}

esp_err_t drv_audio_out_open(uint32_t rate, uint8_t channels)
{
    ESP_RETURN_ON_FALSE(s_audio.out && !s_audio.out_open, ESP_ERR_INVALID_STATE, TAG, "out state");
    ESP_RETURN_ON_ERROR(chans_up(), TAG, "channels");
    esp_codec_dev_sample_info_t fs = {.bits_per_sample = 16, .channel = channels, .sample_rate = rate};
    const esp_err_t err = codec_err(codec_open(s_audio.out, &fs), "out open");
    if (err != ESP_OK) {
        chans_down();
        return err;
    }
    s_audio.out_open = true;
    return codec_err(esp_codec_dev_set_out_vol(s_audio.out, s_audio.volume), "volume");
}

esp_err_t drv_audio_out_write(const void *pcm, size_t bytes)
{
    ESP_RETURN_ON_FALSE(s_audio.out_open, ESP_ERR_INVALID_STATE, TAG, "out closed");
    return codec_err(esp_codec_dev_write(s_audio.out, (void *)pcm, (int)bytes), "write");
}

esp_err_t drv_audio_out_close(void)
{
    if (!s_audio.out_open) {
        return ESP_OK;
    }
    s_audio.out_open = false;
    const esp_err_t err = codec_err(esp_codec_dev_close(s_audio.out), "out close");
    chans_down();
    return err;
}

esp_err_t drv_audio_set_volume(int percent)
{
    s_audio.volume = percent < 0 ? 0 : percent > 100 ? 100 : percent;
    return s_audio.out_open ? codec_err(esp_codec_dev_set_out_vol(s_audio.out, s_audio.volume), "volume") : ESP_OK;
}

esp_err_t drv_audio_set_mute(bool mute)
{
    ESP_RETURN_ON_FALSE(s_audio.out_open, ESP_ERR_INVALID_STATE, TAG, "out closed");
    return codec_err(esp_codec_dev_set_out_mute(s_audio.out, mute), "mute");
}

esp_err_t drv_audio_in_open(uint32_t rate, uint8_t channels, float gain_db)
{
    ESP_RETURN_ON_FALSE(s_audio.in && !s_audio.in_open, ESP_ERR_INVALID_STATE, TAG, "in state");
    ESP_RETURN_ON_ERROR(chans_up(), TAG, "channels");
    esp_codec_dev_sample_info_t fs = {.bits_per_sample = 16, .channel = channels, .sample_rate = rate};
    const esp_err_t err = codec_err(codec_open(s_audio.in, &fs), "in open");
    if (err != ESP_OK) {
        chans_down();
        return err;
    }
    s_audio.in_open = true;
    return codec_err(esp_codec_dev_set_in_gain(s_audio.in, gain_db), "gain");
}

esp_err_t drv_audio_in_read(void *pcm, size_t bytes)
{
    ESP_RETURN_ON_FALSE(s_audio.in_open, ESP_ERR_INVALID_STATE, TAG, "in closed");
    return codec_err(esp_codec_dev_read(s_audio.in, pcm, (int)bytes), "read");
}

esp_err_t drv_audio_in_close(void)
{
    if (!s_audio.in_open) {
        return ESP_OK;
    }
    s_audio.in_open = false;
    const esp_err_t err = codec_err(esp_codec_dev_close(s_audio.in), "in close");
    chans_down();
    return err;
}

// --- Loopback self-test ------------------------------------------------------------

#define LB_RATE        16000
#define LB_CH          2
#define LB_CHUNK       256
#define LB_AMPL        12000
#define LB_GAIN_DB     30.0f
#define LB_SETTLE_MS   200
#define LB_MEASURE_MS  1000
#define LB_MIN_SNR_DB  15.0f
#define LB_MIN_RMS     100.0f

esp_err_t drv_audio_loopback(uint32_t hz, audio_dsp_detect_t result[2])
{
    const size_t settle = LB_RATE * LB_SETTLE_MS / 1000;
    const size_t measure = LB_RATE * LB_MEASURE_MS / 1000;
    int16_t *rec = heap_caps_malloc(measure * LB_CH * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    int16_t *out = heap_caps_malloc(LB_CHUNK * LB_CH * sizeof(int16_t), MALLOC_CAP_DEFAULT);
    int16_t *in = heap_caps_malloc(LB_CHUNK * LB_CH * sizeof(int16_t), MALLOC_CAP_DEFAULT);
    esp_err_t err = rec && out && in ? ESP_OK : ESP_ERR_NO_MEM;
    if (err == ESP_OK) {
        err = drv_audio_in_open(LB_RATE, LB_CH, LB_GAIN_DB);
    }
    if (err == ESP_OK) {
        err = drv_audio_out_open(LB_RATE, LB_CH);
    }
    // Write and read alternately: both directions run off the same clock, so each
    // call blocks for about one chunk and neither DMA ring over/underruns.
    uint32_t phase = 0;
    size_t captured = 0;
    for (size_t t = 0; t < settle + measure && err == ESP_OK; t += LB_CHUNK) {
        audio_dsp_tone(out, LB_CHUNK, LB_CH, LB_RATE, hz, LB_AMPL, &phase);
        err = drv_audio_out_write(out, LB_CHUNK * LB_CH * sizeof(int16_t));
        if (err == ESP_OK) {
            err = drv_audio_in_read(in, LB_CHUNK * LB_CH * sizeof(int16_t));
        }
        if (err == ESP_OK && t >= settle) {
            const size_t n = measure - captured < LB_CHUNK ? measure - captured : LB_CHUNK;
            memcpy(rec + captured * LB_CH, in, n * LB_CH * sizeof(int16_t));
            captured += n;
        }
    }
    drv_audio_out_close();
    drv_audio_in_close();
    if (err == ESP_OK) {
        for (int mic = 0; mic < LB_CH; mic++) {
            result[mic] = audio_dsp_detect_tone(rec + mic, captured, LB_CH, LB_RATE, hz, LB_MIN_SNR_DB, LB_MIN_RMS);
        }
    }
    heap_caps_free(rec);
    heap_caps_free(out);
    heap_caps_free(in);
    return err;
}
