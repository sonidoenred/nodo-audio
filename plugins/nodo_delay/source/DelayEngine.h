#pragma once

#include <juce_dsp/juce_dsp.h>
#include <nodo_core/nodo_core.h>

#include "DelayParameters.h"

namespace nodo::delay
{
/** The delay itself.

    Two lines, each with its own time, its own place in the stereo field and its
    own path back into the loop. Everything interesting about a delay happens in
    that loop: the filters, the saturation and the degradation all sit inside
    it, so every repeat is one generation further from the original rather than
    all of them being processed the same amount on the way out. That is the
    difference between a delay that decays into something and one that just gets
    quieter.

    Nothing here allocates outside prepare().
*/
class DelayEngine
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    void setSettings (const DelaySettings& newSettings) { pending = newSettings; }

    void process (juce::dsp::AudioBlock<float>& block);

    /** GUI. The time each line is actually running at right now, in ms, which
        is not the knob when the delay is synced and not the target when the
        tape mode is still sliding towards it.
    */
    float getCurrentTimeMs (int line) const noexcept
    {
        return currentTimeMs[(size_t) juce::jlimit (0, 1, line)].load (std::memory_order_relaxed);
    }

    /** GUI. How much signal is going round the loop, 0 to 1ish. Worth showing:
        it is the number that tells you whether a high feedback setting is about
        to sustain forever, and it is not the feedback knob.
    */
    float getLoopLevel (int line) const noexcept
    {
        return loopLevel[(size_t) juce::jlimit (0, 1, line)].load (std::memory_order_relaxed);
    }

    /** GUI. What each modulation source is putting out right now: the LFOs from
        -1 to 1, the envelope from 0 to 1. A modulation matrix whose sources you
        cannot see is a matrix you set by trial and error.
    */
    float getModulationSource (int index) const noexcept
    {
        return sourceValue[(size_t) juce::jlimit (0, 2, index)].load (std::memory_order_relaxed);
    }

private:
    void applyPendingSettings();

    struct Line
    {
        dsp::FractionalDelay buffer;
        dsp::Biquad highpass, lowpass;
        dsp::SoftSaturator saturator;
        dsp::BitCrusher crusher;

        /** Where the read head is, in samples. In tape mode this slides towards
            the target and the pitch of the tail bends while it does; in fade
            mode it jumps and the two positions are crossfaded.
        */
        float currentSamples { 0.0f };
        float previousSamples { 0.0f };
        float fadePosition { 1.0f };

        float lfoPhase { 0.0f };
        float level { 0.0f };

        void reset() noexcept
        {
            buffer.reset();
            highpass.reset();
            lowpass.reset();
            saturator.reset();
            crusher.reset();
            previousSamples = currentSamples;
            fadePosition = 1.0f;
            level = 0.0f;
        }
    };

    DelaySettings settings, pending;
    bool settingsValid { false };

    double sampleRate { 44100.0 };
    int maxDelaySamples { 0 };

    std::array<Line, 2> lines;
    bool filtersActive { false };

    juce::SmoothedValue<float> mixAmount, feedbackAmount, crossAmount, widthAmount;

    std::array<dsp::Lfo, numLfos> lfos;
    dsp::ModulationEnvelope envelope;
    bool modActive { false };

    float fadeIncrement { 0.0f };
    float timeGlideCoeff { 0.0f };
    float levelCoeff { 0.0f };

    std::array<std::atomic<float>, 2> currentTimeMs { 375.0f, 500.0f };
    std::array<std::atomic<float>, 2> loopLevel { 0.0f, 0.0f };
    std::array<std::atomic<float>, 3> sourceValue { 0.0f, 0.0f, 0.0f };

    JUCE_LEAK_DETECTOR (DelayEngine)
};
} // namespace nodo::delay
