#include "LimitParameters.h"

namespace nodo::limit
{
using Float = juce::AudioParameterFloat;

juce::StringArray styleNames()
{
    return { "Transparent", "Punchy", "Modern", "Bus", "Safe" };
}

juce::String styleDescription (LimitStyle style)
{
    switch (style)
    {
        case LimitStyle::transparent: return "Longest smoothing, slowest release. Gets out of the way.";
        case LimitStyle::punchy:      return "Short smoothing: transients keep their edge.";
        case LimitStyle::modern:      return "Fast and dense. What most masters are asking for.";
        case LimitStyle::bus:         return "Two stages: fast for the hits, slow for the section.";
        case LimitStyle::safe:        return "Barely moves. For catching accidents, not for loudness.";
    }

    return {};
}

StyleTraits styleTraits (LimitStyle style)
{
    switch (style)
    {
        case LimitStyle::transparent:
            return { 1.00f, 1.6f, 0.55f, 0.30f };

        case LimitStyle::punchy:
            // Half the window spent smoothing means a sharper corner into gain
            // reduction: the first instant of a transient survives, and you can
            // hear the limiter working. That is the trade this style is.
            return { 0.45f, 0.7f, 0.20f, 0.0f };

        case LimitStyle::modern:
            return { 0.70f, 0.5f, 0.35f, 0.20f };

        case LimitStyle::bus:
            return { 0.85f, 1.2f, 0.40f, 0.55f };

        case LimitStyle::safe:
            return { 1.00f, 2.2f, 0.70f, 0.35f };
    }

    return {};
}

juce::StringArray targetNames()
{
    return { "No target", "-9 Club", "-14 Streaming", "-16 Apple", "-23 Broadcast" };
}

float targetLufs (LoudnessTarget target)
{
    switch (target)
    {
        case LoudnessTarget::off:       return 0.0f;
        case LoudnessTarget::club:      return -9.0f;
        case LoudnessTarget::streaming: return -14.0f;   // Spotify, YouTube, Tidal
        case LoudnessTarget::apple:     return -16.0f;
        case LoudnessTarget::broadcast: return -23.0f;   // EBU R128
    }

    return 0.0f;
}

juce::StringArray ditherNames()
{
    return { "No dither", "Dither 16 bit", "Dither 24 bit" };
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

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ids::bypass, 1 }, "Bypass", false));

    // Input gain is how a limiter is driven: you push the signal into a fixed
    // ceiling rather than pulling a threshold down towards it.
    layout.add (makeParam (ids::inputGain, "Gain",
                           juce::NormalisableRange<float> { 0.0f, 24.0f, 0.01f }, 0.0f,
                           format::decibels, format::decibelsFromText));

    layout.add (makeParam (ids::ceiling, "Ceiling",
                           juce::NormalisableRange<float> { -12.0f, 0.0f, 0.01f }, -1.0f,
                           format::decibels, format::decibelsFromText));

    layout.add (makeParam (ids::lookahead, "Lookahead",
                           juce::NormalisableRange<float> { 0.1f, 20.0f, 0.01f }, 5.0f,
                           [] (float v, int) { return juce::String (v, 2) + " ms"; }));

    layout.add (makeParam (ids::release, "Release",
                           ranges::timeMs (1.0f, 2000.0f, 200.0f), 200.0f, format::milliseconds));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::style, 1 }, "Style", styleNames(), 0));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ids::truePeak, 1 }, "True Peak", true));

    layout.add (makeParam (ids::transientLink, "Transient Link",
                           juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 1.0f,
                           [] (float v, int) { return format::percent (v); }));

    layout.add (makeParam (ids::releaseLink, "Release Link",
                           juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 1.0f,
                           [] (float v, int) { return format::percent (v); }));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ids::dcFilter, 1 }, "DC Filter", false));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ids::externalTrigger, 1 }, "External Trigger", false));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ids::delta, 1 }, "Delta", false));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::dither, 1 }, "Dither", ditherNames(), 0));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::target, 1 }, "Loudness Target", targetNames(), 0));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ids::bypassMatch, 1 }, "Matched Bypass", true));

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

LimitSettings readSettings (juce::AudioProcessorValueTreeState& state)
{
    LimitSettings s;

    s.inputGainDb   = readValue (state, ids::inputGain, 0.0f);
    s.ceilingDb     = readValue (state, ids::ceiling, -1.0f);
    s.lookaheadMs   = readValue (state, ids::lookahead, 5.0f);
    s.releaseMs     = readValue (state, ids::release, 200.0f);
    s.style         = (LimitStyle) (int) readValue (state, ids::style, 0.0f);
    s.truePeak      = readValue (state, ids::truePeak, 1.0f) > 0.5f;
    s.transientLink = readValue (state, ids::transientLink, 1.0f);
    s.releaseLink   = readValue (state, ids::releaseLink, 1.0f);
    s.dcFilter      = readValue (state, ids::dcFilter, 0.0f) > 0.5f;
    s.externalTrigger = readValue (state, ids::externalTrigger, 0.0f) > 0.5f;
    s.delta         = readValue (state, ids::delta, 0.0f) > 0.5f;
    s.dither        = (DitherMode) (int) readValue (state, ids::dither, 0.0f);
    s.target        = (LoudnessTarget) (int) readValue (state, ids::target, 0.0f);
    s.bypassMatch   = readValue (state, ids::bypassMatch, 1.0f) > 0.5f;

    return s;
}
} // namespace nodo::limit
