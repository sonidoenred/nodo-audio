#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <nodo_core/nodo_core.h>

namespace nodo::gate
{
/** How the gate behaves before the knobs are touched.

    As in the compressor, a style is not a different plugin: it is a set of
    multipliers and two structural choices laid on top of what the user asked
    for, and every one of them is visible in styleTraits() below. A mode that
    cannot be explained is a mode that cannot be debugged.

    The axes that actually matter for a gate are different from a compressor's:
    what the detector reads (a peak or a loudness), how much the release slows
    down as the gate closes, and how much hold the style adds on its own. Those
    three decide whether a gate chatters on a breath or holds through it.

    THIS ORDER IS FROZEN. The index is written into every saved session.
*/
enum class GateStyle
{
    clean = 0,   ///< Does exactly what the knobs say. The reference.
    classic,     ///< Peak detection with a release that slows as it closes.
    vocal,       ///< Loudness rather than transients, and hold to spare.
    percussive,  ///< Fast hands, short hold. Snare and tom gating.
    smooth,      ///< Slow and heavily program dependent. An expander, not a gate.
    bus          ///< The gentlest: long window, two release stages.
};

inline constexpr int numStyles = 6;

/** Which way round the plugin works. FROZEN ORDER. */
enum class Direction
{
    /** Quiet material gets quieter: the gate and the expander. */
    gate = 0,

    /** Loud material on the detector pulls the signal down. With an external
        sidechain this is the voice-over duck; with the internal one it is a
        plain downward compressor with a range, which is occasionally what you
        want and always what someone tries first.
    */
    ducking
};

/** How the two channels are handled. FROZEN ORDER.

    Linking is not a detail on a gate, it is the difference between a stereo
    image that stays put and one that wanders: two independent gates on a stereo
    pair will open at slightly different moments and the picture will jump.
*/
enum class ChannelMode
{
    linked = 0,  ///< One gate, driven by whichever side is louder.
    dual,        ///< Two independent gates, left and right.
    midSide      ///< Two independent gates, on mid and on side.
};

/** What opens the gate. FROZEN ORDER. */
enum class Trigger
{
    audio = 0,   ///< The detector, as usual.

    /** A MIDI note. The gate opens on note on and closes on note off, with the
        same attack, hold and release. This is how a gated pad is played from a
        drum track, and how a live rig gates a mic from a footswitch without
        having to find a threshold that works at both soundchecks.
    */
    midi
};

/** Where the detector's signal comes from. FROZEN ORDER. */
enum class SidechainSource { internal = 0, external };

juce::StringArray styleNames();
juce::StringArray directionNames();
juce::StringArray channelModeNames();
juce::StringArray triggerNames();
juce::StringArray sidechainSourceNames();

/** One line under the style buttons, so the difference is readable without
    having to A/B all six.
*/
juce::String styleDescription (GateStyle style);

/** The multipliers and structural flags that make up a style. */
struct StyleTraits
{
    float attackScale   { 1.0f };
    float releaseScale  { 1.0f };
    float holdAddMs     { 0.0f };   ///< added to the hold the user dialled
    float kneeScale     { 1.0f };
    float programDepend { 0.0f };   ///< release slows down as the gate closes
    float dualStage     { 0.0f };   ///< amount of a second, slower release
    float rmsMs         { 0.0f };   ///< 0 = peak detection, otherwise the window
};

StyleTraits styleTraits (GateStyle style);

/** Everything the engine needs, resolved from parameters. Plain data so it can
    be copied from the message thread to the audio thread without locking.
*/
struct GateSettings
{
    float thresholdDb { -40.0f };

    /** 1:1 is no expansion at all; the top of the range is a gate. The slope of
        the expansion line is ratio - 1, so 2:1 takes a signal 10 dB under the
        threshold down another 10 dB, and 200:1 takes it to the floor.
    */
    float ratio       { 200.0f };

    /** The most the gate is allowed to take off. This is the control that makes
        a gate usable on anything with a room in it: closing all the way sounds
        like the tape stopping, and 12 dB of range usually does the job.
    */
    float rangeDb     { 60.0f };

    float attackMs    { 1.0f };
    float holdMs      { 20.0f };
    float releaseMs   { 150.0f };
    float kneeDb      { 3.0f };

    /** How far below the opening threshold the gate has to fall before it will
        close again. Without it, material sitting right at the threshold opens
        and closes on every cycle and you hear it as chatter.
    */
    float hysteresisDb { 3.0f };

    float lookaheadMs { 0.0f };
    float mix         { 1.0f };      ///< 0 = dry, 1 = fully gated

    GateStyle style { GateStyle::clean };
    Direction direction { Direction::gate };
    ChannelMode channelMode { ChannelMode::linked };
    Trigger trigger { Trigger::audio };

    SidechainSource sidechain { SidechainSource::internal };
    float scHighpassHz { 20.0f };    ///< at the minimum the filter is bypassed
    float scLowpassHz  { 20000.0f }; ///< at the maximum the filter is bypassed
    bool  scListen     { false };
};

/** Parameter identifiers, written into every saved session. Frozen forever. */
namespace ids
{
    inline const juce::String bypass      { "bypass" };
    inline const juce::String threshold   { "threshold" };
    inline const juce::String ratio       { "ratio" };
    inline const juce::String range       { "range" };
    inline const juce::String attack      { "attack" };
    inline const juce::String hold        { "hold" };
    inline const juce::String release     { "release" };
    inline const juce::String knee        { "knee" };
    inline const juce::String hysteresis  { "hysteresis" };
    inline const juce::String lookahead   { "lookahead" };
    inline const juce::String mix         { "mix" };
    inline const juce::String style       { "style" };
    inline const juce::String direction   { "direction" };
    inline const juce::String channelMode { "channelMode" };
    inline const juce::String trigger     { "trigger" };
    inline const juce::String scSource    { "scSource" };
    inline const juce::String scHighpass  { "scHighpass" };
    inline const juce::String scLowpass   { "scLowpass" };
    inline const juce::String scListen    { "scListen" };
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/** Reads the whole control set out of the parameter tree. */
GateSettings readSettings (juce::AudioProcessorValueTreeState& state);

inline bool sidechainFiltersActive (const GateSettings& s) noexcept
{
    return s.scHighpassHz > 20.5f || s.scLowpassHz < 19500.0f;
}

/** The slope of the expansion line, in dB out per dB in beyond the threshold.

    A gate is an expander with a slope steep enough that the range does all the
    work, so there is no special case for "infinite": the top of the ratio knob
    is 200:1, which reaches a 60 dB floor within a third of a decibel.
*/
inline float expansionSlope (float ratio) noexcept
{
    return juce::jmax (0.0f, ratio - 1.0f);
}
} // namespace nodo::gate
