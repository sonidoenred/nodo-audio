#include "GateParameters.h"

namespace nodo::gate
{
using Float = juce::AudioParameterFloat;
using Bool  = juce::AudioParameterBool;

juce::StringArray styleNames()
{
    return { "Clean", "Classic", "Vocal", "Percussive", "Smooth", "Bus" };
}

juce::StringArray directionNames()       { return { "Gate", "Ducking" }; }
juce::StringArray channelModeNames()     { return { "Linked", "Left / Right", "Mid / Side" }; }
juce::StringArray triggerNames()         { return { "Audio", "MIDI" }; }
juce::StringArray sidechainSourceNames() { return { "Internal", "External" }; }

juce::String styleDescription (GateStyle style)
{
    switch (style)
    {
        case GateStyle::classic:
            return "Peak detection with a release that slows as the gate closes. "
                   "The tail is longer than the knob says, which is what hardware does.";

        case GateStyle::vocal:
            return "Reads loudness rather than transients and adds 40 ms of hold. "
                   "Breaths and room stay out; the gate does not chatter between words.";

        case GateStyle::percussive:
            return "Fast hands and a short hold. For snare, toms and anything where "
                   "the point is the hit and not what is around it.";

        case GateStyle::smooth:
            return "Slow, heavily program dependent. This is an expander rather than "
                   "a gate: it makes the quiet parts quieter without ever slamming.";

        case GateStyle::bus:
            return "The gentlest of the six: a long window and two release stages. "
                   "For a whole mix or a room pair, where any hard move is audible.";

        case GateStyle::clean:
        default:
            return "Does exactly what the knobs say. Peak detection, no program "
                   "dependence, no hidden hold. The reference.";
    }
}

StyleTraits styleTraits (GateStyle style)
{
    switch (style)
    {
        case GateStyle::classic:    return { 1.0f,  1.2f,  10.0f, 1.0f, 0.5f, 0.0f,  0.0f };
        case GateStyle::vocal:      return { 1.4f,  1.5f,  40.0f, 1.6f, 0.4f, 0.0f, 15.0f };
        case GateStyle::percussive: return { 0.35f, 0.6f,   0.0f, 0.5f, 0.0f, 0.0f,  0.0f };
        case GateStyle::smooth:     return { 2.5f,  2.0f,  30.0f, 2.0f, 0.9f, 0.0f, 30.0f };
        case GateStyle::bus:        return { 3.0f,  2.5f,  60.0f, 2.2f, 0.7f, 0.6f, 50.0f };

        case GateStyle::clean:
        default:                    return { 1.0f,  1.0f,   0.0f, 1.0f, 0.0f, 0.0f,  0.0f };
    }
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
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<Bool> (juce::ParameterID { ids::bypass, 1 }, "Bypass", false));

    // Down to -90 dB because a gate's threshold lives near the noise floor, not
    // near the programme, and -60 is not always low enough for a quiet room.
    layout.add (makeParam (ids::threshold, "Threshold",
                           juce::NormalisableRange<float> { -90.0f, 0.0f, 0.01f }, -40.0f,
                           format::decibels, format::decibelsFromText));

    layout.add (makeParam (ids::ratio, "Ratio",
                           juce::NormalisableRange<float> { 1.0f, 200.0f, 0.01f, 0.22f }, 200.0f,
                           [] (float v, int)
                           {
                               return v >= 100.0f ? juce::String::fromUTF8 ("∞:1")
                                                  : juce::String (v, v < 10.0f ? 2 : 1) + ":1";
                           }));

    layout.add (makeParam (ids::range, "Range",
                           juce::NormalisableRange<float> { 0.0f, 90.0f, 0.1f }, 60.0f,
                           [] (float v, int) { return juce::String (v, 1) + " dB"; }));

    layout.add (makeParam (ids::attack, "Attack",
                           ranges::timeMs (0.05f, 500.0f, 5.0f), 1.0f, format::milliseconds));

    layout.add (makeParam (ids::hold, "Hold",
                           ranges::timeMs (0.0f, 1000.0f, 50.0f), 20.0f, format::milliseconds));

    layout.add (makeParam (ids::release, "Release",
                           ranges::timeMs (1.0f, 5000.0f, 200.0f), 150.0f, format::milliseconds));

    layout.add (makeParam (ids::knee, "Knee",
                           juce::NormalisableRange<float> { 0.0f, 30.0f, 0.1f }, 3.0f,
                           [] (float v, int) { return juce::String (v, 1) + " dB"; }));

    layout.add (makeParam (ids::hysteresis, "Hysteresis",
                           juce::NormalisableRange<float> { 0.0f, 24.0f, 0.1f }, 3.0f,
                           [] (float v, int)
                           {
                               return v < 0.05f ? juce::String ("Off")
                                                : juce::String (v, 1) + " dB";
                           }));

    layout.add (makeParam (ids::lookahead, "Lookahead",
                           juce::NormalisableRange<float> { 0.0f, 10.0f, 0.01f }, 0.0f,
                           [] (float v, int)
                           {
                               return v < 0.005f ? juce::String ("Off")
                                                 : juce::String (v, 2) + " ms";
                           }));

    layout.add (makeParam (ids::mix, "Mix",
                           juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 1.0f,
                           [] (float v, int) { return format::percent (v); }));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::style, 1 }, "Style", styleNames(), 0));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::direction, 1 }, "Direction", directionNames(), 0));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::channelMode, 1 }, "Channel Mode", channelModeNames(), 0));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::trigger, 1 }, "Trigger", triggerNames(), 0));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::scSource, 1 }, "Sidechain", sidechainSourceNames(), 0));

    layout.add (makeParam (ids::scHighpass, "SC Highpass",
                           ranges::frequency (20.0f, 2000.0f), 20.0f,
                           [] (float v, int)
                           {
                               return v <= 20.5f ? juce::String ("Off") : format::frequency (v);
                           },
                           format::frequencyFromText));

    layout.add (makeParam (ids::scLowpass, "SC Lowpass",
                           ranges::frequency (200.0f, 20000.0f), 20000.0f,
                           [] (float v, int)
                           {
                               return v >= 19500.0f ? juce::String ("Off") : format::frequency (v);
                           },
                           format::frequencyFromText));

    layout.add (std::make_unique<Bool> (juce::ParameterID { ids::scListen, 1 },
                                        "SC Listen", false));

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

GateSettings readSettings (juce::AudioProcessorValueTreeState& state)
{
    GateSettings s;

    s.thresholdDb  = readValue (state, ids::threshold, -40.0f);
    s.ratio        = readValue (state, ids::ratio, 200.0f);
    s.rangeDb      = readValue (state, ids::range, 60.0f);
    s.attackMs     = readValue (state, ids::attack, 1.0f);
    s.holdMs       = readValue (state, ids::hold, 20.0f);
    s.releaseMs    = readValue (state, ids::release, 150.0f);
    s.kneeDb       = readValue (state, ids::knee, 3.0f);
    s.hysteresisDb = readValue (state, ids::hysteresis, 3.0f);
    s.lookaheadMs  = readValue (state, ids::lookahead, 0.0f);
    s.mix          = readValue (state, ids::mix, 1.0f);

    s.style       = (GateStyle)   (int) readValue (state, ids::style, 0.0f);
    s.direction   = (Direction)   (int) readValue (state, ids::direction, 0.0f);
    s.channelMode = (ChannelMode) (int) readValue (state, ids::channelMode, 0.0f);
    s.trigger     = (Trigger)     (int) readValue (state, ids::trigger, 0.0f);

    s.sidechain    = (SidechainSource) (int) readValue (state, ids::scSource, 0.0f);
    s.scHighpassHz = readValue (state, ids::scHighpass, 20.0f);
    s.scLowpassHz  = readValue (state, ids::scLowpass, 20000.0f);
    s.scListen     = readValue (state, ids::scListen, 0.0f) > 0.5f;

    return s;
}
} // namespace nodo::gate
