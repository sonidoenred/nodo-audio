#pragma once

#include <juce_dsp/juce_dsp.h>
#include <nodo_core/nodo_core.h>

#include "LimitParameters.h"

namespace nodo::limit
{
/** The limiter.

    A limiter is not a compressor with the ratio turned up. A compressor reacts
    to a peak that has already arrived and lets its first samples through; that
    is fine when you are shaping dynamics and useless when the job is "nothing
    leaves this plugin above the ceiling". So the audio is delayed, and the gain
    is computed from the future:

      required gain  ->  sliding minimum over the lookahead window
                     ->  release envelope (instant down, exponential up)
                     ->  two moving averages (a smooth ramp, no corners)
                     ->  safety minimum against the sliding minimum

    The sliding minimum is what makes the gain arrive early: a peak at time p
    pulls the gain down from time p onwards, while the audio for time p does not
    come out until p + lookahead. By the time it does, the gain has already
    ramped all the way down, smoothly, and the peak lands exactly on the ceiling
    instead of over it.

    There is a hard clip at the ceiling after all of that. It should never do
    anything — and getSafetyClipCount() reports whether it did, so the tests can
    insist on that rather than assume it.
*/
class LimitEngine
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    void setSettings (const LimitSettings& newSettings) { pending = newSettings; }

    /** Samples of delay this configuration needs: the lookahead, plus whatever
        the true peak detector costs.
    */
    int computeLatencySamples() const noexcept;
    int getLatencySamples() const noexcept { return latencySamples; }

    void process (juce::dsp::AudioBlock<float>& block,
                  const juce::dsp::AudioBlock<const float>* sidechain);

    /** GUI. At or below zero. */
    float getGainReductionDb() const noexcept { return reductionDb.load (std::memory_order_relaxed); }

    /** GUI. Highest output peak seen since the last reset, in dBTP when the
        true peak detector is on and dBFS when it is not.
    */
    float getPeakDb() const noexcept { return peakDb.load (std::memory_order_relaxed); }
    void clearPeak() noexcept { peakDb.store (-200.0f, std::memory_order_relaxed); }

    /** The reduction the limiter has been applying lately, averaged over a
        second and a half. This is what a matched bypass has to give back: the
        dry signal is louder than the limited one by the input gain minus this.
    */
    float getAverageReductionDb() const noexcept { return averageReductionDb.load (std::memory_order_relaxed); }

    /** How many samples the safety clipper has had to touch. Zero is the only
        acceptable answer for a correct gain path.
    */
    juce::int64 getSafetyClipCount() const noexcept { return clipCount.load (std::memory_order_relaxed); }
    void clearSafetyClipCount() noexcept { clipCount.store (0, std::memory_order_relaxed); }

private:
    void applyPendingSettings();
    void measureTruePeaks (int numSamples, int channels);

    LimitSettings settings, pending;
    bool settingsValid { false };

    double sampleRate { 44100.0 };
    int maxBlockSize { 512 };

    // Audio delay: the whole point of the plugin.
    std::array<std::vector<float>, 2> delayBuffer;
    int delayWritePos { 0 }, delayLength { 0 };
    int lookaheadSamples { 0 }, latencySamples { 0 }, truePeakLatency { 0 };

    std::array<dsp::SlidingMinimum, 2> minimumHold;
    std::array<dsp::MovingAverage, 2> smoother1, smoother2;
    std::array<dsp::Biquad, 2> dcBlocker;

    std::array<float, 2> releaseEnvelope { 1.0f, 1.0f };
    std::array<float, 2> slowEnvelope { 1.0f, 1.0f };
    float releaseCoeff { 0.0f }, slowCoeff { 0.0f };

    // Detector, and its four times oversampled copy for inter-sample peaks.
    juce::AudioBuffer<float> detectorBuffer, peakBuffer;
    std::unique_ptr<juce::dsp::Oversampling<float>> truePeakOversampler;

    juce::SmoothedValue<float> inputGain;
    juce::Random ditherNoise { 20260816 };

    std::atomic<float> reductionDb { 0.0f };
    std::atomic<float> averageReductionDb { 0.0f };
    float runningReduction { 0.0f }, averagingCoeff { 0.0f };
    std::atomic<float> peakDb { -200.0f };
    std::atomic<juce::int64> clipCount { 0 };

    JUCE_LEAK_DETECTOR (LimitEngine)
};
} // namespace nodo::limit
