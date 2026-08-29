#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <nodo_core/nodo_core.h>

namespace nodo::comp
{
/** How the compressor behaves before the knobs are touched.

    A style is not a different compressor: it is a set of multipliers applied on
    top of what the user asked for, plus two structural choices (feedback
    detection and dual stage release). Everything a style does is visible in
    styleTraits() below, which is deliberate — a "Vintage" mode that cannot be
    explained is a mode that cannot be debugged.

    THIS ORDER IS FROZEN. The index is written into every saved session.
*/
enum class CompStyle
{
    clean = 0,   ///< Does exactly what the knobs say. The reference.
    punch,       ///< Harder knee, quicker hands. Drums and anything with a transient.
    opto,        ///< Soft, slow, feedback detection. Vocals, bass, gentle levelling.
    vocal,       ///< Wide knee and a release that follows the phrase.
    bus,         ///< Dual stage release. Glue rather than control.
    classic,     ///< Feedback detection with a moderate hand. The all rounder.
    mastering,   ///< Widest knee, slowest hands. Two dB, and you should not hear it.
    smooth,      ///< Heavily program dependent: rides the level instead of catching peaks.
    pumping      ///< Very short release, hard knee. The effect, on purpose.
};

inline constexpr int numStyles = 9;

/** What the detector listens to. FROZEN ORDER. */
enum class Detection { peak = 0, rms };

/** Which way the compressor works. FROZEN ORDER.

    Downward is the familiar one: loud material gets quieter. Upward lifts what
    falls *below* the threshold, which is how you hear the tail of a note or the
    room in a drum kit without touching the hits. It is also how you amplify a
    noise floor, so it comes with the range control turned down by default.
*/
enum class Direction { downward = 0, upward };

/** Where the detector's signal comes from. FROZEN ORDER. */
enum class SidechainSource { internal = 0, external };

juce::StringArray styleNames();
juce::StringArray detectionNames();
juce::StringArray sidechainSourceNames();
juce::StringArray directionNames();

/** One line under the style buttons, so the difference is readable without
    having to A/B every one of them.
*/
juce::String styleDescription (CompStyle style);

/** The multipliers and structural flags that make up a style. */
struct StyleTraits
{
    float kneeScale     { 1.0f };   ///< multiplies the knee the user dialled
    float attackScale   { 1.0f };
    float releaseScale  { 1.0f };
    float programDepend { 0.0f };   ///< release slows down with deeper reduction
    float dualStage     { 0.0f };   ///< amount of the second, slower release
    float rmsScale      { 1.0f };   ///< multiplies the RMS window
    bool  feedback      { false };  ///< detector reads the output, not the input
};

StyleTraits styleTraits (CompStyle style);

/** Everything the engine needs, resolved from parameters. Plain data so it can
    be copied from the message thread to the audio thread without locking.
*/
struct CompSettings
{
    float thresholdDb { -18.0f };
    float ratio       { 4.0f };
    float kneeDb      { 6.0f };
    float attackMs    { 10.0f };
    float releaseMs   { 120.0f };
    float holdMs      { 0.0f };

    /** The most the compressor is allowed to move the gain, in dB. */
    float rangeDb     { 60.0f };

    Direction direction { Direction::downward };

    /** The threshold follows the running level of the material, so the same
        setting works on a quiet take and a loud one.
    */
    bool  autoThreshold { false };

    /** The release adapts to the depth of the reduction instead of obeying the
        knob exactly.
    */
    bool  autoRelease   { false };

    float makeupDb    { 0.0f };
    bool  autoGain    { false };
    float mix         { 1.0f };      ///< 0 = dry, 1 = fully compressed

    CompStyle style   { CompStyle::clean };
    Detection detection { Detection::peak };
    float stereoLink  { 1.0f };      ///< 0 = two independent compressors, 1 = locked
    float lookaheadMs { 0.0f };

    SidechainSource sidechain { SidechainSource::internal };
    float scHighpassHz { 20.0f };    ///< at the minimum the filter is bypassed
    float scLowpassHz  { 20000.0f }; ///< at the maximum the filter is bypassed
    bool  scListen     { false };
};

/** Parameter identifiers, written into every saved session. Frozen forever. */
namespace ids
{
    inline const juce::String bypass       { "bypass" };
    inline const juce::String threshold    { "threshold" };
    inline const juce::String ratio        { "ratio" };
    inline const juce::String knee         { "knee" };
    inline const juce::String attack       { "attack" };
    inline const juce::String release      { "release" };
    inline const juce::String hold         { "hold" };
    inline const juce::String range        { "range" };
    inline const juce::String direction    { "direction" };
    inline const juce::String autoThreshold { "autoThreshold" };
    inline const juce::String autoRelease  { "autoRelease" };
    inline const juce::String makeup       { "makeup" };
    inline const juce::String autoGain     { "autoGain" };
    inline const juce::String mix          { "mix" };
    inline const juce::String style        { "style" };
    inline const juce::String detection    { "detection" };
    inline const juce::String stereoLink   { "stereoLink" };
    inline const juce::String lookahead    { "lookahead" };
    inline const juce::String scSource     { "scSource" };
    inline const juce::String scHighpass   { "scHighpass" };
    inline const juce::String scLowpass    { "scLowpass" };
    inline const juce::String scListen     { "scListen" };
    inline const juce::String oversampling { "oversampling" };
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/** Reads the whole control set out of the parameter tree. */
CompSettings readSettings (juce::AudioProcessorValueTreeState& state);

/** True when the sidechain filters are doing nothing, so the engine can skip
    them entirely rather than running two transparent biquads per sample.
*/
inline bool sidechainFiltersActive (const CompSettings& s) noexcept
{
    return s.scHighpassHz > 20.5f || s.scLowpassHz < 19500.0f;
}
} // namespace nodo::comp
