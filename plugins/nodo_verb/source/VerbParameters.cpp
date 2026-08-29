#include "VerbParameters.h"

namespace nodo::verb
{
using Float = juce::AudioParameterFloat;
using Bool  = juce::AudioParameterBool;

int lineLengthSamples (int line, float size, double sampleRate)
{
    /*  The primes are lengths at 48 kHz; at any other rate they are scaled so
        the room is the same room. Scaling breaks their being prime, which does
        not matter — what matters is that they share no factors, and lengths
        that were prime and then multiplied by the same number still only share
        that number.
    */
    const auto base = (double) dsp::reverbPrimes[(size_t) juce::jlimit (0, 15, line)];
    const auto scaled = base * (sampleRate / 48000.0) * (double) juce::jlimit (0.2f, 4.0f, size);

    return juce::jmax (16, (int) std::round (scaled));
}

namespace
{
    std::unique_ptr<Float> makeParam (const juce::String& id,
                                      const juce::String& name,
                                      juce::NormalisableRange<float> range,
                                      float defaultValue,
                                      std::function<juce::String (float, int)> toText = nullptr,
                                      std::function<float (const juce::String&)> fromText = nullptr)
    {
        return std::make_unique<Float> (juce::ParameterID { id, 1 },
                                        name, range, defaultValue,
                                        juce::AudioParameterFloatAttributes()
                                            .withStringFromValueFunction (std::move (toText))
                                            .withValueFromStringFunction (std::move (fromText)));
    }

    juce::String secondsText (float seconds, int)
    {
        return seconds < 1.0f ? juce::String (juce::roundToInt (seconds * 1000.0f)) + " ms"
                              : juce::String (seconds, 2) + " s";
    }

    juce::String multiplierText (float value, int)
    {
        return juce::String (juce::roundToInt (value * 100.0f)) + " %";
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<Bool> (juce::ParameterID { ids::bypass, 1 }, "Bypass", false));

    layout.add (makeParam (ids::mix, "Mix",
                           juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.3f,
                           [] (float v, int) { return format::percent (v); }));

    layout.add (makeParam (ids::predelay, "Predelay",
                           ranges::timeMs (0.0f, 500.0f, 60.0f), 0.0f, format::milliseconds));

    layout.add (makeParam (ids::size, "Size",
                           juce::NormalisableRange<float> { 0.25f, 3.0f, 0.001f, 0.7f }, 1.0f,
                           multiplierText));

    // A tenth of a second to twenty. The bottom is a plate's early body and the
    // top is a cathedral you can hold a note in.
    layout.add (makeParam (ids::decay, "Decay",
                           juce::NormalisableRange<float> { 0.1f, 20.0f, 0.001f, 0.35f }, 2.0f,
                           secondsText));

    layout.add (makeParam (ids::decayLow, "Decay Low",
                           juce::NormalisableRange<float> { 0.12f, 2.0f, 0.001f, 0.7f }, 1.3f,
                           multiplierText));

    layout.add (makeParam (ids::decayLowFreq, "Decay Low Freq",
                           ranges::frequency (40.0f, 1500.0f), 250.0f,
                           format::frequency, format::frequencyFromText));

    layout.add (makeParam (ids::decayHigh, "Decay High",
                           juce::NormalisableRange<float> { 0.12f, 2.0f, 0.001f, 0.7f }, 0.5f,
                           multiplierText));

    layout.add (makeParam (ids::decayHighFreq, "Decay High Freq",
                           ranges::frequency (800.0f, 16000.0f), 4000.0f,
                           format::frequency, format::frequencyFromText));

    layout.add (makeParam (ids::diffusion, "Diffusion",
                           juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.7f,
                           [] (float v, int) { return format::percent (v); }));

    layout.add (makeParam (ids::motion, "Motion",
                           juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.25f,
                           [] (float v, int) { return format::percent (v); }));

    layout.add (makeParam (ids::width, "Width",
                           juce::NormalisableRange<float> { 0.0f, 2.0f, 0.001f }, 1.0f,
                           [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }));

    layout.add (std::make_unique<Bool> (juce::ParameterID { ids::freeze, 1 }, "Freeze", false));

    layout.add (makeParam (ids::preLowCut, "Pre Low Cut",
                           ranges::frequency (20.0f, 1000.0f), 100.0f,
                           [] (float v, int)
                           {
                               return v <= 20.5f ? juce::String ("Off") : format::frequency (v);
                           },
                           format::frequencyFromText));

    layout.add (makeParam (ids::preHighCut, "Pre High Cut",
                           ranges::frequency (1000.0f, 20000.0f), 12000.0f,
                           [] (float v, int)
                           {
                               return v >= 19500.0f ? juce::String ("Off") : format::frequency (v);
                           },
                           format::frequencyFromText));

    // ---- post EQ ----------------------------------------------------------
    layout.add (makeParam (ids::postLowFreq, "Post Low Freq",
                           ranges::frequency (30.0f, 1000.0f), 200.0f,
                           format::frequency, format::frequencyFromText));
    layout.add (makeParam (ids::postLowGain, "Post Low Gain",
                           ranges::gainDb (18.0f), 0.0f, format::decibels, format::decibelsFromText));

    layout.add (makeParam (ids::postMidFreq, "Post Mid Freq",
                           ranges::frequency (200.0f, 8000.0f), 1200.0f,
                           format::frequency, format::frequencyFromText));
    layout.add (makeParam (ids::postMidGain, "Post Mid Gain",
                           ranges::gainDb (18.0f), 0.0f, format::decibels, format::decibelsFromText));
    layout.add (makeParam (ids::postMidQ, "Post Mid Q",
                           ranges::quality(), 1.0f, format::quality));

    layout.add (makeParam (ids::postHighFreq, "Post High Freq",
                           ranges::frequency (1000.0f, 18000.0f), 6000.0f,
                           format::frequency, format::frequencyFromText));
    layout.add (makeParam (ids::postHighGain, "Post High Gain",
                           ranges::gainDb (18.0f), 0.0f, format::decibels, format::decibelsFromText));

    // ---- ducking ----------------------------------------------------------
    layout.add (makeParam (ids::duckAmount, "Duck",
                           juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.0f,
                           [] (float v, int) { return format::percent (v); }));

    layout.add (makeParam (ids::duckAttack, "Duck Attack",
                           ranges::timeMs (1.0f, 200.0f, 20.0f), 10.0f, format::milliseconds));

    layout.add (makeParam (ids::duckRelease, "Duck Release",
                           ranges::timeMs (20.0f, 2000.0f, 250.0f), 250.0f, format::milliseconds));

    return layout;
}

namespace
{
    float readValue (juce::AudioProcessorValueTreeState& state, const juce::String& id, float fallback)
    {
        if (auto* raw = state.getRawParameterValue (id))
            return raw->load();

        return fallback;
    }
}

VerbSettings readSettings (juce::AudioProcessorValueTreeState& state)
{
    VerbSettings s;

    s.mix        = readValue (state, ids::mix, 0.3f);
    s.predelayMs = readValue (state, ids::predelay, 0.0f);
    s.size       = readValue (state, ids::size, 1.0f);

    s.decay.midSeconds     = readValue (state, ids::decay, 2.0f);
    s.decay.lowMultiplier  = readValue (state, ids::decayLow, 1.3f);
    s.decay.lowFrequency   = readValue (state, ids::decayLowFreq, 250.0f);
    s.decay.highMultiplier = readValue (state, ids::decayHigh, 0.5f);
    s.decay.highFrequency  = readValue (state, ids::decayHighFreq, 4000.0f);

    s.diffusion = readValue (state, ids::diffusion, 0.7f);
    s.motion    = readValue (state, ids::motion, 0.25f);
    s.width     = readValue (state, ids::width, 1.0f);
    s.freeze    = readValue (state, ids::freeze, 0.0f) > 0.5f;

    s.preLowCutHz  = readValue (state, ids::preLowCut, 100.0f);
    s.preHighCutHz = readValue (state, ids::preHighCut, 12000.0f);

    s.postLowFreq  = readValue (state, ids::postLowFreq, 200.0f);
    s.postLowGain  = readValue (state, ids::postLowGain, 0.0f);
    s.postMidFreq  = readValue (state, ids::postMidFreq, 1200.0f);
    s.postMidGain  = readValue (state, ids::postMidGain, 0.0f);
    s.postMidQ     = readValue (state, ids::postMidQ, 1.0f);
    s.postHighFreq = readValue (state, ids::postHighFreq, 6000.0f);
    s.postHighGain = readValue (state, ids::postHighGain, 0.0f);

    s.duckAmount    = readValue (state, ids::duckAmount, 0.0f);
    s.duckAttackMs  = readValue (state, ids::duckAttack, 10.0f);
    s.duckReleaseMs = readValue (state, ids::duckRelease, 250.0f);

    return s;
}
} // namespace nodo::verb
