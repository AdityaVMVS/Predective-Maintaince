#include "analysis.h"
#include "arm_math.h"
#include <math.h>

static arm_rfft_fast_instance_f32 fft;
static float hann[WINDOW], scratch[WINDOW], spectrum[WINDOW], gain;

bool analysis_init(void)
{
    gain = 0;
    for (unsigned i = 0; i < WINDOW; ++i) {
        hann[i] = 0.5f - 0.5f * cosf(6.28318530718f * (float)i / (WINDOW - 1));
        gain += hann[i];
    }
    return arm_rfft_fast_init_1024_f32(&fft) == ARM_MATH_SUCCESS;
}

Features analyze(const Sample samples[WINDOW], unsigned axis)
{
    /* 10 Hz Butterworth high-pass at 1600 Hz, with each window starting at rest. */
    const float b = 0.972613899f, a1 = -1.944477658f, a2 = 0.945977938f;
    float mean = 0, filtered_mean = 0, peak = 0, square = 0, fourth = 0;
    for (unsigned i = 0; i < WINDOW; ++i) mean += samples[i].axis[axis];
    mean /= WINDOW;
    float z1 = 0, z2 = 0;
    for (unsigned i = 0; i < WINDOW; ++i) {
        float x = ((float)samples[i].axis[axis] - mean) * 0.0039f;
        float y = b * x + z1;
        z1 = -2 * b * x - a1 * y + z2;
        z2 = b * x - a2 * y;
        scratch[i] = y;
        filtered_mean += y;
        square += y * y;
        peak = fmaxf(peak, fabsf(y));
    }
    filtered_mean /= WINDOW;
    float variance = 0;
    for (unsigned i = 0; i < WINDOW; ++i) {
        float deviation = scratch[i] - filtered_mean;
        float s = deviation * deviation;
        variance += s;
        fourth += s * s;
        scratch[i] *= hann[i];
    }
    float rms = sqrtf(square / WINDOW);
    Features out = {.value = {rms, peak, rms > 1e-8f ? peak / rms : 0,
                              variance > 1e-10f ? WINDOW * fourth / (variance * variance) : 0}};
    arm_rfft_fast_f32(&fft, scratch, spectrum, 0);
    float largest = 0, weighted = 0;
    float amplitude_scale = 2 / gain;
    for (unsigned i = 1; i < WINDOW / 2; ++i) {
        float frequency = (float)i * RATE / WINDOW;
        if (frequency < 10 || frequency > 600) continue;
        float re = spectrum[2 * i] * amplitude_scale;
        float im = spectrum[2 * i + 1] * amplitude_scale;
        float power = re * re + im * im;
        if (power > largest) {
            largest = power;
            out.value[4] = frequency;
        }
        out.value[5] += power;
        weighted += frequency * power;
        unsigned band = frequency < 100 ? 7 : frequency < 300 ? 8 : 9;
        out.value[band] += power;
    }
    if (out.value[5] > 1e-12f) out.value[6] = weighted / out.value[5];
    return out;
}

Condition classify(Detector *d, Features f, float *score)
{
    const float values[4] = {f.value[0], f.value[4], f.value[5], f.value[3]};
    const float floors[4] = {0.005f, 3.125f, 0.0001f, 0.25f};
    const float weights[4] = {0.4f, 0.15f, 0.3f, 0.15f};
    *score = 0;
    if (d->count < TRAINING) {
        ++d->count;
        for (unsigned i = 0; i < 4; ++i) {
            float delta = values[i] - d->mean[i];
            d->mean[i] += delta / d->count;
            d->sumsq[i] += delta * (values[i] - d->mean[i]);
        }
        if (d->count == TRAINING) d->condition = NORMAL;
        return LEARNING;
    }
    float z[4];
    for (unsigned i = 0; i < 4; ++i) {
        float deviation = sqrtf(fmaxf(0, d->sumsq[i]) / (TRAINING - 1));
        z[i] = (values[i] - d->mean[i]) / fmaxf(deviation, floors[i]);
        *score += weights[i] * fminf(fabsf(z[i]), 16);
    }
    bool elevated = z[0] >= 3 || z[2] >= 3;
    bool high = z[0] >= 8 || z[2] >= 8;
    bool unusual = fabsf(z[1]) >= 4 || fabsf(z[3]) >= 4 || *score >= 3;
    d->raised = elevated ? (d->raised < 3 ? d->raised + 1 : 3) : 0;
    d->severe = high ? (d->severe < 3 ? d->severe + 1 : 3) : 0;
    d->shape = unusual ? (d->shape < 3 ? d->shape + 1 : 3) : 0;
    d->quiet = !elevated && !unusual && *score < 1.5f ? (d->quiet < 3 ? d->quiet + 1 : 3) : 0;
    if (d->severe == 3) d->condition = CRITICAL;
    else if (d->condition != CRITICAL && d->raised == 3) d->condition = WARNING;
    else if (d->condition == NORMAL && d->shape == 3) d->condition = ANOMALY;
    else if (d->quiet == 3) d->condition = NORMAL;
    return d->condition;
}
