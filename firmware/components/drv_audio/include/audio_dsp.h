// Small audio helpers for bring-up and diagnostics (pure C, host-tested):
// sine generation and Goertzel tone detection for the speaker->mic loopback test.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Fill `frames` interleaved frames of `channels` with a sine at `hz`. *phase carries
 * the oscillator phase (0..2^32 = one cycle) between calls so chunks join cleanly.
 */
void audio_dsp_tone(int16_t *out, size_t frames, uint8_t channels, uint32_t rate, uint32_t hz, int16_t amplitude,
                    uint32_t *phase);

/** Goertzel power at `hz` over n samples taken every `stride` (interleaved channel select). */
float audio_dsp_goertzel(const int16_t *x, size_t n, size_t stride, uint32_t rate, uint32_t hz);

/** RMS of n samples taken every `stride`. */
float audio_dsp_rms(const int16_t *x, size_t n, size_t stride);

typedef struct {
    float rms;     // signal RMS (LSB)
    float snr_db;  // power at the tone vs. average of off-tone probe frequencies
    bool detected; // snr_db >= min_snr_db and rms >= min_rms
} audio_dsp_detect_t;

audio_dsp_detect_t audio_dsp_detect_tone(const int16_t *x, size_t n, size_t stride, uint32_t rate, uint32_t hz,
                                         float min_snr_db, float min_rms);

#ifdef __cplusplus
}
#endif
