#pragma once

#include <cmath>
#include <juce_core/juce_core.h>

namespace nodo::dsp
{
/** A cubic soft clipper and its antiderivative.

    f(x) = x - x^3/3 inside the unit interval and flat at +-2/3 outside it, so
    it is continuous in value and in slope at the joins — no corner to generate
    an edge nobody asked for.
*/
inline float cubicClip (float x) noexcept
{
    if (x >= 1.0f)  return 2.0f / 3.0f;
    if (x <= -1.0f) return -2.0f / 3.0f;

    return x - x * x * x / 3.0f;
}

inline float cubicClipAntiderivative (float x) noexcept
{
    const auto a = std::abs (x);

    if (a <= 1.0f)
        return x * x * 0.5f - x * x * x * x / 12.0f;

    // Beyond the knee the function is a constant, so its integral is a line.
    return 2.0f * a / 3.0f - 0.25f;
}

/** Soft saturation with first-order antiderivative antialiasing.

    Any waveshaper generates harmonics, and any harmonic above half the sample
    rate folds back down as something inharmonic — the metallic edge that makes
    cheap distortion sound cheap. The usual fix is to oversample the whole
    thing, which inside a feedback loop means running the loop several times
    over.

    ADAA gets most of the way there for the price of one extra state variable.
    Instead of evaluating the shaping function at the sample, it evaluates the
    *average* of the function across the segment between this sample and the
    last, which is the antiderivative's difference quotient. Averaging is a low
    pass, and it is applied before the folding rather than after it, which is
    the only place a low pass can help.

    The difference quotient is numerically unstable when two samples are nearly
    equal, so below a threshold it falls back to evaluating at the midpoint —
    which is what the limit works out to anyway.
*/
class SoftSaturator
{
public:
    void reset() noexcept
    {
        previous = 0.0f;
        previousAntiderivative = 0.0f;
    }

    /** The level the output compensation is referenced to, about -12 dBFS.

        Which level you normalise at is the whole design of a drive control, and
        there is no answer that is free. Normalise at zero — divide by the drive
        — and small signals come out where they went in, but anything loud is
        crushed and the knob becomes a volume control pointing downwards.
        Normalise at full scale and small signals get lifted by the whole drive
        amount, which inside a feedback loop is a runaway.

        Referencing a level in the middle of where the loop actually sits keeps
        the knob about level where it matters. It costs a small-signal lift,
        which the closed form below bounds at exactly 1.5x at maximum drive.
    */
    static constexpr float referenceLevel = 0.25f;

    /** drive multiplies the input; the output is scaled back so a signal at the
        reference level comes out where it went in.
    */
    inline float process (float input, float drive) noexcept
    {
        const auto x = input * drive;
        const auto antiderivative = cubicClipAntiderivative (x);
        const auto delta = x - previous;

        const auto shaped = std::abs (delta) > 1.0e-5f
                          ? (antiderivative - previousAntiderivative) / delta
                          : cubicClip ((x + previous) * 0.5f);

        previous = x;
        previousAntiderivative = antiderivative;

        if (! std::isfinite (shaped))
            return 0.0f;

        return shaped * makeupFor (drive);
    }

    /** What the output has to be multiplied by so that the reference level is
        preserved. Exposed because it is exactly the number the tests pin.
    */
    static float makeupFor (float drive) noexcept
    {
        const auto g = juce::jmax (1.0f, drive);
        const auto driven = referenceLevel * g;

        return referenceLevel / juce::jmax (1.0e-6f, cubicClip (driven));
    }

private:
    float previous { 0.0f }, previousAntiderivative { 0.0f };
};

/** Deliberate degradation: fewer bits and a lower sample rate.

    The opposite job to the saturator above. The aliasing here is the point —
    it is what a 12 bit rack delay from 1985 sounds like — so nothing is done to
    prevent it, and the sample and hold runs at the full rate with a counter
    rather than being resampled properly.
*/
class BitCrusher
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        reset();
    }

    void reset() noexcept
    {
        held = 0.0f;
        phase = 0.0f;
    }

    /** amount is 0 to 1. At 0 the signal passes untouched and neither stage
        runs at all.
    */
    inline float process (float input, float amount) noexcept
    {
        if (amount <= 0.0f)
        {
            held = input;
            return input;
        }

        // 24 bits down to 5, and full rate down to about 3 kHz. Both curved so
        // the first half of the knob is gentle: the interesting part of this
        // effect is all at the top.
        const auto shaped = amount * amount;
        const auto bits = 24.0f - 19.0f * shaped;
        const auto crushRate = (float) sampleRate * (1.0f - 0.995f * shaped);

        phase += crushRate / (float) sampleRate;

        if (phase >= 1.0f)
        {
            phase -= std::floor (phase);

            const auto levels = std::pow (2.0f, bits - 1.0f);
            held = std::round (juce::jlimit (-1.5f, 1.5f, input) * levels) / levels;
        }

        return held;
    }

private:
    double sampleRate { 44100.0 };
    float held { 0.0f }, phase { 0.0f };
};

/** A last line of defence for a feedback loop.

    Transparent below the threshold and softly clipped above it, so a feedback
    setting past unity self-oscillates into a steady tone instead of into
    infinity. Not antialiased, on purpose: it only does anything when the loop
    is already in a state the user asked for and can hear.
*/
inline float loopSafety (float x, float threshold = 0.85f) noexcept
{
    const auto a = std::abs (x);

    if (a <= threshold)
        return x;

    const auto over = (a - threshold) / juce::jmax (1.0e-6f, 1.0f - threshold);
    const auto shaped = threshold + (1.0f - threshold) * std::tanh (over);

    return x < 0.0f ? -shaped : shaped;
}
} // namespace nodo::dsp
