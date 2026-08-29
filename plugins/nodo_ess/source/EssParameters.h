#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <nodo_core/nodo_core.h>

namespace nodo::ess
{
/** What the gain is applied to. FROZEN ORDER.

    The two honest answers to "how do you take an S down", and they fail in
    opposite directions:

    - Wideband turns the whole signal down for the length of the sibilant, which
      keeps the voice's tone intact but pulls the body of the word with it. Push
      it and the vocal ducks on every S.
    - Split turns down only the band above the crossover, which leaves the body
      alone but, pushed hard, takes the air with the sibilance and starts to
      lisp.

    There is no third option that is better than both, so the plugin offers the
    choice rather than picking for you.
*/
enum class Mode { wideband = 0, split };

/** How the threshold is decided. FROZEN ORDER. */
enum class Detection
{
    absolute = 0,  ///< A fixed level. Predictable, and needs resetting per take.
    adaptive       ///< Follows the running level of the sibilant band.
};

juce::StringArray modeNames();
juce::StringArray detectionNames();

/** Which part of the stereo image is de-essed. FROZEN ORDER.

    A lead vocal sits in the middle, so de-essing the mid alone leaves the
    reverb, the doubles and everything else in the sides untouched. Side only is
    rarer and more surgical: it is for when the sibilance is coming from the
    widened doubles rather than from the voice itself.
*/
enum class Channel { stereo = 0, mid, side };

juce::StringArray channelNames();

/** What comes out of the plugin, for checking your work. FROZEN ORDER. */
enum class Monitor
{
    normal = 0,
    band,       ///< Only the band being watched.
    difference  ///< Only what is being removed.
};

juce::StringArray monitorNames();

struct EssSettings
{
    float frequencyHz { 6500.0f };

    /** The top of the detector's band. Sibilance is a band, not everything above
        a corner: without this, cymbals and air above the sibilant region drag
        the detector along with them.
    */
    float detectorTopHz { 20000.0f };
    float thresholdDb { -24.0f };
    float rangeDb     { 12.0f };
    float attackMs    { 1.0f };
    float releaseMs   { 60.0f };
    float lookaheadMs { 0.0f };
    float kneeDb      { 4.0f };
    float stereoLink  { 1.0f };

    Mode mode { Mode::split };
    Channel channel { Channel::stereo };
    Detection detection { Detection::adaptive };
    Monitor monitor { Monitor::normal };
};

namespace ids
{
    inline const juce::String bypass     { "bypass" };
    inline const juce::String frequency  { "frequency" };
    inline const juce::String detectorTop { "detectorTop" };
    inline const juce::String channel    { "channel" };
    inline const juce::String threshold  { "threshold" };
    inline const juce::String range      { "range" };
    inline const juce::String attack     { "attack" };
    inline const juce::String release    { "release" };
    inline const juce::String lookahead  { "lookahead" };
    inline const juce::String knee       { "knee" };
    inline const juce::String stereoLink { "stereoLink" };
    inline const juce::String mode       { "mode" };
    inline const juce::String detection  { "detection" };
    inline const juce::String monitor    { "monitor" };
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
EssSettings readSettings (juce::AudioProcessorValueTreeState& state);
} // namespace nodo::ess
