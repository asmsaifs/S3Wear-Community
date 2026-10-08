#include "audio_mix.h"

#include <string.h>

#include "audio_dsp.h"

typedef struct {
    uint16_t hz; // 0 = rest
    uint16_t ms;
} note_t;

typedef struct {
    const char *name;
    snd_cat_t cat;
    bool loop;
    int16_t ampl;
    const note_t *notes;
    uint8_t count;
} sound_t;

#define N(a) (sizeof(a) / sizeof((a)[0]))

static const note_t k_click[] = {{1800, 12}};
static const note_t k_notify[] = {{988, 90}, {0, 40}, {1319, 140}};
static const note_t k_success[] = {{784, 80}, {1047, 80}, {1319, 140}};
static const note_t k_charging[] = {{659, 70}, {880, 110}};
static const note_t k_low[] = {{660, 120}, {0, 80}, {660, 120}};
// Alarm: 4 beeps (on 100, off 100) then a pause: 1400 ms. Timer: 2 beeps (150/100), 1000 ms.
static const note_t k_alarm[] = {{880, 100}, {0, 100}, {880, 100}, {0, 100}, {880, 100}, {0, 100},
                                 {880, 100}, {0, 100}, {0, 600}};
static const note_t k_timer[] = {{880, 150}, {0, 100}, {880, 150}, {0, 100}, {0, 500}};
// Find: two rising C-E-G chirps then a pause: 1500 ms (unlike the alarm, so it is recognisable).
static const note_t k_find[] = {{1047, 120}, {1319, 120}, {1568, 160}, {0, 100},
                                {1047, 120}, {1319, 120}, {1568, 160}, {0, 600}};

// Ring: two 400 ms trills (E6/G#6 every 50 ms, like a desk phone) then a pause: 2000 ms.
static const note_t k_ring[] = {{1319, 50}, {1661, 50}, {1319, 50}, {1661, 50}, {1319, 50}, {1661, 50},
                                {1319, 50}, {1661, 50}, {0, 200},   {1319, 50}, {1661, 50}, {1319, 50},
                                {1661, 50}, {1319, 50}, {1661, 50}, {1319, 50}, {1661, 50}, {0, 1000}};

static const sound_t k_sounds[SND_COUNT] = {
    [SND_CLICK] = {"click", SND_CAT_SYSTEM, false, 6000, k_click, N(k_click)},
    [SND_NOTIFY] = {"notify", SND_CAT_SYSTEM, false, 12000, k_notify, N(k_notify)},
    [SND_SUCCESS] = {"success", SND_CAT_SYSTEM, false, 10000, k_success, N(k_success)},
    [SND_CHARGING] = {"charging", SND_CAT_SYSTEM, false, 10000, k_charging, N(k_charging)},
    [SND_LOW_BATTERY] = {"lowbat", SND_CAT_SYSTEM, false, 12000, k_low, N(k_low)},
    [SND_ALARM] = {"alarm", SND_CAT_ALARM, true, 14000, k_alarm, N(k_alarm)},
    [SND_TIMER] = {"timer", SND_CAT_ALARM, true, 14000, k_timer, N(k_timer)},
    [SND_FIND] = {"find", SND_CAT_FIND, true, 14000, k_find, N(k_find)},
    [SND_RING] = {"ring", SND_CAT_SYSTEM, true, 12000, k_ring, N(k_ring)},
    // Mini apps: the note is the voice's own (tone_hz, tone_ms); the stream has no notes.
    [SND_APP_TONE] = {"apptone", SND_CAT_MEDIA, false, 12000, NULL, 1},
    [SND_APP_STREAM] = {"appstream", SND_CAT_MEDIA, false, 0, NULL, 0},
};

#define EDGE_FRAMES 80 // 5 ms attack and release: no clicks at note edges

snd_cat_t snd_category(snd_id_t id)
{
    return id < SND_COUNT ? k_sounds[id].cat : SND_CAT_SYSTEM;
}

bool snd_loops(snd_id_t id)
{
    return id < SND_COUNT && k_sounds[id].loop;
}

const char *snd_name(snd_id_t id)
{
    return id < SND_COUNT ? k_sounds[id].name : "?";
}

snd_id_t snd_by_name(const char *name)
{
    for (int i = 0; name && i < SND_COUNT; i++) {
        if (strcmp(name, k_sounds[i].name) == 0) {
            return (snd_id_t)i;
        }
    }
    return SND_COUNT;
}

uint32_t snd_length_ms(snd_id_t id)
{
    uint32_t ms = 0;
    if (id < SND_COUNT && k_sounds[id].notes) { // app sounds: their own length
        for (int i = 0; i < k_sounds[id].count; i++) {
            ms += k_sounds[id].notes[i].ms;
        }
    }
    return ms;
}

static int clamp_pct(int v)
{
    return v < 0 ? 0 : v > 100 ? 100 : v;
}

int snd_volume(snd_id_t id, const snd_policy_t *p)
{
    if (id >= SND_COUNT) {
        return 0;
    }
    switch (k_sounds[id].cat) {
    case SND_CAT_FIND:
        return 100;
    case SND_CAT_ALARM:
        return clamp_pct(p->vol_alarm);
    case SND_CAT_MEDIA:
        return p->silent ? 0 : clamp_pct(p->vol_media);
    case SND_CAT_SYSTEM:
    default:
        return p->silent || p->quiet ? 0 : clamp_pct(p->vol_system);
    }
}

void mix_init(mix_t *m)
{
    memset(m, 0, sizeof *m);
}

// The voice's current note: from the sound's table, or the app tone's own.
static note_t voice_note(const mix_voice_t *v)
{
    if (v->id == SND_APP_TONE) {
        return (note_t){v->tone_hz, v->tone_ms};
    }
    return k_sounds[v->id].notes[v->note];
}

static void note_begin(mix_voice_t *v)
{
    v->note_len = v->note_left = (uint32_t)voice_note(v).ms * (MIX_RATE / 1000);
    v->phase = 0;
}

// A voice for id: the one already playing it (restarted), else a free one; NULL if none.
static mix_voice_t *voice_for(mix_t *m, snd_id_t id)
{
    mix_voice_t *slot = NULL;
    for (int i = 0; i < MIX_VOICES; i++) {
        if (m->v[i].active && m->v[i].id == id) {
            return &m->v[i];
        }
        if (!m->v[i].active && !slot) {
            slot = &m->v[i];
        }
    }
    return slot;
}

bool mix_start(mix_t *m, snd_id_t id, int volume, uint32_t ramp_ms)
{
    if (id >= SND_COUNT || id == SND_APP_TONE || id == SND_APP_STREAM || volume <= 0) {
        return false;
    }
    mix_voice_t *slot = voice_for(m, id);
    if (!slot) {
        return false;
    }
    memset(slot, 0, sizeof *slot);
    slot->active = true;
    slot->id = (uint8_t)id;
    slot->volume = (uint16_t)clamp_pct(volume);
    slot->gain = 100;
    slot->ramp_ms = ramp_ms;
    note_begin(slot);
    return true;
}

bool mix_start_tone(mix_t *m, int hz, int ms, int volume, int gain)
{
    if (hz < 20 || hz > 8000 || ms < 1 || ms > 10000 || volume <= 0 || gain < 1 || gain > 100) {
        return false;
    }
    mix_voice_t *slot = voice_for(m, SND_APP_TONE);
    if (!slot) {
        return false;
    }
    memset(slot, 0, sizeof *slot);
    slot->active = true;
    slot->id = SND_APP_TONE;
    slot->volume = (uint16_t)clamp_pct(volume);
    slot->gain = (uint8_t)gain;
    slot->tone_hz = (uint16_t)hz;
    slot->tone_ms = (uint16_t)ms;
    note_begin(slot);
    return true;
}

bool mix_start_stream(mix_t *m, mix_pull_fn pull, void *ctx, int volume, int gain)
{
    if (!pull || volume <= 0 || gain < 0 || gain > 100) {
        return false;
    }
    mix_voice_t *slot = voice_for(m, SND_APP_STREAM);
    if (!slot) {
        return false;
    }
    memset(slot, 0, sizeof *slot);
    slot->active = true;
    slot->id = SND_APP_STREAM;
    slot->volume = (uint16_t)clamp_pct(volume);
    slot->gain = (uint8_t)gain;
    slot->pull = pull;
    slot->pull_ctx = ctx;
    return true;
}

void mix_stop(mix_t *m, snd_id_t id)
{
    for (int i = 0; i < MIX_VOICES; i++) {
        if (m->v[i].active && m->v[i].id == id) {
            m->v[i].active = false;
        }
    }
}

void mix_stop_all(mix_t *m)
{
    for (int i = 0; i < MIX_VOICES; i++) {
        m->v[i].active = false;
    }
}

bool mix_playing(const mix_t *m, snd_id_t id)
{
    for (int i = 0; i < MIX_VOICES; i++) {
        if (m->v[i].active && m->v[i].id == id) {
            return true;
        }
    }
    return false;
}

bool mix_active(const mix_t *m)
{
    for (int i = 0; i < MIX_VOICES; i++) {
        if (m->v[i].active) {
            return true;
        }
    }
    return false;
}

// Voice volume now: with a ramp from a third of the top up to all of it.
static int voice_volume(const mix_voice_t *v)
{
    if (!v->ramp_ms) {
        return v->volume;
    }
    const uint64_t el_ms = (uint64_t)v->frames / (MIX_RATE / 1000);
    const int lo = v->volume / 3;
    const int vol = el_ms >= v->ramp_ms ? v->volume : lo + (int)((v->volume - lo) * el_ms / v->ramp_ms);
    return vol < 1 ? 1 : vol;
}

int mix_render(mix_t *m, int16_t *out, size_t frames)
{
    if (frames > MIX_RENDER_MAX) {
        frames = MIX_RENDER_MAX;
    }
    int vol[MIX_VOICES] = {0};
    int top = 0;
    for (int i = 0; i < MIX_VOICES; i++) {
        if (m->v[i].active) {
            vol[i] = voice_volume(&m->v[i]);
            top = vol[i] > top ? vol[i] : top;
        }
    }
    if (!top) {
        memset(out, 0, frames * sizeof *out);
        return 0;
    }

    int32_t acc[MIX_RENDER_MAX] = {0};
    int16_t tone[MIX_RENDER_MAX];
    for (int i = 0; i < MIX_VOICES; i++) {
        mix_voice_t *v = &m->v[i];
        if (!v->active) {
            continue;
        }
        const sound_t *snd = &k_sounds[v->id];
        const int32_t scale = vol[i] * 256 / top * v->gain / 100; // 0..256
        if (v->id == SND_APP_STREAM) {
            const size_t got = v->pull(v->pull_ctx, tone, frames);
            for (size_t k = 0; k < got && k < frames; k++) {
                acc[k] += (int32_t)tone[k] * scale >> 8;
            }
            v->frames += (uint32_t)frames;
            continue; // plays (silence when starved) until stopped
        }
        size_t pos = 0;
        while (pos < frames && v->active) {
            if (v->note_left == 0) {
                if (++v->note >= snd->count) {
                    if (!snd->loop) {
                        v->active = false;
                        break;
                    }
                    v->note = 0;
                }
                note_begin(v);
            }
            const note_t n = voice_note(v);
            const size_t run = v->note_left < frames - pos ? v->note_left : frames - pos;
            if (n.hz) {
                audio_dsp_tone(tone, run, 1, MIX_RATE, n.hz, snd->ampl, &v->phase);
                const uint32_t edge = v->note_len / 4 < EDGE_FRAMES ? (v->note_len / 4 ? v->note_len / 4 : 1) : EDGE_FRAMES;
                for (size_t k = 0; k < run; k++) {
                    const uint32_t p = v->note_len - v->note_left + (uint32_t)k;
                    const uint32_t from_end = v->note_len - 1 - p;
                    uint32_t e = p < from_end ? p : from_end;
                    e = e >= edge ? 256 : e * 256 / edge;
                    acc[pos + k] += (((int32_t)tone[k] * (int32_t)e) >> 8) * scale >> 8;
                }
            }
            v->note_left -= (uint32_t)run;
            v->frames += (uint32_t)run;
            pos += run;
        }
    }
    for (size_t k = 0; k < frames; k++) {
        out[k] = (int16_t)(acc[k] > 32767 ? 32767 : acc[k] < -32768 ? -32768 : acc[k]);
    }
    return top;
}
