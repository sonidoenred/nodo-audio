#include "EqParameters.h"

namespace nodo::eq
{
juce::StringArray filterTypeNames()
{
    return { "Low Cut", "Low Shelf", "Bell", "Notch", "Band Pass",
             "Tilt Shelf", "All Pass", "High Shelf", "High Cut" };
}

juce::StringArray slopeNames()
{
    juce::StringArray names;

    for (int i = 0; i < 8; ++i)
        names.add (juce::String (slopeIndexToDbPerOctave (i)) + " dB/oct");

    return names;
}

juce::StringArray channelModeNames()
{
    return { "Stereo", "Left", "Right", "Mid", "Side" };
}

juce::StringArray dynamicModeNames()
{
    return { "Above", "Below" };
}

juce::String channelModeBadge (ChannelMode mode)
{
    switch (mode)
    {
        case ChannelMode::left:   return "L";
        case ChannelMode::right:  return "R";
        case ChannelMode::mid:    return "M";
        case ChannelMode::side:   return "S";
        case ChannelMode::stereo: break;
    }

    return {};
}

namespace ids
{
    juce::String bandEnabled (int band) { return "band" + juce::String (band + 1) + "_on"; }
    juce::String bandType    (int band) { return "band" + juce::String (band + 1) + "_type"; }
    juce::String bandFreq    (int band) { return "band" + juce::String (band + 1) + "_freq"; }
    juce::String bandGain    (int band) { return "band" + juce::String (band + 1) + "_gain"; }
    juce::String bandQ       (int band) { return "band" + juce::String (band + 1) + "_q"; }
    juce::String bandSlope   (int band) { return "band" + juce::String (band + 1) + "_slope"; }
    juce::String bandChannel (int band) { return "band" + juce::String (band + 1) + "_ch"; }
    juce::String bandDynamic (int band) { return "band" + juce::String (band + 1) + "_dyn"; }
    juce::String bandDynMode (int band) { return "band" + juce::String (band + 1) + "_dynmode"; }
    juce::String bandThreshold (int band) { return "band" + juce::String (band + 1) + "_thr"; }
    juce::String bandRatio    (int band) { return "band" + juce::String (band + 1) + "_ratio"; }
    juce::String bandAttack   (int band) { return "band" + juce::String (band + 1) + "_att"; }
    juce::String bandRelease  (int band) { return "band" + juce::String (band + 1) + "_rel"; }
    juce::String bandDynRange (int band) { return "band" + juce::String (band + 1) + "_dynrange"; }
}

namespace
{
    /** Starting points spread evenly in log frequency across the audible range,
        so a band switched on from the host (rather than created by clicking on
        the display) lands somewhere useful instead of all of them at 1 kHz.
    */
    float defaultFrequency (int band)
    {
        const auto proportion = numBands > 1 ? (double) band / (double) (numBands - 1) : 0.5;
        return (float) (20.0 * std::pow (1000.0, proportion));
    }

    int defaultType (int band)
    {
        if (band == 0)            return (int) FilterType::lowCut;
        if (band == numBands - 1) return (int) FilterType::highCut;
        return (int) FilterType::bell;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    using Float  = juce::AudioParameterFloat;
    using Choice = juce::AudioParameterChoice;
    using Bool   = juce::AudioParameterBool;

    for (int band = 0; band < numBands; ++band)
    {
        const auto n = juce::String (band + 1);

        layout.add (std::make_unique<Bool> (
            juce::ParameterID { ids::bandEnabled (band), 1 },
            "Band " + n + " On",
            false));

        layout.add (std::make_unique<Choice> (
            juce::ParameterID { ids::bandType (band), 1 },
            "Band " + n + " Type",
            filterTypeNames(),
            defaultType (band)));

        layout.add (std::make_unique<Float> (
            juce::ParameterID { ids::bandFreq (band), 1 },
            "Band " + n + " Freq",
            ranges::frequency(),
            defaultFrequency (band),
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return format::frequency (v); })
                .withValueFromStringFunction ([] (const juce::String& t) { return format::frequencyFromText (t); })));

        layout.add (std::make_unique<Float> (
            juce::ParameterID { ids::bandGain (band), 1 },
            "Band " + n + " Gain",
            ranges::gainDb (24.0f),
            0.0f,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return format::decibels (v); })
                .withValueFromStringFunction ([] (const juce::String& t) { return format::decibelsFromText (t); })));

        layout.add (std::make_unique<Float> (
            juce::ParameterID { ids::bandQ (band), 1 },
            "Band " + n + " Q",
            ranges::quality(),
            0.707f,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return format::quality (v); })));

        layout.add (std::make_unique<Choice> (
            juce::ParameterID { ids::bandSlope (band), 1 },
            "Band " + n + " Slope",
            slopeNames(),
            1)); // 24 dB/oct

        layout.add (std::make_unique<Choice> (
            juce::ParameterID { ids::bandChannel (band), 1 },
            "Band " + n + " Channel",
            channelModeNames(),
            0));

        layout.add (std::make_unique<Bool> (
            juce::ParameterID { ids::bandDynamic (band), 1 },
            "Band " + n + " Dynamic",
            false));

        layout.add (std::make_unique<Choice> (
            juce::ParameterID { ids::bandDynMode (band), 1 },
            "Band " + n + " Dyn Mode",
            dynamicModeNames(),
            0));

        layout.add (std::make_unique<Float> (
            juce::ParameterID { ids::bandThreshold (band), 1 },
            "Band " + n + " Threshold",
            juce::NormalisableRange<float> { -72.0f, 0.0f, 0.1f },
            -24.0f,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return format::decibels (v); })
                .withValueFromStringFunction ([] (const juce::String& t) { return format::decibelsFromText (t); })));

        layout.add (std::make_unique<Float> (
            juce::ParameterID { ids::bandRatio (band), 1 },
            "Band " + n + " Ratio",
            ranges::ratio(),
            4.0f,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return format::ratio (v); })));

        layout.add (std::make_unique<Float> (
            juce::ParameterID { ids::bandAttack (band), 1 },
            "Band " + n + " Attack",
            ranges::timeMs (0.1f, 300.0f, 10.0f),
            10.0f,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return format::milliseconds (v); })));

        layout.add (std::make_unique<Float> (
            juce::ParameterID { ids::bandRelease (band), 1 },
            "Band " + n + " Release",
            ranges::timeMs (5.0f, 2000.0f, 150.0f),
            120.0f,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return format::milliseconds (v); })));
    }

    layout.add (std::make_unique<Bool> (
        juce::ParameterID { ids::bypass, 1 }, "Bypass", false));

    layout.add (std::make_unique<Float> (
        juce::ParameterID { ids::outputGain, 1 },
        "Output",
        ranges::gainDb (24.0f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return format::decibels (v); })
            .withValueFromStringFunction ([] (const juce::String& t) { return format::decibelsFromText (t); })));

    layout.add (std::make_unique<Bool> (
        juce::ParameterID { ids::autoGain, 1 }, "Auto Gain", false));

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ids::oversampling, 1 },
        "Oversampling",
        juce::StringArray { "Off", "2x" },
        0));

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ids::analyserMode, 1 },
        "Analyser",
        juce::StringArray { "Off", "Pre", "Post", "Pre + Post" },
        3));

    // Analog is the default: it is closer to the prototype at every setting the
    // designer accepts, and identical everywhere else. Digital is kept so the
    // difference can be heard, and because it is what every other plugin does.
    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ids::filterMode, 1 },
        "Filter Mode",
        juce::StringArray { "Digital", "Analog" },
        1));

    // Where the dynamic bands listen. One switch for all of them rather than one
    // per band: see EqEngine::setDynamicSidechain for the reasoning.
    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ids::dynSidechain, 1 },
        "Dynamic Sidechain",
        juce::StringArray { "Dyn: Internal", "Dyn: Sidechain" },
        0));

    /*  El recorrido de cada banda dinamica va aqui abajo, fuera del bucle que
        crea el resto de la banda, y no junto a sus companeras.

        El orden de los parametros esta congelado igual que los identificadores:
        algunos hosts guardan la automatizacion por indice. Meter un parametro
        nuevo dentro del bucle habria corrido de sitio todos los de las bandas
        siguientes. Queda feo en el codigo y es correcto en la sesion de
        cualquiera, que es el unico sitio donde importa.

        Mas ancho que la ganancia porque es una diferencia entre dos ganancias:
        de +24 a -24 hay 48 dB.
    */
    for (int band = 0; band < numBands; ++band)
    {
        const auto n = juce::String (band + 1);

        layout.add (std::make_unique<Float> (
            juce::ParameterID { ids::bandDynRange (band), 1 },
            "Band " + n + " Dyn Range",
            ranges::gainDb (48.0f),
            0.0f,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return format::decibels (v); })
                .withValueFromStringFunction ([] (const juce::String& t) { return format::decibelsFromText (t); })));
    }

    return layout;
}

BandSettings readBand (const juce::AudioProcessorValueTreeState& state, int band)
{
    BandSettings s;

    if (auto* p = state.getRawParameterValue (ids::bandEnabled (band))) s.enabled   = p->load() > 0.5f;
    if (auto* p = state.getRawParameterValue (ids::bandType (band)))    s.type      = (FilterType) (int) p->load();
    if (auto* p = state.getRawParameterValue (ids::bandFreq (band)))    s.frequency = p->load();
    if (auto* p = state.getRawParameterValue (ids::bandGain (band)))    s.gainDb    = p->load();
    if (auto* p = state.getRawParameterValue (ids::bandQ (band)))       s.q         = p->load();
    if (auto* p = state.getRawParameterValue (ids::bandSlope (band)))   s.slopeStages = (int) p->load() + 1;
    if (auto* p = state.getRawParameterValue (ids::bandChannel (band))) s.channel   = (ChannelMode) (int) p->load();
    if (auto* p = state.getRawParameterValue (ids::bandDynamic (band))) s.dynamic   = p->load() > 0.5f;
    if (auto* p = state.getRawParameterValue (ids::bandDynMode (band))) s.dynamicMode = (DynamicMode) (int) p->load();
    if (auto* p = state.getRawParameterValue (ids::bandThreshold (band))) s.thresholdDb = p->load();
    if (auto* p = state.getRawParameterValue (ids::bandRatio (band)))   s.ratio     = p->load();
    if (auto* p = state.getRawParameterValue (ids::bandAttack (band)))  s.attackMs  = p->load();
    if (auto* p = state.getRawParameterValue (ids::bandRelease (band))) s.releaseMs = p->load();
    if (auto* p = state.getRawParameterValue (ids::bandDynRange (band))) s.dynRangeDb = p->load();

    return s;
}
} // namespace nodo::eq
