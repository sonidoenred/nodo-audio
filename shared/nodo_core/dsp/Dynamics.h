#pragma once

#include <cmath>
#include <juce_core/juce_core.h>

namespace nodo::dsp
{
/** Level detection and gain computation, shared by the whole suite.

    The EQ uses this per band to drive its dynamic mode; the compressor will use
    the same two pieces on the full-band signal. Keeping them here rather than
    inside the EQ is the difference between writing a compressor later and
    writing a compressor's envelope follower again later.
*/

/** Peak follower with separate attack and release times.

    Works on the rectified signal in the linear domain: converting to dB per
    sample would cost a logarithm per sample for no benefit, since the gain
    computer only needs a value once per control block.
*/
class EnvelopeFollower
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
        attackMs = juce::jmax (0.01f, newAttackMs);
        releaseMs = juce::jmax (0.5f, newReleaseMs);

        attackCoeff = coefficientFor (attackMs);
        releaseCoeff = coefficientFor (releaseMs);
    }

    /** One sample of the signal being watched. Returns the current envelope. */
    inline float process (float input) noexcept
    {
        const auto rectified = std::abs (input);
        const auto coeff = rectified > envelope ? attackCoeff : releaseCoeff;

        envelope = rectified + coeff * (envelope - rectified);

        if (! std::isfinite (envelope) || envelope < 1.0e-12f)
            envelope = 0.0f;

        return envelope;
    }

    float getEnvelope() const noexcept { return envelope; }

private:
    float coefficientFor (float milliseconds) const noexcept
    {
        // One-pole time constant: the envelope covers 1 - 1/e of the distance in
        // the stated time.
        return std::exp (-1.0f / (milliseconds * 0.001f * (float) sampleRate));
    }

    double sampleRate { 44100.0 };
    float envelope { 0.0f };
    float attackMs { 10.0f }, releaseMs { 120.0f };
    float attackCoeff { 0.0f }, releaseCoeff { 0.0f };
};

/** Mean-square follower with a single time constant.

    RMS detection is what makes a compressor read loudness rather than
    transients: the same setting that clamps hard on a snare hit will sit
    politely on a vocal. The window is a one-pole rather than a sliding buffer
    because a sliding window costs memory per channel and, at these lengths,
    sounds the same.
*/
class RmsFollower
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        setWindow (windowMs);
        reset();
    }

    void reset() noexcept { meanSquare = 0.0f; }

    void setWindow (float newWindowMs) noexcept
    {
        windowMs = juce::jmax (0.1f, newWindowMs);
        coeff = std::exp (-1.0f / (windowMs * 0.001f * (float) sampleRate));
    }

    /** Returns the running RMS magnitude, not the mean square. */
    inline float process (float input) noexcept
    {
        const auto squared = input * input;
        meanSquare = squared + coeff * (meanSquare - squared);

        if (! std::isfinite (meanSquare) || meanSquare < 1.0e-20f)
            meanSquare = 0.0f;

        return std::sqrt (meanSquare);
    }

private:
    double sampleRate { 44100.0 };
    float meanSquare { 0.0f };
    float windowMs { 10.0f };
    float coeff { 0.0f };
};

/** The static curve of a downward compressor: how much gain reduction a given
    input level asks for, before any attack or release smoothing.

    Returns a value at or below zero, in dB. Separate from the dynamic EQ's
    version above because a compressor has no range limit and no upward
    direction, and because the limiter is going to want exactly this function.

    The soft knee is a quadratic tangent to both straight sections, so the curve
    and its slope are continuous: at the threshold itself the reduction is
    slope * knee / 8, which is the number the tests pin.
*/
inline float computeStaticGainDb (float levelDb,
                                  float thresholdDb,
                                  float ratio,
                                  float kneeDb) noexcept
{
    if (! std::isfinite (levelDb))
        return 0.0f;

    // Anything past 1000:1 is a brick wall for every practical purpose, and
    // treating it as infinite avoids a division that grows without bound.
    const auto slope = ratio >= 1000.0f ? 1.0f
                                        : 1.0f - 1.0f / juce::jmax (1.0f, ratio);
    const auto knee = juce::jmax (0.0f, kneeDb);
    const auto over = levelDb - thresholdDb;

    if (knee <= 0.0f)
        return over > 0.0f ? -over * slope : 0.0f;

    if (over <= -knee * 0.5f)
        return 0.0f;

    if (over >= knee * 0.5f)
        return -over * slope;

    const auto x = over + knee * 0.5f;
    return -slope * x * x / (2.0f * knee);
}

/** The static curve of an upward compressor: how much gain to *add* to material
    that falls below the threshold.

    The mirror image of the function above, and the same knee. Upward work is
    where a compressor stops being a safety device and starts being a way to
    hear the quiet parts, but it is also where an unbounded gain computer will
    happily amplify the noise floor by 40 dB — which is why the range control
    exists and why the caller is expected to use it.
*/
inline float computeUpwardGainDb (float levelDb,
                                  float thresholdDb,
                                  float ratio,
                                  float kneeDb) noexcept
{
    if (! std::isfinite (levelDb))
        return 0.0f;

    const auto slope = ratio >= 1000.0f ? 1.0f
                                        : 1.0f - 1.0f / juce::jmax (1.0f, ratio);
    const auto knee = juce::jmax (0.0f, kneeDb);
    const auto under = thresholdDb - levelDb;

    if (knee <= 0.0f)
        return under > 0.0f ? under * slope : 0.0f;

    if (under <= -knee * 0.5f)
        return 0.0f;

    if (under >= knee * 0.5f)
        return under * slope;

    const auto x = under + knee * 0.5f;
    return slope * x * x / (2.0f * knee);
}

/** Attack and release smoothing applied to the gain reduction itself, in dB.

    Smoothing the gain rather than the level is what separates a compressor from
    a gate with a slow envelope: the detector can jump instantly, and the attack
    time describes how fast the gain actually moves, which is what a user hears
    and what an attack knob is expected to mean.

    Two behaviours on top of the plain one-pole, both of them what "styles" are
    made of:

    - Program dependence: the release slows down the deeper the reduction is.
      Hardware does this as a side effect of its detector; it is the reason a
      slow compressor can still let go quickly after a stray peak.
    - Dual stage: a second, slower release running alongside the first, with the
      deeper of the two winning. This is the classic bus-compressor glue, where
      the fast stage handles individual hits and the slow one holds the level of
      the section.
*/
class GainSmoother
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        setTimes (attackMs, releaseMs);
        reset();
    }

    void reset() noexcept
    {
        currentDb = 0.0f;
        slowDb = 0.0f;
        holdCounter = 0;
    }

    void setTimes (float newAttackMs, float newReleaseMs) noexcept
    {
        attackMs = juce::jmax (0.01f, newAttackMs);
        releaseMs = juce::jmax (1.0f, newReleaseMs);

        attackCoeff = coefficientFor (attackMs);
        releaseCoeff = coefficientFor (releaseMs);
        slowCoeff = coefficientFor (releaseMs * 4.0f);
        holdSamples = (int) std::round (holdMs * 0.001f * sampleRate);
    }

    /** 0 = the release time means exactly what it says. 1 = heavily program
        dependent, roughly doubling by 6 dB of reduction.
    */
    void setProgramDependence (float amount) noexcept
    {
        programDepend = juce::jlimit (0.0f, 1.0f, amount);
    }

    /** 0 = single release stage. 1 = the slow stage decides on its own. */
    void setDualStage (float amount) noexcept
    {
        dualStage = juce::jlimit (0.0f, 1.0f, amount);
    }

    /** How long the gain stays where it is before the release starts.

        Without hold, a signal that dips for a few milliseconds — between words,
        between hits — lets the gain climb back and then get slammed down again,
        which is heard as chatter. Hold is the cheapest fix there is: do nothing
        for a moment and see if the signal comes back.
    */
    void setHold (float newHoldMs) noexcept
    {
        holdMs = juce::jmax (0.0f, newHoldMs);
        holdSamples = (int) std::round (holdMs * 0.001f * sampleRate);
    }

    /** targetDb is the static curve's answer for this sample, at or below zero.
        Returns the smoothed reduction to actually apply.
    */
    inline float process (float targetDb) noexcept
    {
        if (targetDb < currentDb)
        {
            // More reduction wanted: attack.
            currentDb = targetDb + attackCoeff * (currentDb - targetDb);
            holdCounter = holdSamples;
        }
        else if (holdCounter > 0)
        {
            // Holding: the signal has dropped, but not for long enough to
            // believe it.
            --holdCounter;
        }
        else
        {
            auto coeff = releaseCoeff;

            if (programDepend > 0.0f)
            {
                // Stretch the time constant with the depth of the reduction.
                // At 6 dB down and full program dependence the release takes
                // about twice as long as the knob says.
                const auto stretch = 1.0f + programDepend * (-currentDb) / 6.0f;
                coeff = std::exp (std::log (juce::jmax (1.0e-6f, releaseCoeff)) / stretch);
            }

            currentDb = targetDb + coeff * (currentDb - targetDb);
        }

        if (dualStage > 0.0f)
        {
            slowDb = targetDb < slowDb
                   ? targetDb + attackCoeff * (slowDb - targetDb)
                   : targetDb + slowCoeff * (slowDb - targetDb);

            const auto deeper = juce::jmin (currentDb, slowDb);
            return currentDb + dualStage * (deeper - currentDb);
        }

        slowDb = currentDb;

        if (! std::isfinite (currentDb))
            currentDb = 0.0f;

        return currentDb;
    }

    float getCurrentDb() const noexcept { return currentDb; }

private:
    float coefficientFor (float milliseconds) const noexcept
    {
        return std::exp (-1.0f / (juce::jmax (0.01f, milliseconds)
                                  * 0.001f * (float) sampleRate));
    }

    double sampleRate { 44100.0 };
    float currentDb { 0.0f }, slowDb { 0.0f };
    float attackMs { 10.0f }, releaseMs { 120.0f }, holdMs { 0.0f };
    float attackCoeff { 0.0f }, releaseCoeff { 0.0f }, slowCoeff { 0.0f };
    float programDepend { 0.0f }, dualStage { 0.0f };
    int holdSamples { 0 }, holdCounter { 0 };
};

/** How the dynamic section reacts to the detector. */
enum class DynamicDirection
{
    /** Engages as the level rises above the threshold. Taming a resonance, a
        de-esser, controlling a boomy note: this is what people mean by dynamic
        EQ nine times out of ten.
    */
    above = 0,

    /** Engages as the level falls below the threshold. Upward work: lifting
        detail that only shows up in the quiet parts.
    */
    below
};

struct DynamicSettings
{
    float thresholdDb { -24.0f };
    float ratio { 4.0f };
    float kneeDb { 6.0f };

    /** How far the band is allowed to travel, in dB. This is the band's own gain
        setting: the knob says where the band goes, the dynamics decide when it
        gets there.
    */
    float rangeDb { 0.0f };

    DynamicDirection direction { DynamicDirection::above };
};

/** Returns the gain to apply to the band right now, in dB, signed to match the
    range. Zero means the band is idle.

    A soft knee is built in rather than optional. A hard knee on a dynamic EQ
    chatters audibly on material that sits near the threshold, which is exactly
    where people set it.
*/
inline float computeDynamicGainDb (float levelDb, const DynamicSettings& settings) noexcept
{
    if (! std::isfinite (levelDb))
        return 0.0f;

    const auto ratio = juce::jmax (1.0f, settings.ratio);
    const auto knee = juce::jmax (0.0f, settings.kneeDb);
    const auto slope = 1.0f - 1.0f / ratio;

    // Distance past the point where the band should start working.
    const auto over = settings.direction == DynamicDirection::above
                    ? levelDb - settings.thresholdDb
                    : settings.thresholdDb - levelDb;

    float amountDb;

    if (knee <= 0.0f)
    {
        amountDb = over > 0.0f ? over * slope : 0.0f;
    }
    else if (over <= -knee * 0.5f)
    {
        amountDb = 0.0f;
    }
    else if (over >= knee * 0.5f)
    {
        amountDb = over * slope;
    }
    else
    {
        // Quadratic blend across the knee, tangent to both straight sections.
        const auto x = over + knee * 0.5f;
        amountDb = slope * x * x / (2.0f * knee);
    }

    const auto limit = std::abs (settings.rangeDb);
    amountDb = juce::jlimit (0.0f, limit, amountDb);

    return settings.rangeDb < 0.0f ? -amountDb : amountDb;
}
} // namespace nodo::dsp
