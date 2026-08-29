#include "CompParameters.h"

namespace nodo::comp
{
using Float = juce::AudioParameterFloat;

juce::StringArray styleNames()
{
    return { "Clean", "Punch", "Opto", "Vocal", "Bus",
             "Classic", "Master", "Smooth", "Pump" };
}

juce::StringArray detectionNames()
{
    return { "Peak", "RMS" };
}

juce::StringArray sidechainSourceNames()
{
    return { "Internal", "External" };
}

juce::StringArray directionNames()
{
    return { "Downward", "Upward" };
}

juce::String styleDescription (CompStyle style)
{
    switch (style)
    {
        case CompStyle::clean: return "Does exactly what the knobs say.";
        case CompStyle::punch: return "Harder knee, quicker hands. For transients.";
        case CompStyle::opto:  return "Soft and slow, listening to its own output.";
        case CompStyle::vocal: return "Wide knee, release that follows the phrase.";
        case CompStyle::bus:   return "Two release stages. Glue, not control.";
        case CompStyle::classic:   return "Feedback detection, moderate hand. The all rounder.";
        case CompStyle::mastering: return "Widest knee, slowest hands. Two dB and no more.";
        case CompStyle::smooth:    return "Rides the level instead of catching peaks.";
        case CompStyle::pumping:   return "Very short release, hard knee. The effect, on purpose.";
    }

    return {};
}

StyleTraits styleTraits (CompStyle style)
{
    switch (style)
    {
        case CompStyle::clean:
            return { 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, false };

        case CompStyle::punch:
            // Half the knee and a faster attack than asked for: the point of this
            // style is that the transient gets through before the gain moves.
            return { 0.5f, 0.6f, 0.8f, 0.25f, 0.0f, 0.6f, false };

        case CompStyle::opto:
            // Feedback detection is what makes an optical compressor forgiving:
            // it can only react to what it has already let past, so it never
            // clamps as hard as the numbers suggest.
            return { 2.5f, 1.6f, 2.0f, 0.80f, 0.35f, 1.6f, true };

        case CompStyle::vocal:
            return { 1.8f, 1.0f, 1.2f, 0.45f, 0.20f, 1.0f, false };

        case CompStyle::bus:
            return { 2.2f, 1.3f, 1.5f, 0.35f, 0.50f, 1.3f, true };

        case CompStyle::classic:
            // The general purpose feedback compressor: enough program
            // dependence to sound alive, not enough to feel out of control.
            return { 1.2f, 1.0f, 1.0f, 0.35f, 0.15f, 1.0f, true };

        case CompStyle::mastering:
            // Everything widened and slowed. A mastering compressor that can be
            // heard working is a mastering compressor set wrong.
            return { 3.0f, 1.6f, 1.8f, 0.55f, 0.40f, 1.6f, false };

        case CompStyle::smooth:
            return { 2.0f, 1.4f, 1.6f, 0.75f, 0.30f, 1.2f, false };

        case CompStyle::pumping:
            // A short release against a hard knee is exactly the thing every
            // other style is shaped to avoid, which is why it gets its own name
            // instead of being an accident.
            return { 0.3f, 0.5f, 0.30f, 0.0f, 0.0f, 0.5f, false };
    }

    return {};
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
                                            name,
                                            range,
                                            defaultValue,
                                            juce::AudioParameterFloatAttributes()
                                                .withStringFromValueFunction (std::move (toText))
                                                .withValueFromStringFunction (std::move (fromText)));
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ids::bypass, 1 }, "Bypass", false));

    layout.add (makeParam (ids::threshold, "Threshold",
                           juce::NormalisableRange<float> { -60.0f, 0.0f, 0.01f },
                           -18.0f, format::decibels, format::decibelsFromText));

    layout.add (makeParam (ids::ratio, "Ratio", ranges::ratio(), 4.0f, format::ratio));

    layout.add (makeParam (ids::knee, "Knee",
                           juce::NormalisableRange<float> { 0.0f, 30.0f, 0.1f },
                           6.0f,
                           [] (float v, int) { return juce::String (v, 1) + " dB"; }));

    layout.add (makeParam (ids::attack, "Attack",
                           ranges::timeMs (0.05f, 250.0f, 10.0f), 10.0f, format::milliseconds));

    layout.add (makeParam (ids::release, "Release",
                           ranges::timeMs (5.0f, 2500.0f, 200.0f), 150.0f, format::milliseconds));

    layout.add (makeParam (ids::hold, "Hold",
                           juce::NormalisableRange<float> { 0.0f, 500.0f, 0.1f }, 0.0f,
                           [] (float v, int)
                           {
                               return v < 0.05f ? juce::String ("Off")
                                                : format::milliseconds (v);
                           }));

    layout.add (makeParam (ids::range, "Range",
                           juce::NormalisableRange<float> { 0.0f, 60.0f, 0.1f }, 60.0f,
                           [] (float v, int)
                           {
                               return v >= 59.5f ? juce::String ("Full")
                                                 : juce::String (v, 1) + " dB";
                           }));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::direction, 1 }, "Direction", directionNames(), 0));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ids::autoThreshold, 1 }, "Auto Threshold", false));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ids::autoRelease, 1 }, "Auto Release", false));

    layout.add (makeParam (ids::makeup, "Makeup", ranges::gainDb (24.0f), 0.0f,
                           format::decibels, format::decibelsFromText));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ids::autoGain, 1 }, "Auto Gain", false));

    layout.add (makeParam (ids::mix, "Mix",
                           juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 1.0f,
                           [] (float v, int) { return format::percent (v); }));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::style, 1 }, "Style", styleNames(), 0));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::detection, 1 }, "Detection", detectionNames(), 0));

    layout.add (makeParam (ids::stereoLink, "Stereo Link",
                           juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 1.0f,
                           [] (float v, int) { return format::percent (v); }));

    layout.add (makeParam (ids::lookahead, "Lookahead",
                           juce::NormalisableRange<float> { 0.0f, 20.0f, 0.01f }, 0.0f,
                           [] (float v, int)
                           {
                               return v < 0.005f ? juce::String ("Off")
                                                 : juce::String (v, 2) + " ms";
                           }));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::scSource, 1 }, "Sidechain", sidechainSourceNames(), 0));

    layout.add (makeParam (ids::scHighpass, "SC High Pass",
                           ranges::frequency (20.0f, 2000.0f), 20.0f,
                           [] (float v, int)
                           {
                               return v <= 20.5f ? juce::String ("Off") : format::frequency (v);
                           },
                           format::frequencyFromText));

    layout.add (makeParam (ids::scLowpass, "SC Low Pass",
                           ranges::frequency (200.0f, 20000.0f), 20000.0f,
                           [] (float v, int)
                           {
                               return v >= 19500.0f ? juce::String ("Off") : format::frequency (v);
                           },
                           format::frequencyFromText));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ids::scListen, 1 }, "SC Listen", false));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::oversampling, 1 }, "Oversampling",
        juce::StringArray { "Off", "2x" }, 0));

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

CompSettings readSettings (juce::AudioProcessorValueTreeState& state)
{
    CompSettings s;

    s.thresholdDb = readValue (state, ids::threshold, -18.0f);
    s.ratio       = readValue (state, ids::ratio, 4.0f);
    s.kneeDb      = readValue (state, ids::knee, 6.0f);
    s.attackMs    = readValue (state, ids::attack, 10.0f);
    s.releaseMs   = readValue (state, ids::release, 150.0f);
    s.holdMs      = readValue (state, ids::hold, 0.0f);
    s.rangeDb     = readValue (state, ids::range, 60.0f);
    s.direction   = (Direction) (int) readValue (state, ids::direction, 0.0f);
    s.autoThreshold = readValue (state, ids::autoThreshold, 0.0f) > 0.5f;
    s.autoRelease   = readValue (state, ids::autoRelease, 0.0f) > 0.5f;
    s.makeupDb    = readValue (state, ids::makeup, 0.0f);
    s.autoGain    = readValue (state, ids::autoGain, 0.0f) > 0.5f;
    s.mix         = readValue (state, ids::mix, 1.0f);
    s.style       = (CompStyle) (int) readValue (state, ids::style, 0.0f);
    s.detection   = (Detection) (int) readValue (state, ids::detection, 0.0f);
    s.stereoLink  = readValue (state, ids::stereoLink, 1.0f);
    s.lookaheadMs = readValue (state, ids::lookahead, 0.0f);
    s.sidechain   = (SidechainSource) (int) readValue (state, ids::scSource, 0.0f);
    s.scHighpassHz = readValue (state, ids::scHighpass, 20.0f);
    s.scLowpassHz  = readValue (state, ids::scLowpass, 20000.0f);
    s.scListen     = readValue (state, ids::scListen, 0.0f) > 0.5f;

    return s;
}
} // namespace nodo::comp
