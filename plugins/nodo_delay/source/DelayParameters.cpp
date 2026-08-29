#include "DelayParameters.h"

namespace nodo::delay
{
using Float = juce::AudioParameterFloat;
using Bool  = juce::AudioParameterBool;

juce::StringArray routingNames()  { return { "Stereo", "Ping-pong", "Mono" }; }
juce::StringArray timeModeNames() { return { "Tape", "Fade" }; }

juce::StringArray noteValueNames()
{
    return { "1/16T", "1/16", "1/16.", "1/8T", "1/8", "1/8.",
             "1/4T", "1/4", "1/4.", "1/2", "1/2.", "1/1" };
}

/*  "Off" rather than a dash. A dash in a plain string literal is not ASCII
    and came out of the combo box as mojibake; it also reads worse than the
    word does.
*/
juce::StringArray modSourceNames() { return { "Off", "LFO 1", "LFO 2", "Envelope" }; }

juce::StringArray modTargetNames()
{
    return { "Off", "Time", "Time L", "Time R", "Feedback", "Cross", "Drive", "Lo-Fi",
             "Loop HP", "Loop LP", "Mix", "Width", "Tap spread", "Tap level" };
}

juce::StringArray lfoShapeNames()
{
    return { "Sine", "Triangle", "Saw", "Ramp", "Square", "Random", "Random smooth" };
}

namespace ids
{
    juce::String tapOn    (int index) { return "tapOn"    + juce::String (index); }
    juce::String tapLevel (int index) { return "tapLevel" + juce::String (index); }
    juce::String tapPan   (int index) { return "tapPan"   + juce::String (index); }

    juce::String lfoRate  (int lfo) { return "lfo" + juce::String (lfo) + "Rate"; }
    juce::String lfoSync  (int lfo) { return "lfo" + juce::String (lfo) + "Sync"; }
    juce::String lfoNote  (int lfo) { return "lfo" + juce::String (lfo) + "Note"; }
    juce::String lfoShape (int lfo) { return "lfo" + juce::String (lfo) + "Shape"; }
    juce::String lfoPhase (int lfo) { return "lfo" + juce::String (lfo) + "Phase"; }

    juce::String modSource (int slot) { return "mod" + juce::String (slot) + "Source"; }
    juce::String modTarget (int slot) { return "mod" + juce::String (slot) + "Target"; }
    juce::String modAmount (int slot) { return "mod" + juce::String (slot) + "Amount"; }
}

double noteValueInBeats (NoteValue value)
{
    // A beat is a quarter note, which is what every host means by BPM.
    switch (value)
    {
        case NoteValue::sixteenthTriplet: return 0.25 * 2.0 / 3.0;
        case NoteValue::sixteenth:        return 0.25;
        case NoteValue::sixteenthDotted:  return 0.25 * 1.5;
        case NoteValue::eighthTriplet:    return 0.5 * 2.0 / 3.0;
        case NoteValue::eighth:           return 0.5;
        case NoteValue::eighthDotted:     return 0.5 * 1.5;
        case NoteValue::quarterTriplet:   return 1.0 * 2.0 / 3.0;
        case NoteValue::quarter:          return 1.0;
        case NoteValue::quarterDotted:    return 1.5;
        case NoteValue::half:             return 2.0;
        case NoteValue::halfDotted:       return 3.0;
        case NoteValue::whole:            return 4.0;
    }

    return 1.0;
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

    juce::String panText (float value, int)
    {
        if (std::abs (value) < 0.005f)
            return "C";

        const auto amount = juce::roundToInt (std::abs (value) * 100.0f);
        return (value < 0.0f ? "L " : "R ") + juce::String (amount);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<Bool> (juce::ParameterID { ids::bypass, 1 }, "Bypass", false));

    // Five milliseconds to five seconds, skewed so the musical middle — the
    // hundred to six hundred where nearly every delay lives — gets most of the
    // travel instead of being squeezed against the bottom stop.
    const auto timeRange = juce::NormalisableRange<float> { 5.0f, 5000.0f, 0.01f, 0.28f };

    layout.add (makeParam (ids::timeLeft,  "Time L", timeRange, 375.0f, format::milliseconds));
    layout.add (makeParam (ids::timeRight, "Time R", timeRange, 500.0f, format::milliseconds));

    layout.add (std::make_unique<Bool> (juce::ParameterID { ids::sync, 1 }, "Sync", false));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::noteLeft, 1 }, "Note L", noteValueNames(),
        (int) NoteValue::eighthDotted));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::noteRight, 1 }, "Note R", noteValueNames(),
        (int) NoteValue::quarter));

    layout.add (std::make_unique<Bool> (juce::ParameterID { ids::linkTimes, 1 }, "Link", false));

    // Past 100 % the loop sustains rather than decaying. Allowed on purpose, and
    // caught by a soft clipper in the loop so it lands on a tone instead of
    // running away.
    layout.add (makeParam (ids::feedback, "Feedback",
                           juce::NormalisableRange<float> { 0.0f, 1.1f, 0.001f }, 0.35f,
                           [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }));

    layout.add (makeParam (ids::cross, "Cross",
                           juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.0f,
                           [] (float v, int) { return format::percent (v); }));

    layout.add (std::make_unique<Bool> (juce::ParameterID { ids::invert, 1 },
                                        "Invert Feedback", false));
    layout.add (std::make_unique<Bool> (juce::ParameterID { ids::freeze, 1 }, "Freeze", false));

    layout.add (makeParam (ids::highpass, "Loop Highpass",
                           ranges::frequency (20.0f, 2000.0f), 20.0f,
                           [] (float v, int)
                           {
                               return v <= 20.5f ? juce::String ("Off") : format::frequency (v);
                           },
                           format::frequencyFromText));

    layout.add (makeParam (ids::lowpass, "Loop Lowpass",
                           ranges::frequency (200.0f, 20000.0f), 20000.0f,
                           [] (float v, int)
                           {
                               return v >= 19500.0f ? juce::String ("Off") : format::frequency (v);
                           },
                           format::frequencyFromText));

    layout.add (makeParam (ids::drive, "Drive",
                           juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.0f,
                           [] (float v, int) { return format::percent (v); }));

    layout.add (makeParam (ids::loFi, "Lo-Fi",
                           juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.0f,
                           [] (float v, int) { return format::percent (v); }));

    layout.add (makeParam (ids::modRate, "Mod Rate",
                           juce::NormalisableRange<float> { 0.02f, 10.0f, 0.001f, 0.35f }, 0.5f,
                           [] (float v, int) { return juce::String (v, 2) + " Hz"; }));

    layout.add (makeParam (ids::modDepth, "Mod Depth",
                           juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.0f,
                           [] (float v, int) { return format::percent (v); }));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::routing, 1 }, "Routing", routingNames(), 0));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::timeMode, 1 }, "Time Mode", timeModeNames(), 0));

    layout.add (makeParam (ids::panLeft, "Pan L",
                           juce::NormalisableRange<float> { -1.0f, 1.0f, 0.001f }, -1.0f, panText));

    layout.add (makeParam (ids::panRight, "Pan R",
                           juce::NormalisableRange<float> { -1.0f, 1.0f, 0.001f }, 1.0f, panText));

    layout.add (makeParam (ids::width, "Width",
                           juce::NormalisableRange<float> { 0.0f, 2.0f, 0.001f }, 1.0f,
                           [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }));

    layout.add (makeParam (ids::mix, "Mix",
                           juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.35f,
                           [] (float v, int) { return format::percent (v); }));

    // ---- multi-tap --------------------------------------------------------

    layout.add (std::make_unique<Bool> (juce::ParameterID { ids::multiTap, 1 }, "Multi-tap", false));

    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ids::division, 1 }, "Division", 1, numTaps, 4));

    layout.add (makeParam (ids::tapSpread, "Tap Spread",
                           juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 1.0f,
                           [] (float v, int) { return format::percent (v); }));

    /*  A default pattern that is worth hearing the moment multi-tap is switched
        on: every step present, alternating sides, and the level falling across
        the bar so it reads as a decay rather than as a machine gun.
    */
    for (int i = 1; i <= numTaps; ++i)
    {
        const auto defaultLevel = juce::jmax (0.25f, 1.0f - 0.045f * (float) (i - 1));
        const auto defaultPan = (i % 2 == 0 ? 0.7f : -0.7f);

        layout.add (std::make_unique<Bool> (
            juce::ParameterID { ids::tapOn (i), 1 }, "Tap " + juce::String (i), true));

        layout.add (makeParam (ids::tapLevel (i), "Tap " + juce::String (i) + " Level",
                               juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, defaultLevel,
                               [] (float v, int) { return format::percent (v); }));

        layout.add (makeParam (ids::tapPan (i), "Tap " + juce::String (i) + " Pan",
                               juce::NormalisableRange<float> { -1.0f, 1.0f, 0.001f }, defaultPan,
                               panText));
    }

    // ---- modulation -------------------------------------------------------

    for (int lfo = 1; lfo <= numLfos; ++lfo)
    {
        layout.add (makeParam (ids::lfoRate (lfo), "LFO " + juce::String (lfo) + " Rate",
                               juce::NormalisableRange<float> { 0.01f, 20.0f, 0.001f, 0.3f },
                               lfo == 1 ? 1.0f : 0.25f,
                               [] (float v, int) { return juce::String (v, 2) + " Hz"; }));

        layout.add (std::make_unique<Bool> (
            juce::ParameterID { ids::lfoSync (lfo), 1 }, "LFO " + juce::String (lfo) + " Sync", false));

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ids::lfoNote (lfo), 1 }, "LFO " + juce::String (lfo) + " Note",
            noteValueNames(), (int) NoteValue::quarter));

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ids::lfoShape (lfo), 1 }, "LFO " + juce::String (lfo) + " Shape",
            lfoShapeNames(), 0));

        // Phase in degrees rather than a fraction: 90 is a number people can
        // aim at and 0.25 is not.
        layout.add (makeParam (ids::lfoPhase (lfo), "LFO " + juce::String (lfo) + " Phase",
                               juce::NormalisableRange<float> { 0.0f, 360.0f, 1.0f },
                               lfo == 1 ? 0.0f : 90.0f,
                               [] (float v, int) { return juce::String ((int) v) + juce::String::fromUTF8 ("°"); }));
    }

    layout.add (makeParam (ids::envAttack, "Env Attack",
                           ranges::timeMs (0.5f, 200.0f, 20.0f), 10.0f, format::milliseconds));

    layout.add (makeParam (ids::envRelease, "Env Release",
                           ranges::timeMs (5.0f, 2000.0f, 200.0f), 200.0f, format::milliseconds));

    for (int slot = 1; slot <= numModSlots; ++slot)
    {
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ids::modSource (slot), 1 },
            "Mod " + juce::String (slot) + " Source", modSourceNames(), 0));

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ids::modTarget (slot), 1 },
            "Mod " + juce::String (slot) + " Target", modTargetNames(), 0));

        layout.add (makeParam (ids::modAmount (slot), "Mod " + juce::String (slot) + " Amount",
                               juce::NormalisableRange<float> { -1.0f, 1.0f, 0.001f }, 0.0f,
                               [] (float v, int)
                               {
                                   return juce::String (juce::roundToInt (v * 100.0f)) + " %";
                               }));
    }

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

DelaySettings readSettings (juce::AudioProcessorValueTreeState& state)
{
    DelaySettings s;

    s.timeLeftMs  = readValue (state, ids::timeLeft, 375.0f);
    s.timeRightMs = readValue (state, ids::timeRight, 500.0f);
    s.sync        = readValue (state, ids::sync, 0.0f) > 0.5f;
    s.noteLeft    = (NoteValue) (int) readValue (state, ids::noteLeft, (float) (int) NoteValue::eighthDotted);
    s.noteRight   = (NoteValue) (int) readValue (state, ids::noteRight, (float) (int) NoteValue::quarter);
    s.linkTimes   = readValue (state, ids::linkTimes, 0.0f) > 0.5f;

    s.feedback = readValue (state, ids::feedback, 0.35f);
    s.cross    = readValue (state, ids::cross, 0.0f);
    s.invertFeedback = readValue (state, ids::invert, 0.0f) > 0.5f;
    s.freeze   = readValue (state, ids::freeze, 0.0f) > 0.5f;

    s.highpassHz = readValue (state, ids::highpass, 20.0f);
    s.lowpassHz  = readValue (state, ids::lowpass, 20000.0f);
    s.drive      = readValue (state, ids::drive, 0.0f);
    s.loFi       = readValue (state, ids::loFi, 0.0f);

    s.modRateHz = readValue (state, ids::modRate, 0.5f);
    s.modDepth  = readValue (state, ids::modDepth, 0.0f);

    s.routing  = (Routing)  (int) readValue (state, ids::routing, 0.0f);
    s.timeMode = (TimeMode) (int) readValue (state, ids::timeMode, 0.0f);

    s.panLeft  = readValue (state, ids::panLeft, -1.0f);
    s.panRight = readValue (state, ids::panRight, 1.0f);
    s.width    = readValue (state, ids::width, 1.0f);
    s.mix      = readValue (state, ids::mix, 0.35f);

    s.multiTap  = readValue (state, ids::multiTap, 0.0f) > 0.5f;
    s.division  = juce::jlimit (1, numTaps, (int) std::round (readValue (state, ids::division, 4.0f)));
    s.tapSpread = readValue (state, ids::tapSpread, 1.0f);

    for (int i = 0; i < numTaps; ++i)
    {
        auto& tap = s.taps[(size_t) i];

        tap.on    = readValue (state, ids::tapOn (i + 1), 1.0f) > 0.5f;
        tap.level = readValue (state, ids::tapLevel (i + 1), 1.0f);
        tap.pan   = readValue (state, ids::tapPan (i + 1), 0.0f);
    }

    for (int lfo = 0; lfo < numLfos; ++lfo)
    {
        auto& settings = s.lfos[(size_t) lfo];

        settings.rateHz = readValue (state, ids::lfoRate (lfo + 1), 1.0f);
        settings.sync   = readValue (state, ids::lfoSync (lfo + 1), 0.0f) > 0.5f;
        settings.note   = (NoteValue) (int) readValue (state, ids::lfoNote (lfo + 1),
                                                       (float) (int) NoteValue::quarter);
        settings.shape  = (dsp::Lfo::Shape) (int) readValue (state, ids::lfoShape (lfo + 1), 0.0f);
        settings.phase  = readValue (state, ids::lfoPhase (lfo + 1), 0.0f) / 360.0f;
    }

    s.envelopeAttackMs  = readValue (state, ids::envAttack, 10.0f);
    s.envelopeReleaseMs = readValue (state, ids::envRelease, 200.0f);

    for (int slot = 0; slot < numModSlots; ++slot)
    {
        auto& m = s.mod[(size_t) slot];

        m.source = (ModSource) (int) readValue (state, ids::modSource (slot + 1), 0.0f);
        m.target = (ModTarget) (int) readValue (state, ids::modTarget (slot + 1), 0.0f);
        m.amount = readValue (state, ids::modAmount (slot + 1), 0.0f);
    }

    return s;
}

float resolvedLfoRateHz (const DelaySettings& settings, int lfo)
{
    const auto& s = settings.lfos[(size_t) juce::jlimit (0, numLfos - 1, lfo)];

    if (! s.sync)
        return s.rateHz;

    const auto bpm = juce::jlimit (20.0, 400.0, settings.bpm);
    const auto seconds = noteValueInBeats (s.note) * 60.0 / bpm;

    return (float) juce::jlimit (0.01, 20.0, 1.0 / juce::jmax (1.0e-6, seconds));
}

bool modulationActive (const DelaySettings& settings) noexcept
{
    for (const auto& slot : settings.mod)
        if (slot.source != ModSource::none
            && slot.target != ModTarget::none
            && std::abs (slot.amount) > 1.0e-4f)
            return true;

    return false;
}

float resolvedTimeMs (const DelaySettings& settings, int line)
{
    const auto useLeft = settings.linkTimes || line == 0;

    if (settings.sync)
    {
        const auto bpm = juce::jlimit (20.0, 400.0, settings.bpm);
        const auto beats = noteValueInBeats (useLeft ? settings.noteLeft : settings.noteRight);

        return (float) juce::jlimit (5.0, 5000.0, beats * 60000.0 / bpm);
    }

    return useLeft ? settings.timeLeftMs : settings.timeRightMs;
}
} // namespace nodo::delay
