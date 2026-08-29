#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <nodo_core/nodo_core.h>

namespace nodo::eq
{
/** Twenty-four bands, all disabled by default.

    The count is fixed rather than truly unlimited: every band has to exist as a
    host-visible parameter, and parameters cannot be created after construction.
    Twenty-four is what Pro-Q offers and is far more than anyone uses on a single
    instance, so in practice it behaves like "as many as you want" while staying
    a static, automatable parameter set.
*/
inline constexpr int numBands = 24;

/** Filter shapes available per band.

    THIS ORDER IS FROZEN. The index is what gets written into every saved session
    and preset, so inserting a shape in the middle would silently turn every
    saved bell into a notch. New shapes go on the end.
*/
enum class FilterType
{
    lowCut = 0,
    lowShelf,
    bell,
    notch,
    bandPass,
    tiltShelf,
    allPass,
    highShelf,
    highCut
};

inline constexpr int numFilterTypes = 9;

juce::StringArray filterTypeNames();
juce::StringArray slopeNames();

/** 0 -> 12 dB/oct ... 7 -> 96 dB/oct */
inline int slopeIndexToDbPerOctave (int index) noexcept { return 12 * (index + 1); }

inline bool isCut (FilterType t) noexcept
{
    return t == FilterType::lowCut || t == FilterType::highCut;
}

/** True when the shape has a meaningful gain control. */
inline bool usesGain (FilterType t) noexcept
{
    return t == FilterType::bell
        || t == FilterType::lowShelf
        || t == FilterType::highShelf
        || t == FilterType::tiltShelf;
}

/** True when Q does something audible. All-pass and band-pass need it; shelves
    use it as a slope control; cuts take their steepness from the slope menu.
*/
inline bool usesQ (FilterType t) noexcept
{
    return ! isCut (t);
}

/** Which part of the stereo image a band acts on.

    THIS ORDER IS FROZEN, like the filter types: the index is written into every
    saved session.
*/
enum class ChannelMode
{
    stereo = 0,
    left,
    right,
    mid,
    side
};

inline constexpr int numChannelModes = 5;

juce::StringArray channelModeNames();

/** Short badge drawn on the band's node: empty for stereo. */
juce::String channelModeBadge (ChannelMode mode);

/** True when the band has to be applied in the mid/side domain rather than the
    left/right one.
*/
inline bool isMidSide (ChannelMode mode) noexcept
{
    return mode == ChannelMode::mid || mode == ChannelMode::side;
}

/** Which of the two channels a band touches, as a bitmask.
    In the left/right domain channel 0 is L and 1 is R; once the block has been
    rotated into mid/side, channel 0 is M and 1 is S.
*/
inline int channelMaskFor (ChannelMode mode) noexcept
{
    switch (mode)
    {
        case ChannelMode::left:
        case ChannelMode::mid:    return 0b01;
        case ChannelMode::right:
        case ChannelMode::side:   return 0b10;
        case ChannelMode::stereo: break;
    }

    return 0b11;
}

/** Dynamic direction, mirrored from nodo::dsp so the parameter layer does not
    have to reach into the DSP namespace. FROZEN ORDER.
*/
enum class DynamicMode { above = 0, below };

juce::StringArray dynamicModeNames();

/** One band's state, resolved from parameters. Plain data so it can be copied
    freely between the audio thread and the GUI.
*/
struct BandSettings
{
    bool       enabled { false };
    FilterType type { FilterType::bell };
    float      frequency { 1000.0f };
    float      gainDb { 0.0f };
    float      q { 0.707f };
    int        slopeStages { 2 };      // 1..8 biquads, cuts only
    ChannelMode channel { ChannelMode::stereo };

    // Dynamic section. When dynamic is off the band simply sits at gainDb.
    bool        dynamic { false };
    DynamicMode dynamicMode { DynamicMode::above };

    /*  Cuanto se mueve la banda desde donde esta, en dB, cuando la dinamica
        llega al final de su recorrido.

        La ganancia es donde vive la banda y esto es cuanto viaja: encender la
        dinamica con el recorrido a cero no cambia absolutamente nada de lo que
        se oye, que es la unica forma de que encenderla no sea una sorpresa.
        Antes la banda descansaba en plano y viajaba hasta su ganancia, o sea
        que encender la dinamica apagaba la banda hasta que entrara senal.
    */
    float       dynRangeDb { 0.0f };
    float       thresholdDb { -24.0f };
    float       ratio { 4.0f };
    float       attackMs { 10.0f };
    float       releaseMs { 120.0f };

    bool operator== (const BandSettings& other) const noexcept
    {
        return enabled == other.enabled
            && type == other.type
            && channel == other.channel
            && dynamic == other.dynamic
            && dynamicMode == other.dynamicMode
            && juce::approximatelyEqual (frequency, other.frequency)
            && juce::approximatelyEqual (gainDb, other.gainDb)
            && juce::approximatelyEqual (q, other.q)
            && juce::approximatelyEqual (dynRangeDb, other.dynRangeDb)
            && juce::approximatelyEqual (thresholdDb, other.thresholdDb)
            && juce::approximatelyEqual (ratio, other.ratio)
            && juce::approximatelyEqual (attackMs, other.attackMs)
            && juce::approximatelyEqual (releaseMs, other.releaseMs)
            && slopeStages == other.slopeStages;
    }

    bool operator!= (const BandSettings& other) const noexcept { return ! operator== (other); }
};

/** Parameter identifiers. These strings are written into every saved session and
    preset, so they are frozen forever once the plugin ships. Renaming one is a
    breaking change that needs a migration step in StateVersion.h.
*/
namespace ids
{
    juce::String bandEnabled (int band);
    juce::String bandType    (int band);
    juce::String bandFreq    (int band);
    juce::String bandGain    (int band);
    juce::String bandQ       (int band);
    juce::String bandSlope   (int band);
    juce::String bandChannel (int band);
    juce::String bandDynamic (int band);
    juce::String bandDynMode (int band);
    juce::String bandThreshold (int band);
    juce::String bandRatio    (int band);
    juce::String bandAttack   (int band);
    juce::String bandRelease  (int band);
    juce::String bandDynRange (int band);

    inline const juce::String bypass       { "bypass" };
    inline const juce::String outputGain   { "outputGain" };
    inline const juce::String autoGain     { "autoGain" };
    inline const juce::String oversampling { "oversampling" };
    inline const juce::String analyserMode { "analyserMode" };
    inline const juce::String filterMode   { "filterMode" };
    inline const juce::String dynSidechain { "dynSidechain" };
}

/** Analyser display mode. Stored as a parameter so it survives session reload. */
enum class AnalyserMode { off = 0, pre, post, both };

/** How bells and shelves are turned into digital filters.

    Digital is the classic bilinear (RBJ cookbook) design that nearly every
    plugin uses. Analog re-derives the filter so it follows the analog prototype
    all the way to Nyquist instead of being crushed flat against it — see
    nodo_core/dsp/MatchedBiquad.h for what that means and why it matters.

    Cuts, notches, band passes and all passes are bilinear in both modes.
*/
enum class FilterMode { digital = 0, analogMatched };

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/** Reads a band's current target values straight from the parameter tree. */
BandSettings readBand (const juce::AudioProcessorValueTreeState& state, int band);
} // namespace nodo::eq
