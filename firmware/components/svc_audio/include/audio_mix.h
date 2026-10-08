// System sounds and the mixer (pure C, host-tested). Sounds are short note sequences
// synthesised at 16 kHz mono (docs/03 F15); the mixer sums up to MIX_VOICES of them. Mini apps
// (P8-04) add a tone of their own (SND_APP_TONE) and a PCM stream (SND_APP_STREAM).
// Per-sound volume (percent, like the codec's) is applied relative to the loudest voice,
// so the service sets the codec to that loudest volume and scales the others in software.
// App sounds also carry a gain (the app's own percent) applied to their samples only: the
// codec's percent is a dB scale (esp_codec_dev: 1..100 % = -50..0 dB), so multiplying it by
// the app's percent would cut far more than the app asked for (P3-11a).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MIX_RATE        16000
#define MIX_VOICES      4
#define MIX_RENDER_MAX  512 // frames per mix_render call

typedef enum {
    SND_CLICK,       // soft tick for UI events
    SND_NOTIFY,      // notification arrives
    SND_SUCCESS,     // done / confirmed
    SND_CHARGING,    // charger connected
    SND_LOW_BATTERY, // 15 / 10 / 3 % warnings
    SND_ALARM,       // loops until stopped
    SND_TIMER,       // loops until stopped
    SND_FIND,        // find my watch (P4-08): loops until stopped
    SND_RING,        // incoming call (P6-04): loops until stopped
    SND_APP_TONE,    // a mini app's tone (mix_start_tone): frequency and length per voice
    SND_APP_STREAM,  // a mini app's 16 kHz PCM (mix_start_stream): until stopped
    SND_COUNT,
} snd_id_t;

typedef enum {
    SND_CAT_SYSTEM, // VOLUME_SYSTEM; muted by Silent and by quiet modes (DND, sleep, theater); also the call ring
    SND_CAT_MEDIA,  // VOLUME_MEDIA; muted by Silent only (reserved for the player, P2)
    SND_CAT_ALARM,  // VOLUME_ALARM; never muted
    SND_CAT_FIND,   // full volume, never muted: the watch must be heard wherever it is
} snd_cat_t;

typedef struct {
    bool silent;      // SILENT setting
    bool quiet;       // modes_state_t.quiet
    int vol_system;   // 0..100
    int vol_media;
    int vol_alarm;
} snd_policy_t;

snd_cat_t snd_category(snd_id_t id);
bool snd_loops(snd_id_t id);
const char *snd_name(snd_id_t id);
/** Sound by name (console), SND_COUNT if unknown. */
snd_id_t snd_by_name(const char *name);
/** Length of one pass in ms (a looping sound repeats after this). */
uint32_t snd_length_ms(snd_id_t id);

/** Volume (percent) the sound plays at under the policy; 0 = do not play. */
int snd_volume(snd_id_t id, const snd_policy_t *p);

/** Pulls up to frames samples of a stream (SND_APP_STREAM); returns how many it gave (the rest
 *  is silence). Runs inside mix_render(). */
typedef size_t (*mix_pull_fn)(void *ctx, int16_t *out, size_t frames);

typedef struct {
    bool active;
    uint8_t id;        // snd_id_t
    uint8_t note;      // index in the sound's notes
    uint16_t volume;   // percent (the ramp's top)
    uint8_t gain;      // percent of the samples' amplitude (app sounds; 100 for the others)
    uint32_t ramp_ms;  // 0 = none; else from volume/3 up to volume over ramp_ms
    uint32_t note_left; // frames left in the note
    uint32_t note_len;  // frames in the note
    uint32_t phase;     // oscillator
    uint32_t frames;    // played, for the ramp
    uint16_t tone_hz;   // SND_APP_TONE
    uint16_t tone_ms;
    mix_pull_fn pull;   // SND_APP_STREAM
    void *pull_ctx;
} mix_voice_t;

typedef struct {
    mix_voice_t v[MIX_VOICES];
} mix_t;

void mix_init(mix_t *m);

/**
 * Start a sound at volume percent (> 0). The same sound already playing restarts.
 * Returns false if all voices are busy or the arguments are bad.
 */
bool mix_start(mix_t *m, snd_id_t id, int volume, uint32_t ramp_ms);
/** SND_APP_TONE: hz 20..8000, ms 1..10000, gain 1..100 (replaces a tone already playing). */
bool mix_start_tone(mix_t *m, int hz, int ms, int volume, int gain);
/** SND_APP_STREAM: plays what pull gives, scaled by gain 0..100, until mix_stop(SND_APP_STREAM). */
bool mix_start_stream(mix_t *m, mix_pull_fn pull, void *ctx, int volume, int gain);
void mix_stop(mix_t *m, snd_id_t id);
void mix_stop_all(mix_t *m);
bool mix_playing(const mix_t *m, snd_id_t id);
bool mix_active(const mix_t *m);

/**
 * Render `frames` (<= MIX_RENDER_MAX) mono frames. Returns the volume (percent) the codec
 * should be at for this chunk: the loudest active voice, 0 if nothing played (out is
 * then silence). Voices whose sound ends are freed.
 */
int mix_render(mix_t *m, int16_t *out, size_t frames);

#ifdef __cplusplus
}
#endif
