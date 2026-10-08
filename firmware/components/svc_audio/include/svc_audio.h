// System sounds service (docs/03 F15, P3-11): plays the sounds in audio_mix.h through
// the speaker (drv_audio) with per-category volume, Silent and the quiet modes applied.
//
// One task feeds the I2S port; it sleeps until a request arrives, opens the codec for a
// sound and closes it SVC_AUDIO_IDLE_CLOSE_MS after the last one (rail and amp off
// otherwise). Policy at play time: system sounds (click, notify, success, charging, low
// battery) obey VOLUME_SYSTEM, SILENT and svc_modes quiet; alarm and timer sounds obey
// VOLUME_ALARM only. svc_audio itself plays the charger and low-battery sounds (from
// svc_power events); svc_alarm rings through it; svc_notify (P6) will play SND_NOTIFY.
//
// API calls are safe from any task except an ISR, and never block on audio.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "audio_mix.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SVC_AUDIO_IDLE_CLOSE_MS 1000
#define SVC_AUDIO_RING_RAMP_MS  30000 // ringing starts at a third of ALARM volume and rises to all of it

/** After svc_settings, svc_modes and bsp_audio_start; before svc_alarm. */
esp_err_t svc_audio_start(void);

/** True if a speaker was found (otherwise every call is a silent no-op). */
bool svc_audio_available(void);

/** Play a sound once (looping sounds: until svc_audio_stop). Dropped, with ESP_OK, if the policy mutes it. */
esp_err_t svc_audio_play(snd_id_t id);

/** Ringer: SND_ALARM or SND_TIMER, looping with the volume rising over SVC_AUDIO_RING_RAMP_MS. */
esp_err_t svc_audio_ring_start(snd_id_t id);

esp_err_t svc_audio_stop(snd_id_t id);
esp_err_t svc_audio_stop_all(void);

typedef struct {
    bool available;
    bool codec_open;
    bool playing;
    uint32_t plays;    // started
    uint32_t muted;    // dropped by Silent, quiet modes or volume 0
    uint32_t dropped;  // no free voice, or queue full
    uint32_t errors;   // codec open / write failures
    uint32_t opens;    // codec open count
    int volume;        // codec volume of the last chunk
} svc_audio_status_t;

void svc_audio_get_status(svc_audio_status_t *out);

#ifdef __cplusplus
}
#endif
