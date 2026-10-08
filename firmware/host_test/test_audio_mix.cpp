// svc_audio: sound policy (categories, Silent, quiet modes) and the mixer.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "audio_dsp.h"
#include "audio_mix.h"

namespace {

snd_policy_t policy(bool silent = false, bool quiet = false)
{
    return {silent, quiet, 50, 70, 80};
}

constexpr size_t kChunk = 320; // 20 ms

// Render ms of audio, returning samples; `vols` collects the codec volume per chunk.
std::vector<int16_t> render(mix_t *m, int ms, std::vector<int> *vols = nullptr)
{
    std::vector<int16_t> all;
    for (int t = 0; t < ms; t += 20) {
        int16_t buf[kChunk];
        const int v = mix_render(m, buf, kChunk);
        if (vols) {
            vols->push_back(v);
        }
        all.insert(all.end(), buf, buf + kChunk);
    }
    return all;
}

int peak(const std::vector<int16_t> &x, size_t from = 0, size_t to = SIZE_MAX)
{
    int p = 0;
    for (size_t i = from; i < x.size() && i < to; i++) {
        p = std::max(p, std::abs((int)x[i]));
    }
    return p;
}

} // namespace

TEST(AudioPolicy, SystemSoundsUseSystemVolume)
{
    const snd_policy_t p = policy();
    EXPECT_EQ(snd_volume(SND_CLICK, &p), 50);
    EXPECT_EQ(snd_volume(SND_NOTIFY, &p), 50);
    EXPECT_EQ(snd_volume(SND_CHARGING, &p), 50);
}

TEST(AudioPolicy, SilentMutesSystemButNotAlarms)
{
    const snd_policy_t p = policy(true, false);
    for (int i = 0; i < SND_COUNT; i++) {
        const snd_id_t id = (snd_id_t)i;
        if (snd_category(id) == SND_CAT_ALARM) {
            EXPECT_EQ(snd_volume(id, &p), 80) << snd_name(id);
        } else if (snd_category(id) == SND_CAT_FIND) {
            EXPECT_EQ(snd_volume(id, &p), 100) << snd_name(id);
        } else {
            EXPECT_EQ(snd_volume(id, &p), 0) << snd_name(id);
        }
    }
}

TEST(AudioPolicy, QuietModesMuteSystemButNotAlarms)
{
    const snd_policy_t p = policy(false, true);
    EXPECT_EQ(snd_volume(SND_NOTIFY, &p), 0);
    EXPECT_EQ(snd_volume(SND_LOW_BATTERY, &p), 0);
    EXPECT_EQ(snd_volume(SND_ALARM, &p), 80);
    EXPECT_EQ(snd_volume(SND_TIMER, &p), 80);
}

TEST(AudioPolicy, FindIsFullVolumeInEveryMode)
{
    snd_policy_t p = policy(true, true);
    p.vol_alarm = 0;
    p.vol_system = 0;
    EXPECT_EQ(snd_volume(SND_FIND, &p), 100);
}

TEST(AudioPolicy, ZeroSystemVolumeMutes)
{
    snd_policy_t p = policy();
    p.vol_system = 0;
    EXPECT_EQ(snd_volume(SND_CLICK, &p), 0);
}

TEST(AudioPolicy, NamesRoundTrip)
{
    for (int i = 0; i < SND_COUNT; i++) {
        EXPECT_EQ(snd_by_name(snd_name((snd_id_t)i)), (snd_id_t)i);
    }
    EXPECT_EQ(snd_by_name("nope"), SND_COUNT);
    EXPECT_EQ(snd_by_name(nullptr), SND_COUNT);
}

TEST(AudioPolicy, RingersLoop)
{
    EXPECT_TRUE(snd_loops(SND_ALARM));
    EXPECT_TRUE(snd_loops(SND_TIMER));
    EXPECT_TRUE(snd_loops(SND_FIND));
    EXPECT_FALSE(snd_loops(SND_CLICK));
    EXPECT_EQ(snd_length_ms(SND_ALARM), 1400u);
    EXPECT_EQ(snd_length_ms(SND_TIMER), 1000u);
    EXPECT_EQ(snd_length_ms(SND_FIND), 1500u);
    EXPECT_TRUE(snd_loops(SND_RING));
    EXPECT_EQ(snd_length_ms(SND_RING), 2000u);
}

TEST(AudioPolicy, CallRingIsASystemSound)
{
    snd_policy_t p = policy();
    EXPECT_EQ(snd_volume(SND_RING, &p), p.vol_system);
    EXPECT_EQ(snd_volume(SND_RING, &(p = policy(true, false))), 0) << "Silent";
    EXPECT_EQ(snd_volume(SND_RING, &(p = policy(false, true))), 0) << "DND, sleep, theater";
}

TEST(AudioMix, IdleRendersSilenceAndZeroVolume)
{
    mix_t m;
    mix_init(&m);
    int16_t buf[kChunk];
    memset(buf, 0x55, sizeof buf);
    EXPECT_EQ(mix_render(&m, buf, kChunk), 0);
    for (int16_t v : buf) {
        EXPECT_EQ(v, 0);
    }
    EXPECT_FALSE(mix_active(&m));
}

TEST(AudioMix, RejectsBadStart)
{
    mix_t m;
    mix_init(&m);
    EXPECT_FALSE(mix_start(&m, SND_COUNT, 50, 0));
    EXPECT_FALSE(mix_start(&m, SND_CLICK, 0, 0));
    EXPECT_FALSE(mix_active(&m));
}

TEST(AudioMix, OneShotPlaysItsLengthThenFreesTheVoice)
{
    mix_t m;
    mix_init(&m);
    ASSERT_TRUE(mix_start(&m, SND_NOTIFY, 60, 0));
    EXPECT_TRUE(mix_playing(&m, SND_NOTIFY));
    const uint32_t len = snd_length_ms(SND_NOTIFY);
    const auto pcm = render(&m, (int)len - 20);
    EXPECT_GT(peak(pcm), 1000);
    EXPECT_TRUE(mix_active(&m));
    render(&m, 40); // past the end
    EXPECT_FALSE(mix_active(&m));
}

TEST(AudioMix, ClickStartsAndEndsAtZero)
{
    mix_t m;
    mix_init(&m);
    ASSERT_TRUE(mix_start(&m, SND_CLICK, 100, 0));
    int16_t buf[kChunk];
    mix_render(&m, buf, kChunk);
    EXPECT_EQ(buf[0], 0);
    const size_t n = snd_length_ms(SND_CLICK) * (MIX_RATE / 1000);
    EXPECT_EQ(buf[n - 1], 0);
    EXPECT_GT(peak({buf, buf + n}, 0, n), 1000);
    EXPECT_EQ(buf[n + 1], 0);
}

TEST(AudioMix, AlarmBeepsAt880HzAndPauses)
{
    mix_t m;
    mix_init(&m);
    ASSERT_TRUE(mix_start(&m, SND_ALARM, 80, 0));
    const auto pcm = render(&m, 1400);
    // First beep 0..100 ms: 880 Hz.
    const auto d = audio_dsp_detect_tone(pcm.data() + 160, 1280, 1, MIX_RATE, 880, 15.0f, 500.0f);
    EXPECT_TRUE(d.detected) << d.snr_db << " dB, rms " << d.rms;
    // Gap 100..200 ms is silent; the 600 ms pause at the end of the period too.
    EXPECT_EQ(peak(pcm, 1700, 3100), 0);
    EXPECT_EQ(peak(pcm, 12800, 22400), 0);
    // Four beeps: count rising edges of the envelope.
    int beeps = 0;
    bool on = false;
    for (size_t i = 0; i < pcm.size(); i += 80) {
        const bool now = peak(pcm, i, i + 80) > 500;
        beeps += now && !on;
        on = now;
    }
    EXPECT_EQ(beeps, 4);
}

TEST(AudioMix, LoopsUntilStopped)
{
    mix_t m;
    mix_init(&m);
    ASSERT_TRUE(mix_start(&m, SND_TIMER, 80, 0));
    const auto pcm = render(&m, 5000); // five periods
    EXPECT_TRUE(mix_playing(&m, SND_TIMER));
    EXPECT_GT(peak(pcm, 70000, 72400), 1000); // a beep in the fifth period
    mix_stop(&m, SND_TIMER);
    EXPECT_FALSE(mix_active(&m));
}

TEST(AudioMix, RampRisesFromAThirdToFull)
{
    mix_t m;
    mix_init(&m);
    ASSERT_TRUE(mix_start(&m, SND_ALARM, 90, 30000));
    std::vector<int> vols;
    render(&m, 31000, &vols);
    EXPECT_EQ(vols.front(), 30); // 90 / 3
    EXPECT_EQ(vols.back(), 90);
    for (size_t i = 1; i < vols.size(); i++) {
        EXPECT_GE(vols[i], vols[i - 1]);
    }
    EXPECT_NEAR(vols[750], 60, 2); // halfway
}

TEST(AudioMix, CodecVolumeIsLoudestVoiceAndOthersScaleDown)
{
    // A notification alone at 20 %: the codec is at 20 % and the notification is at full scale.
    mix_t alone;
    mix_init(&alone);
    ASSERT_TRUE(mix_start(&alone, SND_NOTIFY, 20, 0));
    int16_t a[kChunk];
    EXPECT_EQ(mix_render(&alone, a, kChunk), 20);
    const int full = peak({a, a + kChunk});

    // Next to a 100 % ringer the codec is at 100 % and the notification at a fifth of its scale.
    mix_t both;
    mix_init(&both);
    ASSERT_TRUE(mix_start(&both, SND_NOTIFY, 20, 0));
    ASSERT_TRUE(mix_start(&both, SND_ALARM, 100, 0));
    int16_t b[kChunk];
    EXPECT_EQ(mix_render(&both, b, kChunk), 100);
    EXPECT_GT(peak({b, b + kChunk}), full / 2);

    // Ringer gone: the codec drops back to the notification's volume.
    mix_stop(&both, SND_ALARM);
    EXPECT_EQ(mix_render(&both, b, kChunk), 20);
}

TEST(AudioMix, SameSoundRestartsInsteadOfStacking)
{
    mix_t m;
    mix_init(&m);
    ASSERT_TRUE(mix_start(&m, SND_NOTIFY, 50, 0));
    render(&m, 100);
    ASSERT_TRUE(mix_start(&m, SND_NOTIFY, 50, 0));
    int used = 0;
    for (const auto &v : m.v) {
        used += v.active;
    }
    EXPECT_EQ(used, 1);
    EXPECT_EQ(m.v[0].note, 0);
}

TEST(AudioMix, FullMixerDropsNewSound)
{
    mix_t m;
    mix_init(&m);
    ASSERT_TRUE(mix_start(&m, SND_CLICK, 50, 0));
    ASSERT_TRUE(mix_start(&m, SND_NOTIFY, 50, 0));
    ASSERT_TRUE(mix_start(&m, SND_SUCCESS, 50, 0));
    ASSERT_TRUE(mix_start(&m, SND_CHARGING, 50, 0));
    EXPECT_FALSE(mix_start(&m, SND_LOW_BATTERY, 50, 0));
    mix_stop_all(&m);
    EXPECT_TRUE(mix_start(&m, SND_LOW_BATTERY, 50, 0));
}

TEST(AudioMix, SumClipsInsteadOfWrapping)
{
    mix_t m;
    mix_init(&m);
    // Four loud voices at once: samples must stay within int16 without wrapping sign.
    ASSERT_TRUE(mix_start(&m, SND_ALARM, 100, 0));
    ASSERT_TRUE(mix_start(&m, SND_TIMER, 100, 0));
    ASSERT_TRUE(mix_start(&m, SND_NOTIFY, 100, 0));
    ASSERT_TRUE(mix_start(&m, SND_LOW_BATTERY, 100, 0));
    const auto pcm = render(&m, 400);
    EXPECT_LE(peak(pcm), 32768);
    // A wrapped sum would show as a jump of nearly full scale between neighbours.
    int worst = 0;
    for (size_t i = 1; i < pcm.size(); i++) {
        worst = std::max(worst, std::abs((int)pcm[i] - (int)pcm[i - 1]));
    }
    EXPECT_LT(worst, 40000);
}

TEST(AudioMix, ChunkBoundariesDoNotChangeTheSignal)
{
    mix_t a, b;
    mix_init(&a);
    mix_init(&b);
    ASSERT_TRUE(mix_start(&a, SND_SUCCESS, 70, 0));
    ASSERT_TRUE(mix_start(&b, SND_SUCCESS, 70, 0));
    int16_t whole[480];
    mix_render(&a, whole, 480);
    int16_t parts[480];
    mix_render(&b, parts, 100);
    mix_render(&b, parts + 100, 380);
    EXPECT_EQ(memcmp(whole, parts, sizeof whole), 0);
}

TEST(AudioMix, AppToneHasItsOwnPitchAndLength)
{
    mix_t m;
    mix_init(&m);
    EXPECT_FALSE(mix_start(&m, SND_APP_TONE, 50, 0)); // only through mix_start_tone
    EXPECT_FALSE(mix_start_tone(&m, 10, 100, 50, 100));    // below 20 Hz
    EXPECT_FALSE(mix_start_tone(&m, 440, 0, 50, 100));
    ASSERT_TRUE(mix_start_tone(&m, 1000, 50, 60, 100));
    const auto pcm = render(&m, 40);
    EXPECT_GT(peak(pcm), 1000);
    EXPECT_TRUE(mix_playing(&m, SND_APP_TONE));
    render(&m, 20); // 50 ms in all: done
    EXPECT_FALSE(mix_active(&m));
    // 1 kHz at 16 kHz: 16 samples a period, so about 2 sign changes per 16 samples mid-note.
    mix_init(&m);
    ASSERT_TRUE(mix_start_tone(&m, 1000, 100, 60, 100));
    const auto mid = render(&m, 50);
    int crossings = 0;
    for (size_t i = 400; i < 560; i++) {
        crossings += (mid[i - 1] < 0) != (mid[i] < 0);
    }
    EXPECT_NEAR(crossings, 20, 2);
}

struct Feed {
    std::vector<int16_t> pcm;
    size_t pos = 0;
    static size_t pull(void *ctx, int16_t *out, size_t frames)
    {
        auto *f = static_cast<Feed *>(ctx);
        const size_t n = std::min(frames, f->pcm.size() - f->pos);
        std::copy(f->pcm.begin() + (long)f->pos, f->pcm.begin() + (long)(f->pos + n), out);
        f->pos += n;
        return n;
    }
};

TEST(AudioMix, StreamPlaysWhatItPullsThenSilenceUntilStopped)
{
    mix_t m;
    mix_init(&m);
    Feed f;
    f.pcm.assign(100, 8000);
    EXPECT_FALSE(mix_start_stream(&m, nullptr, &f, 50, 100));
    ASSERT_TRUE(mix_start_stream(&m, Feed::pull, &f, 50, 100));
    int16_t out[300];
    EXPECT_EQ(mix_render(&m, out, 300), 50);
    EXPECT_EQ(out[0], 8000);
    EXPECT_EQ(out[99], 8000);
    EXPECT_EQ(out[100], 0); // starved: silence
    EXPECT_TRUE(mix_playing(&m, SND_APP_STREAM));
    mix_stop(&m, SND_APP_STREAM);
    EXPECT_FALSE(mix_active(&m));
}

TEST(AudioMix, StreamUnderALouderVoiceIsScaled)
{
    mix_t m;
    mix_init(&m);
    Feed f;
    f.pcm.assign(10, 8000);
    ASSERT_TRUE(mix_start_stream(&m, Feed::pull, &f, 25, 100));
    ASSERT_TRUE(mix_start_tone(&m, 2000, 1000, 100, 100));
    int16_t out[10];
    EXPECT_EQ(mix_render(&m, out, 10), 100); // the codec follows the louder voice
    // The stream is a quarter of the top volume; the tone starts at 0 (attack).
    EXPECT_NEAR(out[0], 2000, 50);
}

// P3-11a: the app's percent scales the samples; the codec stays at the media volume. (The codec's
// percent is a dB scale, so 25 % of media 60 sent as codec 15 % played at -42.5 dB: unheard.)
TEST(AudioMix, AppGainScalesTheSamplesNotTheCodec)
{
    mix_t full, quarter;
    mix_init(&full);
    mix_init(&quarter);
    EXPECT_FALSE(mix_start_tone(&quarter, 700, 100, 60, 0));
    EXPECT_FALSE(mix_start_tone(&quarter, 700, 100, 60, 101));
    ASSERT_TRUE(mix_start_tone(&full, 700, 100, 60, 100));
    ASSERT_TRUE(mix_start_tone(&quarter, 700, 100, 60, 25));
    int16_t a[480], b[480];
    EXPECT_EQ(mix_render(&full, a, 480), 60);
    EXPECT_EQ(mix_render(&quarter, b, 480), 60); // codec at the media volume either way
    const std::vector<int16_t> va(a, a + 480), vb(b, b + 480);
    EXPECT_GT(peak(va), 10000);
    EXPECT_NEAR(peak(vb), peak(va) / 4, 100);
}

TEST(AudioMix, StreamGainZeroIsSilent)
{
    mix_t m;
    mix_init(&m);
    Feed f;
    f.pcm.assign(100, 8000);
    EXPECT_FALSE(mix_start_stream(&m, Feed::pull, &f, 50, 101));
    ASSERT_TRUE(mix_start_stream(&m, Feed::pull, &f, 1, 0));
    int16_t out[100];
    mix_render(&m, out, 100);
    EXPECT_EQ(out[0], 0);
    EXPECT_EQ(f.pos, 100u); // still drains at the app's pace
}

TEST(AudioPolicy, AppSoundsFollowMediaVolume)
{
    snd_policy_t p = policy();
    p.vol_media = 30;
    EXPECT_EQ(snd_volume(SND_APP_TONE, &p), 30);
    EXPECT_EQ(snd_volume(SND_APP_STREAM, &p), 30);
    EXPECT_EQ(snd_length_ms(SND_APP_TONE), 0u);
}
