#pragma once

#include <juce_dsp/juce_dsp.h>
#include <nodo_core/nodo_core.h>

#include "CompParameters.h"

namespace nodo::comp
{
/** The compressor itself.

    Feed-forward by default: the detector reads the input, the gain computer
    turns a level into a wanted reduction, and the smoother decides how fast the
    gain is allowed to move. The Opto and Bus styles switch the detector over to
    the output instead, which is what makes them behave like hardware that can
    only respond to what it has already let past.

    Nothing here allocates. Every buffer is sized in prepare(), the sidechain
    filters are plain biquads from nodo_core, and the per-sample path is
    arithmetic and two transcendentals.
*/
class CompEngine
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    /** Message thread writes, audio thread reads at the top of each block. */
    void setSettings (const CompSettings& newSettings) { pending = newSettings; }

    /** Samples of lookahead currently in use. The processor reports this to the
        host so the delay is compensated.
    */
    int getLatencySamples() const noexcept { return lookaheadSamples; }

    /** Recomputes the lookahead from the pending settings. Called outside the
        audio thread so latency changes do not surprise the host mid-block.
    */
    int computeLatencySamples() const noexcept;

    /** sidechain may be null; when it is, an external sidechain request falls
        back to the input, which is what a host that has not connected the bus
        will give us anyway.
    */
    void process (juce::dsp::AudioBlock<float>& block,
                  const juce::dsp::AudioBlock<const float>* sidechain);

    /** GUI thread. Current reduction, at or below zero. */
    float getGainReductionDb() const noexcept { return reductionDb.load (std::memory_order_relaxed); }

    /** GUI thread. What the detector is seeing right now, in dB. */
    float getDetectorLevelDb() const noexcept { return detectorDb.load (std::memory_order_relaxed); }

    /** The static curve, for drawing and for tests: input level in, output level
        out, including makeup and mix but not the smoother.
    */
    static float staticOutputDb (float inputDb, const CompSettings& settings);

private:
    void applyPendingSettings();
    void updateSidechainFilters();

    CompSettings settings, pending;
    bool settingsValid { false };

    double sampleRate { 44100.0 };
    int maxBlockSize { 512 };
    int numChannels { 2 };

    // Lookahead. A plain ring buffer per channel: the delay is a whole number of
    // samples, so there is no interpolation to get wrong.
    std::array<std::vector<float>, 2> delayBuffer;
    int delayWritePos { 0 };
    int delayLength { 0 };
    int lookaheadSamples { 0 };

    std::array<dsp::RmsFollower, 2> rmsFollowers;

    // Auto threshold: a slow reading of how loud the material is, so the
    // threshold can sit a fixed distance under it instead of at a fixed number.
    float slowLevelDb { -100.0f };
    float slowRiseCoeff { 0.0f }, slowFallCoeff { 0.0f };
    std::array<dsp::GainSmoother, 2> smoothers;
    std::array<dsp::Biquad, 2> scHighpass, scLowpass;
    bool scFiltersActive { false };

    // Feedback styles need the previous output sample as their detector input.
    std::array<float, 2> feedbackMemory { 0.0f, 0.0f };

    juce::SmoothedValue<float> makeupGain, mixAmount;

    // Auto gain follows the reduction being applied, slowly, and gives it back.
    float autoGainDb { 0.0f };
    float autoGainCoeff { 0.0f };

    std::atomic<float> reductionDb { 0.0f };
    std::atomic<float> detectorDb { -100.0f };

    JUCE_LEAK_DETECTOR (CompEngine)
};
} // namespace nodo::comp
