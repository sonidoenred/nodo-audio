#pragma once

#include <juce_dsp/juce_dsp.h>
#include <nodo_core/nodo_core.h>

#include "GateParameters.h"

namespace nodo::gate
{
/** The static curve of an expander: how much gain a level below the threshold
    asks for, before any attack, hold or release.

    Returns a value at or below zero, in dB. The knee is the same quadratic
    tangent used everywhere else in the suite, so at the threshold itself the
    curve is exactly `slope * knee / 8` dB down and its slope is continuous with
    both straight sections. The tests pin that number.

    A gate is not a separate device from an expander, it is an expander with a
    slope steep enough that the range does all the work. Writing it as one
    function rather than two is what keeps the ratio knob continuous instead of
    snapping into a different behaviour at the top.
*/
inline float expansionGainDb (float levelDb,
                              float thresholdDb,
                              float slope,
                              float kneeDb,
                              float rangeDb) noexcept
{
    if (! std::isfinite (levelDb))
        return 0.0f;

    const auto under = thresholdDb - levelDb;
    const auto knee = juce::jmax (0.0f, kneeDb);

    auto raw = 0.0f;

    if (knee <= 0.0f)
        raw = under > 0.0f ? -slope * under : 0.0f;
    else if (under <= -knee * 0.5f)
        raw = 0.0f;
    else if (under >= knee * 0.5f)
        raw = -slope * under;
    else
    {
        const auto x = under + knee * 0.5f;
        raw = -slope * x * x / (2.0f * knee);
    }

    return juce::jmax (-juce::jmax (0.0f, rangeDb), raw);
}

/** Attack, hold and release for a gate, which is not the compressor's smoother
    with the sign flipped.

    In a compressor the attack is the move *into* reduction; in a gate the
    attack is the move *out* of it — opening. And hold means something different
    and more important here: it is how long the gate stays where it is after the
    signal falls away, before the release is allowed to start. Without it a
    gate on anything with a decay chatters, because the level crosses the
    threshold several times on the way down.
*/
class GateEnvelope
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        setTimes (attackMs, releaseMs);
        setHold (holdMs);
        reset();
    }

    void reset() noexcept
    {
        currentDb = 0.0f;
        slowDb = 0.0f;
        holdCounter = 0;
        primed = false;
    }

    void setTimes (float newAttackMs, float newReleaseMs) noexcept
    {
        attackMs  = juce::jmax (0.01f, newAttackMs);
        releaseMs = juce::jmax (0.5f, newReleaseMs);

        attackCoeff  = coefficientFor (attackMs);
        releaseCoeff = coefficientFor (releaseMs);
        slowCoeff    = coefficientFor (releaseMs * 4.0f);
    }

    void setHold (float newHoldMs) noexcept
    {
        holdMs = juce::jmax (0.0f, newHoldMs);
        holdSamples = (int) std::round (holdMs * 0.001f * sampleRate);
    }

    /** 0 = the release time means exactly what it says. 1 = the deeper the gate
        has closed, the longer it takes — which is what hardware does, and what
        makes a tail sound like a tail instead of a fade.
    */
    void setProgramDependence (float amount) noexcept
    {
        programDepend = juce::jlimit (0.0f, 1.0f, amount);
    }

    /** A second, slower release running alongside the first, with the *more
        open* of the two winning. On a bus this is what stops a single quiet bar
        from shutting the whole thing.
    */
    void setDualStage (float amount) noexcept
    {
        dualStage = juce::jlimit (0.0f, 1.0f, amount);
    }

    /** targetDb is the static curve's answer for this sample, at or below zero.
        Returns the gain to actually apply.
    */
    inline float process (float targetDb) noexcept
    {
        if (! primed)
        {
            // Snap on the first sample rather than sliding in from wherever the
            // envelope happened to be. A gate that fades open for a few
            // milliseconds every time the transport starts is a gate that eats
            // the first note.
            currentDb = targetDb;
            slowDb = targetDb;
            primed = true;
            return currentDb;
        }

        if (targetDb >= currentDb)
        {
            // Opening — or already open and being held there. Either way the
            // hold counter is refreshed, so it starts counting from the moment
            // the signal actually falls away.
            currentDb = targetDb + attackCoeff * (currentDb - targetDb);
            holdCounter = holdSamples;
        }
        else if (holdCounter > 0)
        {
            --holdCounter;
        }
        else
        {
            auto coeff = releaseCoeff;

            if (programDepend > 0.0f)
            {
                // Stretch the time constant with how far the gate has already
                // closed. At 24 dB down and full dependence the release takes
                // about twice as long as the knob says.
                const auto stretch = 1.0f + programDepend * (-currentDb) / 24.0f;
                coeff = std::exp (std::log (juce::jmax (1.0e-6f, releaseCoeff)) / stretch);
            }

            currentDb = targetDb + coeff * (currentDb - targetDb);
        }

        if (dualStage > 0.0f)
        {
            slowDb = targetDb >= slowDb
                   ? targetDb + attackCoeff * (slowDb - targetDb)
                   : targetDb + slowCoeff * (slowDb - targetDb);

            const auto opener = juce::jmax (currentDb, slowDb);
            const auto blended = currentDb + dualStage * (opener - currentDb);

            return std::isfinite (blended) ? blended : 0.0f;
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
    float attackMs { 1.0f }, releaseMs { 150.0f }, holdMs { 20.0f };
    float attackCoeff { 0.0f }, releaseCoeff { 0.0f }, slowCoeff { 0.0f };
    float programDepend { 0.0f }, dualStage { 0.0f };
    int holdSamples { 0 }, holdCounter { 0 };
    bool primed { false };
};

/** The gate itself.

    Feed-forward throughout: the detector reads the input (or the external
    sidechain), a Schmitt trigger decides whether the gate is considered open,
    the static curve turns the level into a wanted gain, and the envelope
    decides how fast the gain is allowed to move.

    The Schmitt trigger is the part people miss. A gate with one threshold
    opens and closes every time the level wobbles across it, which on anything
    with a decay is several times a second and is heard as chatter. Two
    thresholds — open here, close a few dB lower — cost nothing and remove the
    whole class of problem.

    Nothing here allocates. Every buffer is sized in prepare().
*/
class GateEngine
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    void setSettings (const GateSettings& newSettings) { pending = newSettings; }

    /** Message thread writes when a MIDI note arrives; the audio thread reads it
        at the top of the next block.
    */
    void setTriggerOpen (bool shouldBeOpen) noexcept { triggerOpen = shouldBeOpen; }

    int getLatencySamples() const noexcept { return lookaheadSamples; }
    int computeLatencySamples() const noexcept;

    /** sidechain may be null; when it is, a request for the external sidechain
        falls back to the input, which is what a host that has not connected the
        bus will hand us anyway.
    */
    void process (juce::dsp::AudioBlock<float>& block,
                  const juce::dsp::AudioBlock<const float>* sidechain);

    /** GUI. At or below zero. */
    float getGainReductionDb() const noexcept { return reductionDb.load (std::memory_order_relaxed); }

    /** GUI. What the detector is seeing right now, in dB. */
    float getDetectorLevelDb() const noexcept { return detectorDb.load (std::memory_order_relaxed); }

    /** GUI. Whether the Schmitt trigger currently considers the gate open, which
        is not the same question as whether the gain has finished moving.
    */
    bool isOpen() const noexcept { return openNow.load (std::memory_order_relaxed); }

    /** The static curve, for drawing and for tests: input level in, output level
        out, without the envelope.
    */
    static float staticOutputDb (float inputDb, const GateSettings& settings);

private:
    void applyPendingSettings();

    GateSettings settings, pending;
    bool settingsValid { false };

    double sampleRate { 44100.0 };
    int maxBlockSize { 512 };

    // Lookahead. A plain ring buffer per channel: the delay is a whole number of
    // samples, so there is no interpolation to get wrong.
    std::array<std::vector<float>, 2> delayBuffer;
    int delayWritePos { 0 }, delayLength { 0 }, lookaheadSamples { 0 };

    std::array<dsp::EnvelopeFollower, 2> peakFollowers;
    std::array<dsp::RmsFollower, 2> rmsFollowers;
    bool useRms { false };

    std::array<GateEnvelope, 2> envelopes;
    std::array<bool, 2> gateOpen { false, false };

    std::array<dsp::Biquad, 2> scHighpass, scLowpass;
    bool scFiltersActive { false };

    juce::SmoothedValue<float> mixAmount;

    std::atomic<bool> triggerOpen { false };

    std::atomic<float> reductionDb { 0.0f };
    std::atomic<float> detectorDb { -100.0f };
    std::atomic<bool> openNow { false };

    JUCE_LEAK_DETECTOR (GateEngine)
};
} // namespace nodo::gate
