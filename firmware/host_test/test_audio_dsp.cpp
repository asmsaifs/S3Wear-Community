// Audio helpers (components/drv_audio/audio_dsp.c).
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include "audio_dsp.h"

static constexpr uint32_t kRate = 16000;

static std::vector<int16_t> tone(uint32_t hz, size_t frames, uint8_t ch, int16_t amp)
{
    std::vector<int16_t> v(frames * ch);
    uint32_t phase = 0;
    audio_dsp_tone(v.data(), frames, ch, kRate, hz, amp, &phase);
    return v;
}

TEST(AudioDspTone, AmplitudeAndInterleave)
{
    const auto v = tone(1000, 160, 2, 10000);
    int16_t peak = 0;
    for (size_t i = 0; i < 160; i++) {
        EXPECT_EQ(v[2 * i], v[2 * i + 1]); // same sample on both channels
        peak = std::max<int16_t>(peak, static_cast<int16_t>(std::abs(v[2 * i])));
    }
    EXPECT_NEAR(peak, 10000, 50);
    EXPECT_NEAR(audio_dsp_rms(v.data(), 160, 2), 10000 / std::sqrt(2.0), 100);
}

TEST(AudioDspTone, PhaseContinuesAcrossChunks)
{
    std::vector<int16_t> a(100), b(100), whole(200);
    uint32_t p1 = 0;
    audio_dsp_tone(a.data(), 100, 1, kRate, 440, 8000, &p1);
    audio_dsp_tone(b.data(), 100, 1, kRate, 440, 8000, &p1);
    uint32_t p2 = 0;
    audio_dsp_tone(whole.data(), 200, 1, kRate, 440, 8000, &p2);
    for (size_t i = 0; i < 100; i++) {
        EXPECT_EQ(a[i], whole[i]);
        EXPECT_EQ(b[i], whole[100 + i]);
    }
}

TEST(AudioDspGoertzel, PeaksAtToneFrequency)
{
    const auto v = tone(1000, 4000, 1, 8000);
    const float at = audio_dsp_goertzel(v.data(), v.size(), 1, kRate, 1000);
    const float off = audio_dsp_goertzel(v.data(), v.size(), 1, kRate, 1370);
    EXPECT_GT(at, 1000.0f * off);
}

TEST(AudioDspDetect, FindsToneInNoise)
{
    auto v = tone(1000, 8000, 2, 3000);
    std::mt19937 rng(42);
    std::normal_distribution<float> noise(0.0f, 1500.0f);
    for (auto &s : v) {
        s = static_cast<int16_t>(std::clamp(s + noise(rng), -32768.0f, 32767.0f));
    }
    const auto r = audio_dsp_detect_tone(v.data(), 8000, 2, kRate, 1000, 15.0f, 100.0f);
    EXPECT_TRUE(r.detected) << r.snr_db;
}

TEST(AudioDspDetect, RejectsSilenceNoiseAndWrongTone)
{
    std::vector<int16_t> silence(8000, 0);
    EXPECT_FALSE(audio_dsp_detect_tone(silence.data(), 8000, 1, kRate, 1000, 15.0f, 100.0f).detected);

    std::vector<int16_t> noise(8000);
    std::mt19937 rng(7);
    std::normal_distribution<float> n(0.0f, 3000.0f);
    for (auto &s : noise) {
        s = static_cast<int16_t>(n(rng));
    }
    EXPECT_FALSE(audio_dsp_detect_tone(noise.data(), 8000, 1, kRate, 1000, 15.0f, 100.0f).detected);

    const auto other = tone(2500, 8000, 1, 8000);
    EXPECT_FALSE(audio_dsp_detect_tone(other.data(), 8000, 1, kRate, 1000, 15.0f, 100.0f).detected);
}
