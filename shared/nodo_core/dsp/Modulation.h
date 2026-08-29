#pragma once

#include <cmath>
#include <juce_core/juce_core.h>

namespace nodo::dsp
{
/** A low frequency oscillator with the shapes people actually reach for.

    Runs per sample rather than per block. It costs a handful of arithmetic and
    it removes a whole class of problem: a modulator updated once per buffer
    steps, and a stepped modulator on a delay time is a zipper you can hear.

    Output is always -1 to 1 whatever the shape, so a matrix slot's amount means
    the same thing wherever it is pointed.
*/
class Lfo
{
public:
    /** FROZEN ORDER — the index is written into every saved session. */
    enum class Shape { sine = 0, triangle, saw, rampUp, square, randomStep, randomSmooth };

    static constexpr int numShapes = 7;

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        reset();
    }

    void reset() noexcept
    {
        phase = phaseOffset;
        held = 0.0f;
        target = 0.0f;
        previous = 0.0f;
        state = 0x2545F491u;
    }

    void setRateHz (float hz) noexcept
    {
        increment = (float) (juce::jlimit (0.001f, 100.0f, hz) / sampleRate);
    }

    void setShape (Shape newShape) noexcept { shape = newShape; }

    /** 0 to 1. Applied on the next reset rather than immediately: moving the
        phase of a running LFO is a click, and the control exists to offset one
        LFO against another, not to be swept.
    */
    void setPhaseOffset (float offset) noexcept { phaseOffset = juce::jlimit (0.0f, 1.0f, offset); }

    /** Puts the phase back to its offset without disturbing anything else.
        Hosts call this at transport start so a synced LFO lines up with the bar.
    */
    void retrigger() noexcept { phase = phaseOffset; }

    inline float process() noexcept
    {
        const auto wasBelow = phase;
        phase += increment;

        auto wrapped = false;

        if (phase >= 1.0f)
        {
            phase -= std::floor (phase);
            wrapped = true;
        }

        juce::ignoreUnused (wasBelow);

        switch (shape)
        {
            case Shape::triangle:
                return 4.0f * std::abs (phase - 0.5f) - 1.0f;

            case Shape::saw:
                return 1.0f - 2.0f * phase;

            case Shape::rampUp:
                return 2.0f * phase - 1.0f;

            case Shape::square:
                return phase < 0.5f ? 1.0f : -1.0f;

            case Shape::randomStep:
                if (wrapped)
                    held = nextRandom();

                return held;

            case Shape::randomSmooth:
            {
                if (wrapped)
                {
                    previous = target;
                    target = nextRandom();
                }

                // Cosine interpolation between the two draws: a straight line
                // has a corner at every step and the corner is audible on a
                // delay time.
                const auto blend = 0.5f - 0.5f * std::cos (phase * juce::MathConstants<float>::pi);
                return previous + (target - previous) * blend;
            }

            case Shape::sine:
            default:
                return std::sin (phase * juce::MathConstants<float>::twoPi);
        }
    }

    float getPhase() const noexcept { return phase; }

private:
    /** A plain linear congruential generator. Deterministic on purpose: two
        instances of the plugin given the same settings have to produce the same
        sound, or a session does not recall.
    */
    inline float nextRandom() noexcept
    {
        state = state * 1664525u + 1013904223u;
        return (float) ((state >> 8) & 0xffffffu) / 8388607.5f - 1.0f;
    }

    double sampleRate { 44100.0 };
    float phase { 0.0f }, increment { 0.0f }, phaseOffset { 0.0f };
    float held { 0.0f }, target { 0.0f }, previous { 0.0f };
    juce::uint32 state { 0x2545F491u };
    Shape shape { Shape::sine };
};

/** An envelope follower shaped for use as a modulation source.

    Different job from the one in Dynamics.h, which exists to drive a gain
    computer and therefore works in the linear domain with times measured
    against a compressor's expectations. This one answers "how loud is it right
    now, as a number between zero and one", and the mapping from decibels to
    that number is the interesting part: a linear follower spends almost all of
    its travel in the top few dB and then sits at zero, which as a modulator
    means nothing happens until something is nearly clipping.
*/
class ModulationEnvelope
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        setTimes (attackMs, releaseMs);
        reset();
    }

    void reset() noexcept { envelope = 0.0f; }

    void setTimes (float newAttackMs, float newReleaseMs) noexcept
    {
        attackMs = juce::jmax (0.1f, newAttackMs);
        releaseMs = juce::jmax (1.0f, newReleaseMs);

        attackCoeff = std::exp (-1.0f / (attackMs * 0.001f * (float) sampleRate));
        releaseCoeff = std::exp (-1.0f / (releaseMs * 0.001f * (float) sampleRate));
    }

    /** The level that reads as fully on, in dB. Anything at or above it gives 1;
        60 dB below it gives 0.
    */
    void setRange (float newTopDb, float newRangeDb) noexcept
    {
        topDb = newTopDb;
        rangeDb = juce::jmax (6.0f, newRangeDb);
    }

    /** Returns 0 to 1. */
    inline float process (float input) noexcept
    {
        const auto rectified = std::abs (input);
        const auto coeff = rectified > envelope ? attackCoeff : releaseCoeff;

        envelope = rectified + coeff * (envelope - rectified);

        if (! std::isfinite (envelope) || envelope < 1.0e-9f)
            envelope = 0.0f;

        const auto db = 20.0f * std::log10 (juce::jmax (1.0e-7f, envelope));

        return juce::jlimit (0.0f, 1.0f, (db - (topDb - rangeDb)) / rangeDb);
    }

    float getValue() const noexcept { return envelope; }

private:
    double sampleRate { 44100.0 };
    float envelope { 0.0f };
    float attackMs { 10.0f }, releaseMs { 200.0f };
    float attackCoeff { 0.0f }, releaseCoeff { 0.0f };
    float topDb { -6.0f }, rangeDb { 40.0f };
};
} // namespace nodo::dsp
