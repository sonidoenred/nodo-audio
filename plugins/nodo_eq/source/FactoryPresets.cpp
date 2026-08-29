#include "FactoryPresets.h"

namespace nodo::eq
{
namespace
{
    PresetBand lowCut (float frequency, int slopeIndex = 1)
    {
        PresetBand b;
        b.type = FilterType::lowCut;
        b.frequency = frequency;
        b.slopeIndex = slopeIndex;
        return b;
    }

    PresetBand highCut (float frequency, int slopeIndex = 1)
    {
        PresetBand b;
        b.type = FilterType::highCut;
        b.frequency = frequency;
        b.slopeIndex = slopeIndex;
        return b;
    }

    PresetBand bell (float frequency, float gainDb, float q)
    {
        PresetBand b;
        b.type = FilterType::bell;
        b.frequency = frequency;
        b.gainDb = gainDb;
        b.q = q;
        return b;
    }

    PresetBand notch (float frequency, float q)
    {
        PresetBand b;
        b.type = FilterType::notch;
        b.frequency = frequency;
        b.q = q;
        return b;
    }

    PresetBand highShelf (float frequency, float gainDb, float q = 0.7f)
    {
        PresetBand b;
        b.type = FilterType::highShelf;
        b.frequency = frequency;
        b.gainDb = gainDb;
        b.q = q;
        return b;
    }

    PresetBand tilt (float gainDb)
    {
        PresetBand b;
        b.type = FilterType::tiltShelf;
        b.frequency = 1000.0f;
        b.gainDb = gainDb;
        b.q = 0.5f;
        return b;
    }

    /** A dynamic bell: the gain is the furthest it may travel, not where it sits. */
    /*  Una banda dinamica de fabrica: plana en reposo y con el recorrido en el
        segundo argumento. Antes ese numero era la ganancia de la banda, porque
        la dinamica viajaba desde plano hasta ella; ahora la banda vive donde
        dice su ganancia y el recorrido es lo que se mueve desde ahi.
    */
    PresetBand dynamicBell (float frequency, float rangeDb, float q,
                            float thresholdDb, float ratio, float attackMs, float releaseMs)
    {
        auto b = bell (frequency, 0.0f, q);
        b.dynRangeDb = rangeDb;
        b.dynamic = true;
        b.dynamicMode = DynamicMode::above;
        b.thresholdDb = thresholdDb;
        b.ratio = ratio;
        b.attackMs = attackMs;
        b.releaseMs = releaseMs;
        return b;
    }

    PresetBand onSide (PresetBand b)
    {
        b.channel = ChannelMode::side;
        return b;
    }

    const char* const scaffold =
        "A scaffold, not an answer. Every band here is a guess about a source it "
        "has never heard: move them.";
}

const std::vector<PresetDefinition>& getPresetDefinitions()
{
    static const std::vector<PresetDefinition> presets
    {
        //======================================================================
        { "Utility", "Low Cut 80 Hz",
          "The most common move there is. Clears rumble, handling noise and "
          "proximity build-up without touching anything musical.",
          { lowCut (80.0f, 1) } },

        { "Utility", "Low Cut 120 Hz steep",
          "For a dense arrangement, where anything under the bass is just taking "
          "up headroom. 48 dB per octave leaves no tail.",
          { lowCut (120.0f, 3) } },

        { "Utility", "Rumble and Hiss",
          "Cleans both ends and nothing in between: below 40 Hz and above 16 kHz "
          "there is rarely anything you meant to record.",
          { lowCut (40.0f, 1), highCut (16000.0f, 1) } },

        { "Utility", "Mains Hum 50 Hz",
          "Four narrow notches on 50 Hz and its harmonics, for European mains "
          "buzz. Check the fundamental first: often only one notch is needed.",
          { notch (50.0f, 18.0f), notch (100.0f, 18.0f),
            notch (150.0f, 18.0f), notch (200.0f, 18.0f) } },

        { "Utility", "Mains Hum 60 Hz",
          "The same for 60 Hz mains, as used in the Americas and Japan.",
          { notch (60.0f, 18.0f), notch (120.0f, 18.0f),
            notch (180.0f, 18.0f), notch (240.0f, 18.0f) } },

        //======================================================================
        { "Dynamic", "De-esser",
          "A dynamic dip around 7 kHz that only engages on sibilance and leaves "
          "the rest of the top end alone. Solo the band to hear what it is "
          "listening to, then move the threshold until only the harsh S sounds "
          "trigger it.",
          { dynamicBell (7000.0f, -8.0f, 3.0f, -24.0f, 6.0f, 1.0f, 60.0f) } },

        { "Dynamic", "Tame Low Mids",
          "Pulls back the boxy region only when it builds up, so the body of the "
          "sound survives the quiet passages.",
          { dynamicBell (280.0f, -5.0f, 1.2f, -20.0f, 4.0f, 15.0f, 150.0f) } },

        { "Dynamic", "Tame Harshness",
          "For a source that turns hard when it gets loud. Sits idle until it "
          "does.",
          { dynamicBell (3200.0f, -5.0f, 1.5f, -22.0f, 4.0f, 5.0f, 120.0f) } },

        { "Dynamic", "Boom Control",
          "Catches the one note on a bass or a room that jumps out, without "
          "thinning the rest of the low end.",
          { dynamicBell (120.0f, -6.0f, 2.0f, -18.0f, 5.0f, 10.0f, 180.0f) } },

        //======================================================================
        { "Tone", "Air",
          "A gentle shelf up top. The oldest trick in the book and still the one "
          "people reach for first.",
          { highShelf (12000.0f, 2.5f, 0.7f) } },

        { "Tone", "Tilt Warm",
          "Rotates the whole spectrum around 1 kHz: lows up, highs down, by two "
          "decibels end to end. Useful when a source is bright rather than wrong.",
          { tilt (-2.0f) } },

        { "Tone", "Tilt Bright",
          "The same rotation the other way.",
          { tilt (2.0f) } },

        //======================================================================
        { "Stereo", "Mono Bass below 150 Hz",
          "A steep low cut applied only to the side channel, which leaves the "
          "bottom end centred while everything above stays as wide as it was. "
          "Standard practice before cutting vinyl, and it usually tightens a mix "
          "anyway.",
          { [] { auto b = lowCut (150.0f, 2); b.channel = ChannelMode::side; return b; }() } },

        { "Stereo", "Wide Top",
          "Lifts the top of the side channel only. Opens up a mix without the "
          "smearing that a stereo widener would add.",
          { onSide (highShelf (8000.0f, 2.5f, 0.7f)) } },

        //======================================================================
        { "Starting point", "Vocal", scaffold,
          { lowCut (90.0f, 1),
            bell (300.0f, -2.0f, 1.0f),
            bell (4000.0f, 1.5f, 0.8f),
            highShelf (10000.0f, 2.0f, 0.7f),
            dynamicBell (7000.0f, -6.0f, 3.0f, -22.0f, 6.0f, 1.0f, 60.0f) } },

        { "Starting point", "Acoustic Guitar", scaffold,
          { lowCut (90.0f, 1),
            bell (200.0f, -2.5f, 1.2f),
            bell (6000.0f, 2.0f, 0.8f) } },

        { "Starting point", "Kick", scaffold,
          { lowCut (30.0f, 1),
            bell (60.0f, 2.0f, 1.2f),
            bell (350.0f, -3.0f, 1.5f),
            bell (3500.0f, 2.0f, 1.0f) } },

        { "Starting point", "Snare", scaffold,
          { lowCut (90.0f, 1),
            bell (200.0f, 2.0f, 1.2f),
            bell (800.0f, -2.0f, 1.5f),
            highShelf (8000.0f, 2.0f, 0.7f) } },

        { "Starting point", "Bass", scaffold,
          { lowCut (30.0f, 1),
            bell (80.0f, 1.5f, 1.0f),
            bell (400.0f, -2.0f, 1.2f),
            bell (1200.0f, 2.0f, 1.0f) } },

        { "Starting point", "Drum Bus", scaffold,
          { lowCut (25.0f, 1),
            bell (70.0f, 1.5f, 0.8f),
            bell (400.0f, -1.5f, 1.0f),
            highShelf (9000.0f, 1.5f, 0.7f) } },

        { "Starting point", "Mix Bus", scaffold,
          { lowCut (25.0f, 1),
            dynamicBell (250.0f, -2.0f, 1.0f, -18.0f, 3.0f, 20.0f, 200.0f),
            highShelf (12000.0f, 1.0f, 0.7f) } },
    };

    return presets;
}

std::vector<nodo::FactoryPreset> buildFactoryPresets()
{
    std::vector<nodo::FactoryPreset> result;

    for (const auto& definition : getPresetDefinitions())
    {
        nodo::FactoryPreset preset;
        // utf8(): the text is Spanish and juce::String would read the bytes as
        // ASCII. See the note on the helper.
        preset.category = nodo::utf8 (definition.category);
        preset.name = nodo::utf8 (definition.name);
        preset.description = nodo::utf8 (definition.description);

        for (size_t i = 0; i < definition.bands.size() && i < (size_t) numBands; ++i)
        {
            const auto& band = definition.bands[i];
            const auto index = (int) i;

            preset.assignments.emplace_back (ids::bandEnabled (index), 1.0f);
            preset.assignments.emplace_back (ids::bandType (index), (float) (int) band.type);
            preset.assignments.emplace_back (ids::bandFreq (index), band.frequency);
            preset.assignments.emplace_back (ids::bandGain (index), band.gainDb);
            preset.assignments.emplace_back (ids::bandDynRange (index), band.dynRangeDb);
            preset.assignments.emplace_back (ids::bandQ (index), band.q);
            preset.assignments.emplace_back (ids::bandSlope (index), (float) band.slopeIndex);
            preset.assignments.emplace_back (ids::bandChannel (index), (float) (int) band.channel);
            preset.assignments.emplace_back (ids::bandDynamic (index), band.dynamic ? 1.0f : 0.0f);
            preset.assignments.emplace_back (ids::bandDynMode (index), (float) (int) band.dynamicMode);
            preset.assignments.emplace_back (ids::bandThreshold (index), band.thresholdDb);
            preset.assignments.emplace_back (ids::bandRatio (index), band.ratio);
            preset.assignments.emplace_back (ids::bandAttack (index), band.attackMs);
            preset.assignments.emplace_back (ids::bandRelease (index), band.releaseMs);
        }

        result.push_back (std::move (preset));
    }

    return result;
}
} // namespace nodo::eq
