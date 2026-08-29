#pragma once

#include <juce_dsp/juce_dsp.h>
#include <nodo_core/nodo_core.h>

#include "EssParameters.h"

namespace nodo::ess
{
/** The de-esser.

    A Linkwitz-Riley crossover splits the signal in two. The upper half is what
    the detector watches; the gain that comes out of that goes either onto the
    upper half alone or onto the whole signal, depending on the mode.

    The crossover is fourth order — two cascaded Butterworth sections a side —
    because that is the order whose two halves sum back to a flat magnitude. A
    de-esser that is not transparent when no sibilance is present is not a
    de-esser, it is a tone control that happens to move, and the tests hold this
    one to a millionth of full scale.

    The adaptive threshold is what makes it usable across a take rather than per
    phrase: the threshold rides a slow follower of the sibilant band, so the
    same setting works on a quiet line and a shouted one. The interface draws
    where that threshold has ended up, because a moving threshold you cannot see
    is a threshold you cannot trust.
*/
class EssEngine
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    void setSettings (const EssSettings& newSettings) { pending = newSettings; }

    int computeLatencySamples() const noexcept;
    int getLatencySamples() const noexcept { return lookaheadSamples; }

    void process (juce::dsp::AudioBlock<float>& block);

    /** GUI. At or below zero. */
    float getGainReductionDb() const noexcept { return reductionDb.load (std::memory_order_relaxed); }

    /** GUI. The level of the band being watched, in dB. */
    float getDetectorDb() const noexcept { return detectorDb.load (std::memory_order_relaxed); }

    /** GUI. Where the threshold has ended up, which is not the knob's value once
        the detector is adaptive.
    */
    float getEffectiveThresholdDb() const noexcept { return effectiveThresholdDb.load (std::memory_order_relaxed); }

    /** The part of the adaptive threshold that does not depend on the knob: the
        band's running level plus the reference offset.

        The interface needs this to turn "put the line here" into a knob value.
        Deriving it instead from the difference between the line and the knob
        would be algebraically identical and practically wrong — with the
        transport stopped nothing updates between two drags, so each one would
        be computed from the previous answer and the value would walk.
    */
    float getAdaptiveReferenceDb() const noexcept { return adaptiveReference.load (std::memory_order_relaxed); }

private:
    void applyPendingSettings();

    /** One half of a Linkwitz-Riley fourth order crossover: two identical
        Butterworth sections in series.
    */
    struct CrossoverHalf
    {
        std::array<dsp::Biquad, 2> sections;

        void reset() noexcept { for (auto& s : sections) s.reset(); }

        void setCoefficients (const dsp::BiquadCoefficients& c) noexcept
        {
            for (auto& s : sections)
                s.setCoefficients (c);
        }

        inline float process (float x) noexcept
        {
            return sections[1].processSample (sections[0].processSample (x));
        }

        void snapToZero() noexcept { for (auto& s : sections) s.snapToZero(); }
    };

    EssSettings settings, pending;
    bool settingsValid { false };

    double sampleRate { 44100.0 };
    int maxBlockSize { 512 };

    std::array<CrossoverHalf, 2> lowBand, highBand;

    /** The top of the detector's band. Detection only: the audio never goes
        through it, so narrowing the detector cannot colour anything.
    */
    std::array<dsp::Biquad, 2> detectorTop;
    bool detectorTopActive { false };

    std::array<dsp::GainSmoother, 2> smoothers;

    // Lookahead. Both halves are delayed rather than the input, so the split
    // only has to happen once.
    std::array<std::vector<float>, 2> lowDelay, highDelay;
    int delayWritePos { 0 }, delayLength { 0 }, lookaheadSamples { 0 };

    // The adaptive threshold's slow reading of the band.
    std::array<float, 2> slowLevelDb { -100.0f, -100.0f };
    float slowRiseCoeff { 0.0f }, slowFallCoeff { 0.0f };

    std::atomic<float> reductionDb { 0.0f };
    std::atomic<float> detectorDb { -100.0f };
    std::atomic<float> effectiveThresholdDb { -24.0f };
    std::atomic<float> adaptiveReference { -30.0f };

    JUCE_LEAK_DETECTOR (EssEngine)
};
} // namespace nodo::ess
