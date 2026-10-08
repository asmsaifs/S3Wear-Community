// System sounds and the mixer (pure C, host-tested). Sounds are short note sequences
// synthesised at 16 kHz mono (docs/03 F15); the mixer sums up to MIX_VOICES of them.
// Per-sound volume (percent, like the codec's) is applied relative to the loudest voice,
// so the service sets the codec to that loudest volume and scales the others in software.
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
    SND_COUNT,
} snd_id_t;

typedef enum {
    SND_CAT_SYSTEM, // VOLUME_SYSTEM; muted by Silent and by quiet modes (DND, sleep, theater)
    SND_CAT_MEDIA,  // VOLUME_MEDIA; muted by Silent only (reserved for the player, P2)
    SND_CAT_ALARM,  // VOLUME_ALARM; never muted
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

typedef struct {
    bool active;
    uint8_t id;        // snd_id_t
    uint8_t note;      // index in the sound's notes
    uint16_t volume;   // percent (the ramp's top)
    uint32_t ramp_ms;  // 0 = none; else from volume/3 up to volume over ramp_ms
    uint32_t note_left; // frames left in the note
    uint32_t note_len;  // frames in the note
    uint32_t phase;     // oscillator
    uint32_t frames;    // played, for the ramp
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
