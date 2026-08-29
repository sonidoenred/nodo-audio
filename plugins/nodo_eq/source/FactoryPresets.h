#pragma once

#include <nodo_core/nodo_core.h>
#include "EqParameters.h"

namespace nodo::eq
{
/** The presets that ship with the plugin.

    A word on what these are for, because EQ presets have a deservedly poor
    reputation. A preset that boosts 3 kHz "for vocals" is close to useless: it
    depends entirely on that voice, that microphone and that arrangement. So this
    set is deliberately split.

    The Utility and Dynamic presets are objective. A low cut at 80 Hz, a notch on
    mains hum, a de-esser: these are answers to problems that exist independently
    of taste, and they are the ones worth having a shortcut for.

    The Starting point presets are scaffolding, and they say so in their own
    description. For someone learning, a sensible arrangement of bands to then
    move is worth more than a blank window. For anyone else they are a waste of a
    click, and that is fine.
*/

/** One band of a factory preset, in human terms rather than parameter IDs. */
struct PresetBand
{
    FilterType  type { FilterType::bell };
    float       frequency { 1000.0f };
    float       gainDb { 0.0f };
    float       q { 0.707f };
    int         slopeIndex { 1 };                       // 0 == 12 dB/oct
    ChannelMode channel { ChannelMode::stereo };

    bool        dynamic { false };
    DynamicMode dynamicMode { DynamicMode::above };

    /** Cuanto viaja la banda desde su ganancia cuando la dinamica actua. */
    float       dynRangeDb { 0.0f };
    float       thresholdDb { -24.0f };
    float       ratio { 4.0f };
    float       attackMs { 10.0f };
    float       releaseMs { 120.0f };
};

struct PresetDefinition
{
    const char* category;
    const char* name;
    const char* description;
    std::vector<PresetBand> bands;
};

/** The definitions, in menu order. */
const std::vector<PresetDefinition>& getPresetDefinitions();

/** Turns the definitions into the parameter assignments the PresetManager
    applies. Exposed separately so the tests can check every identifier against
    the real parameter layout.
*/
std::vector<nodo::FactoryPreset> buildFactoryPresets();
} // namespace nodo::eq
