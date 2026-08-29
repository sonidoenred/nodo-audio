#pragma once

#include <juce_dsp/juce_dsp.h>
#include <nodo_core/nodo_core.h>

#include "VerbParameters.h"

namespace nodo::verb
{
/** The reverb.

    A feedback delay network: eight delay lines, an energy-preserving mixing
    matrix that hands every line a bit of every other one, and a filter on each
    line that decides how much comes back.

    The reason for that shape rather than the older comb-and-allpass one is the
    filters. Because the matrix neither adds nor removes energy, the decay is
    decided entirely by those eight filters — so a decay time that is different
    at different frequencies is not an effect layered on top, it is what the
    filters *are*. It also means the decay time is a number that can be designed
    and then measured, which for a reverb is unusual and is the whole reason
    this one can be verified without being heard.

    In front of the network, four allpasses per channel smear the input into a
    cloud. Behind it, an EQ and the mix.

    Nothing here allocates outside prepare().
*/
class VerbEngine
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    void setSettings (const VerbSettings& newSettings) { pending = newSettings; }

    void process (juce::dsp::AudioBlock<float>& block);

    /** GUI. How much the ducking is pulling the tail down right now, in dB at
        or below zero.
    */
    float getDuckingDb() const noexcept { return duckingDb.load (std::memory_order_relaxed); }

    /** GUI. The level of what is going round the network, so a frozen or nearly
        runaway tail is visible rather than only audible.
    */
    float getTailLevel() const noexcept { return tailLevel.load (std::memory_order_relaxed); }

private:
    void applyPendingSettings();

    struct Line
    {
        dsp::FractionalDelay buffer;
        dsp::Biquad lowShelf, highShelf;

        float targetSamples { 0.0f };
        float currentSamples { 0.0f };
        float gain { 0.0f };
        float lfoPhase { 0.0f };

        void reset() noexcept
        {
            buffer.reset();
            lowShelf.reset();
            highShelf.reset();
        }
    };

    struct Diffuser
    {
        std::array<dsp::Allpass, 4> stages;
        std::array<float, 4> lengths {};
        std::array<float, 4> phases {};

        void reset() noexcept
        {
            for (auto& stage : stages)
                stage.reset();
        }
    };

    VerbSettings settings, pending;
    bool settingsValid { false };

    double sampleRate { 44100.0 };
    int maxBlockSize { 512 };

    std::array<Line, numLines> lines;
    std::array<Diffuser, 2> diffusers;

    std::array<dsp::FractionalDelay, 2> predelay;
    float predelaySamples { 0.0f };

    std::array<dsp::Biquad, 2> preLowCut, preHighCut;
    bool preFiltersActive { false };

    std::array<dsp::Biquad, 2> postLow, postMid, postHigh;
    bool postActive { false };

    dsp::ModulationEnvelope duckEnvelope;

    juce::SmoothedValue<float> mixAmount, widthAmount;

    float motionIncrement { 0.0f };
    float sizeGlideCoeff { 0.0f };
    float levelCoeff { 0.0f };
    float tailFollower { 0.0f };

    std::atomic<float> duckingDb { 0.0f };
    std::atomic<float> tailLevel { 0.0f };

    JUCE_LEAK_DETECTOR (VerbEngine)
};
} // namespace nodo::verb
