#include "audio_dsp.h"

#include <math.h>

#define TWO_PI 6.28318530717958647692f

void audio_dsp_tone(int16_t *out, size_t frames, uint8_t channels, uint32_t rate, uint32_t hz, int16_t amplitude,
                    uint32_t *phase)
{
    const uint32_t step = (uint32_t)(((uint64_t)hz << 32) / rate);
    uint32_t ph = *phase;
    for (size_t i = 0; i < frames; i++) {
        const float s = sinf((float)ph * (TWO_PI / 4294967296.0f));
        const int16_t v = (int16_t)lrintf(s * amplitude);
        for (uint8_t c = 0; c < channels; c++) {
            *out++ = v;
        }
        ph += step;
    }
    *phase = ph;
}

float audio_dsp_goertzel(const int16_t *x, size_t n, size_t stride, uint32_t rate, uint32_t hz)
{
    const float coeff = 2.0f * cosf(TWO_PI * (float)hz / (float)rate);
    float s1 = 0.0f;
    float s2 = 0.0f;
    for (size_t i = 0; i < n; i++) {
        const float s0 = (float)x[i * stride] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const float power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    return n ? power / ((float)n * (float)n) : 0.0f;
}

float audio_dsp_rms(const int16_t *x, size_t n, size_t stride)
{
    double acc = 0.0;
    for (size_t i = 0; i < n; i++) {
        const double v = x[i * stride];
        acc += v * v;
    }
    return n ? (float)sqrt(acc / (double)n) : 0.0f;
}

audio_dsp_detect_t audio_dsp_detect_tone(const int16_t *x, size_t n, size_t stride, uint32_t rate, uint32_t hz,
                                         float min_snr_db, float min_rms)
{
    // Probe frequencies away from the tone and its first harmonic.
    static const float k_probe[] = {0.61f, 0.79f, 1.37f, 1.53f, 2.71f};
    audio_dsp_detect_t r = {0};
    const float p_tone = audio_dsp_goertzel(x, n, stride, rate, hz);
    float p_off = 0.0f;
    int probes = 0;
    for (size_t i = 0; i < sizeof k_probe / sizeof k_probe[0]; i++) {
        const uint32_t f = (uint32_t)((float)hz * k_probe[i]);
        if (f > 0 && f < rate / 2) {
            p_off += audio_dsp_goertzel(x, n, stride, rate, f);
            probes++;
        }
    }
    p_off = probes ? p_off / (float)probes : 0.0f;
    r.rms = audio_dsp_rms(x, n, stride);
    r.snr_db = 10.0f * log10f((p_tone + 1e-9f) / (p_off + 1e-9f));
    r.detected = r.snr_db >= min_snr_db && r.rms >= min_rms;
    return r;
}
