#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <nodo_core/nodo_core.h>

namespace nodo::limit
{
/** How the gain curve is shaped on its way in and out of limiting.

    Same idea as the compressor's styles and the same honesty rule: a style is a
    set of numbers applied to one algorithm, all of them visible in
    styleTraits(). What changes is how much of the lookahead window is spent
    smoothing the attack, how long the release is, and how much of the work is
    handed to a second, slower stage that holds the level between peaks.

    THIS ORDER IS FROZEN. The index goes into every saved session.
*/
enum class LimitStyle
{
    transparent = 0, ///< Longest smoothing, slowest release. Gets out of the way.
    punchy,          ///< Short smoothing so transients keep their edge.
    modern,          ///< Fast, dense, loud. What most masters are asking for.
    bus,             ///< Two stages: fast for hits, slow for the section.
    safe             ///< A brick wall that barely moves. For catching accidents.
};

inline constexpr int numStyles = 5;

juce::StringArray styleNames();
juce::String styleDescription (LimitStyle style);

struct StyleTraits
{
    /** Fraction of the lookahead window used to smooth the attack. Less means a
        sharper corner, which lets more transient through and sounds punchier at
        the cost of being more audible.
    */
    float smoothingFraction { 1.0f };

    float releaseScale  { 1.0f };
    float programDepend { 0.0f };   ///< release slows down with deeper limiting
    float dualStage     { 0.0f };   ///< amount of a second, slower release stage
};

StyleTraits styleTraits (LimitStyle style);

/** Dither applied on the way out. FROZEN ORDER. */
enum class DitherMode { off = 0, sixteenBit, twentyFourBit };

juce::StringArray ditherNames();

/** Loudness targets worth having one click away. FROZEN ORDER.

    These are the numbers people are actually aiming at when they reach for a
    free limiter: the plugin already measures the loudness, so refusing to say
    how far off the target it is would be withholding the answer to the question
    that made them open it.
*/
enum class LoudnessTarget { off = 0, club, streaming, apple, broadcast };

inline constexpr int numTargets = 5;

juce::StringArray targetNames();

/** The figure each target stands for, in LUFS. */
float targetLufs (LoudnessTarget target);

struct LimitSettings
{
    float inputGainDb { 0.0f };
    float ceilingDb   { -1.0f };
    float lookaheadMs { 5.0f };
    float releaseMs   { 200.0f };

    LimitStyle style { LimitStyle::transparent };

    /** Inter-sample peaks are found by measuring the signal four times
        oversampled, which is what BS.1770 does and what a converter will
        actually reconstruct.
    */
    bool truePeak { true };

    /** How much the two channels share their gain. Separated into the fast and
        the slow path because they want different answers: linking transients
        keeps the image stable, linking the release keeps it from breathing
        sideways.
    */
    float transientLink { 1.0f };
    float releaseLink   { 1.0f };

    LoudnessTarget target { LoudnessTarget::off };

    /** Bypass at matched loudness, so the comparison is about the sound rather
        than about which one is louder.
    */
    bool  bypassMatch { true };

    bool  dcFilter { false };
    bool  externalTrigger { false };

    /** Outputs what the limiter removed rather than what it kept. */
    bool  delta { false };

    DitherMode dither { DitherMode::off };
};

namespace ids
{
    inline const juce::String bypass        { "bypass" };
    inline const juce::String inputGain     { "inputGain" };
    inline const juce::String ceiling       { "ceiling" };
    inline const juce::String lookahead     { "lookahead" };
    inline const juce::String release       { "release" };
    inline const juce::String style         { "style" };
    inline const juce::String truePeak      { "truePeak" };
    inline const juce::String transientLink { "transientLink" };
    inline const juce::String releaseLink   { "releaseLink" };
    inline const juce::String dcFilter      { "dcFilter" };
    inline const juce::String externalTrigger { "externalTrigger" };
    inline const juce::String delta         { "delta" };
    inline const juce::String dither        { "dither" };
    inline const juce::String target        { "target" };
    inline const juce::String bypassMatch   { "bypassMatch" };
}

/*  There is deliberately no oversampling control. The reason a limiter
    oversamples is inter-sample peaks, and those are already handled by
    measuring the detector four times oversampled — see LimitSettings::truePeak.
    What would be left is aliasing from the gain modulation itself, and this
    limiter's gain is smoothed by two boxcars precisely so that it does not move
    fast enough to produce any worth removing.
*/
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
LimitSettings readSettings (juce::AudioProcessorValueTreeState& state);
} // namespace nodo::limit
