// Console: `audio tone|rec|play|loop|vol` — ES8311/ES7210 bring-up (P1-07);
// `audio snd|ring|stop|status` — system sounds through svc_audio (P3-11).
// Everything runs at 16 kHz, 16-bit, 2 channels (output: both channels equal,
// input: MIC1/MIC2 interleaved).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_dsp.h"
#include "bsp_s3w.h"
#include "drv_audio.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "svc_audio.h"
#include "svc_diag_priv.h"

#define RATE          16000
#define CH            2
#define CHUNK_FRAMES  256
#define TONE_AMPL     12000
#define REC_MAX_S     10
#define MIC_GAIN_DB   30.0f

static int16_t *s_rec;      // PSRAM, interleaved stereo
static size_t s_rec_frames;

static int audio_tone(uint32_t hz, uint32_t ms, int vol)
{
    if (vol >= 0) {
        drv_audio_set_volume(vol);
    }
    if (drv_audio_out_open(RATE, CH) != ESP_OK) {
        printf("speaker open failed\n");
        return 1;
    }
    int16_t buf[CHUNK_FRAMES * CH];
    uint32_t phase = 0;
    const size_t total = (size_t)RATE * ms / 1000;
    esp_err_t err = ESP_OK;
    for (size_t done = 0; done < total && err == ESP_OK; done += CHUNK_FRAMES) {
        audio_dsp_tone(buf, CHUNK_FRAMES, CH, RATE, hz, TONE_AMPL, &phase);
        err = drv_audio_out_write(buf, sizeof buf);
    }
    drv_audio_out_close();
    printf("tone %lu Hz %lu ms: %s\n", (unsigned long)hz, (unsigned long)ms, err == ESP_OK ? "done" : "write error");
    return err == ESP_OK ? 0 : 1;
}

static int audio_rec(int seconds)
{
    if (seconds <= 0 || seconds > REC_MAX_S) {
        printf("1..%d seconds\n", REC_MAX_S);
        return 1;
    }
    const size_t frames = (size_t)RATE * seconds;
    heap_caps_free(s_rec);
    s_rec_frames = 0;
    s_rec = heap_caps_malloc(frames * CH * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!s_rec) {
        printf("no PSRAM for %d s\n", seconds);
        return 1;
    }
    if (drv_audio_in_open(RATE, CH, MIC_GAIN_DB) != ESP_OK) {
        printf("mic open failed\n");
        return 1;
    }
    printf("recording %d s...\n", seconds);
    esp_err_t err = ESP_OK;
    size_t done = 0;
    while (done < frames && err == ESP_OK) {
        const size_t n = frames - done < CHUNK_FRAMES ? frames - done : CHUNK_FRAMES;
        err = drv_audio_in_read(s_rec + done * CH, n * CH * sizeof(int16_t));
        done += n;
    }
    drv_audio_in_close();
    s_rec_frames = done;
    printf("recorded %u frames, RMS mic1 %.0f mic2 %.0f\n", (unsigned)done, audio_dsp_rms(s_rec, done, CH),
           audio_dsp_rms(s_rec + 1, done, CH));
    return err == ESP_OK ? 0 : 1;
}

static int audio_play(void)
{
    if (!s_rec_frames) {
        printf("nothing recorded (audio rec <s>)\n");
        return 1;
    }
    if (drv_audio_out_open(RATE, CH) != ESP_OK) {
        printf("speaker open failed\n");
        return 1;
    }
    esp_err_t err = ESP_OK;
    for (size_t done = 0; done < s_rec_frames && err == ESP_OK; done += CHUNK_FRAMES) {
        const size_t n = s_rec_frames - done < CHUNK_FRAMES ? s_rec_frames - done : CHUNK_FRAMES;
        int16_t buf[CHUNK_FRAMES * CH];
        for (size_t i = 0; i < n; i++) { // play MIC1 on both channels
            buf[2 * i] = buf[2 * i + 1] = s_rec[(done + i) * CH];
        }
        err = drv_audio_out_write(buf, n * CH * sizeof(int16_t));
    }
    drv_audio_out_close();
    printf("playback %s\n", err == ESP_OK ? "done" : "error");
    return err == ESP_OK ? 0 : 1;
}

static int audio_loop(uint32_t hz)
{
    audio_dsp_detect_t r[2];
    if (drv_audio_loopback(hz, r) != ESP_OK) {
        printf("codec I/O error\n");
        return 1;
    }
    for (int mic = 0; mic < 2; mic++) {
        printf("mic%d: %s (tone SNR %.1f dB, RMS %.0f)\n", mic + 1, r[mic].detected ? "PASS" : "FAIL", r[mic].snr_db,
               r[mic].rms);
    }
    const bool pass = r[0].detected && r[1].detected;
    printf("loopback %lu Hz: %s\n", (unsigned long)hz, pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

static int audio_snd(const char *name)
{
    const snd_id_t id = snd_by_name(name);
    if (id == SND_COUNT) {
        printf("sounds:");
        for (int i = 0; i < SND_COUNT; i++) {
            printf(" %s", snd_name((snd_id_t)i));
        }
        printf("\n");
        return 1;
    }
    const esp_err_t err = snd_loops(id) ? svc_audio_ring_start(id) : svc_audio_play(id);
    printf("%s: %s%s\n", name, err == ESP_OK ? "queued" : esp_err_to_name(err),
           snd_loops(id) ? " (loops until `audio stop`)" : "");
    return err == ESP_OK ? 0 : 1;
}

static int audio_status(void)
{
    svc_audio_status_t st;
    svc_audio_get_status(&st);
    printf("svc_audio: %s, codec %s, %s; plays %lu muted %lu dropped %lu errors %lu opens %lu vol %d\n",
           st.available ? "speaker" : "no speaker", st.codec_open ? "open" : "closed", st.playing ? "playing" : "idle",
           (unsigned long)st.plays, (unsigned long)st.muted, (unsigned long)st.dropped, (unsigned long)st.errors,
           (unsigned long)st.opens, st.volume);
    return 0;
}

static int cmd_audio(int argc, char **argv)
{
    if (!bsp_audio_ready()) {
        printf("audio not initialised\n");
        return 1;
    }
    const char *sub = argc >= 2 ? argv[1] : "";
    if (strcmp(sub, "tone") == 0 && argc >= 4) {
        return audio_tone((uint32_t)atoi(argv[2]), (uint32_t)atoi(argv[3]), argc >= 5 ? atoi(argv[4]) : -1);
    }
    if (strcmp(sub, "rec") == 0 && argc >= 3) {
        return audio_rec(atoi(argv[2]));
    }
    if (strcmp(sub, "play") == 0) {
        return audio_play();
    }
    if (strcmp(sub, "loop") == 0) {
        return audio_loop(argc >= 3 ? (uint32_t)atoi(argv[2]) : 1000);
    }
    if (strcmp(sub, "vol") == 0 && argc >= 3) {
        return drv_audio_set_volume(atoi(argv[2])) == ESP_OK ? 0 : 1;
    }
    if (strcmp(sub, "snd") == 0 && argc >= 3) {
        return audio_snd(argv[2]);
    }
    if (strcmp(sub, "ring") == 0 && argc >= 3) {
        return audio_snd(argv[2]);
    }
    if (strcmp(sub, "stop") == 0) {
        return svc_audio_stop_all() == ESP_OK ? 0 : 1;
    }
    if (strcmp(sub, "status") == 0) {
        return audio_status();
    }
    printf("usage: audio tone <hz> <ms> [vol] | rec <s> | play | loop [hz] | vol <0..100> | snd <name> | ring <alarm|timer> "
           "| stop | status\n");
    return 1;
}

esp_err_t diag_register_audio(void)
{
    const esp_console_cmd_t cmd = {
        .command = "audio",
        .help = "ES8311/ES7210 @16 kHz: tone <hz> <ms> [vol], rec <s> (max 10), play, loop [hz] "
                "(speaker->mic tone detection), vol <0..100>; snd <click|notify|success|charging|lowbat|alarm|timer> plays a "
                "system sound with the policy applied (Silent, quiet modes), stop, status",
        .hint = "tone|rec|play|loop|vol|snd|ring|stop|status",
        .func = cmd_audio,
    };
    return esp_console_cmd_register(&cmd);
}
