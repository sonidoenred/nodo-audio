#include "EssParameters.h"

namespace nodo::ess
{
using Float = juce::AudioParameterFloat;

juce::StringArray modeNames()      { return { "Wideband", "Split band" }; }
juce::StringArray detectionNames() { return { "Absolute", "Adaptive" }; }
juce::StringArray monitorNames()   { return { "Normal", "Listen band", "Listen difference" }; }
juce::StringArray channelNames()   { return { "Stereo", "Mid only", "Side only" }; }

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

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ids::bypass, 1 }, "Bypass", false));

    // Sibilance lives between about 5 and 10 kHz on most voices; the range goes
    // wider so a dark voice and a bright one both have somewhere to sit.
    layout.add (makeParam (ids::frequency, "Frequency",
                           ranges::frequency (2000.0f, 16000.0f), 6500.0f,
                           format::frequency, format::frequencyFromText));

    layout.add (makeParam (ids::detectorTop, "Detector Top",
                           ranges::frequency (4000.0f, 20000.0f), 20000.0f,
                           [] (float v, int)
                           {
                               return v >= 19500.0f ? juce::String ("Off") : format::frequency (v);
                           },
                           format::frequencyFromText));

    layout.add (makeParam (ids::threshold, "Threshold",
                           juce::NormalisableRange<float> { -60.0f, 0.0f, 0.01f }, -24.0f,
                           format::decibels, format::decibelsFromText));

    layout.add (makeParam (ids::range, "Range",
                           juce::NormalisableRange<float> { 0.0f, 30.0f, 0.1f }, 12.0f,
                           [] (float v, int) { return juce::String (v, 1) + " dB"; }));

    layout.add (makeParam (ids::attack, "Attack",
                           ranges::timeMs (0.1f, 20.0f, 2.0f), 1.0f, format::milliseconds));

    layout.add (makeParam (ids::release, "Release",
                           ranges::timeMs (5.0f, 500.0f, 80.0f), 60.0f, format::milliseconds));

    layout.add (makeParam (ids::lookahead, "Lookahead",
                           juce::NormalisableRange<float> { 0.0f, 15.0f, 0.01f }, 0.0f,
                           [] (float v, int)
                           {
                               return v < 0.005f ? juce::String ("Off")
                                                 : juce::String (v, 2) + " ms";
                           }));

    layout.add (makeParam (ids::knee, "Knee",
                           juce::NormalisableRange<float> { 0.0f, 18.0f, 0.1f }, 4.0f,
                           [] (float v, int) { return juce::String (v, 1) + " dB"; }));

    layout.add (makeParam (ids::stereoLink, "Stereo Link",
                           juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 1.0f,
                           [] (float v, int) { return format::percent (v); }));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::mode, 1 }, "Mode", modeNames(), 1));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::detection, 1 }, "Detection", detectionNames(), 1));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::channel, 1 }, "Channel", channelNames(), 0));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::monitor, 1 }, "Monitor", monitorNames(), 0));

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

EssSettings readSettings (juce::AudioProcessorValueTreeState& state)
{
    EssSettings s;

    s.frequencyHz = readValue (state, ids::frequency, 6500.0f);
    s.detectorTopHz = readValue (state, ids::detectorTop, 20000.0f);
    s.channel     = (Channel) (int) readValue (state, ids::channel, 0.0f);
    s.thresholdDb = readValue (state, ids::threshold, -24.0f);
    s.rangeDb     = readValue (state, ids::range, 12.0f);
    s.attackMs    = readValue (state, ids::attack, 1.0f);
    s.releaseMs   = readValue (state, ids::release, 60.0f);
    s.lookaheadMs = readValue (state, ids::lookahead, 0.0f);
    s.kneeDb      = readValue (state, ids::knee, 4.0f);
    s.stereoLink  = readValue (state, ids::stereoLink, 1.0f);
    s.mode        = (Mode) (int) readValue (state, ids::mode, 1.0f);
    s.detection   = (Detection) (int) readValue (state, ids::detection, 1.0f);
    s.monitor     = (Monitor) (int) readValue (state, ids::monitor, 0.0f);

    return s;
}
} // namespace nodo::ess
