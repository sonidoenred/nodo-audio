#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <nodo_core/nodo_core.h>

#include "DecayCurve.h"

namespace nodo::verb
{
/** How many delay lines the network runs. Eight rather than sixteen: with four
    allpasses of diffusion at the input the tail is already dense, and the
    second eight would double the cost of the damping filters — which are the
    expensive part — to make a difference nobody has been able to hear.
*/
inline constexpr int numLines = 8;

struct VerbSettings
{
    float mix { 0.3f };
    float predelayMs { 0.0f };

    /** Scales every delay line. Small is a room, large is a hall, and the decay
        time is set separately — which is the opposite of how a real space
        works and exactly what makes a reverb useful.
    */
    float size { 1.0f };

    DecayCurve decay;

    /** How much the input is smeared before it reaches the network. At zero the
        network is fed the signal itself and the first few repeats are audible
        as repeats; at one it arrives as a cloud.
    */
    float diffusion { 0.7f };

    /** Slow movement of the line lengths. Some of it is not optional: eight
        fixed delays ring on eight fixed frequencies and the tail goes metallic.
        The control decides how much beyond that, from barely there to obviously
        modulated.
    */
    float motion { 0.25f };

    float width { 1.0f };
    bool freeze { false };

    /** Filters on the way in, not on the way out: keeping the bottom out of the
        network stops the tail turning to mud, and doing it before means the
        energy was never in there to begin with.
    */
    float preLowCutHz { 100.0f };
    float preHighCutHz { 12000.0f };

    // ---- post EQ, after the reverb and before the mix ----------------------
    float postLowFreq { 200.0f },  postLowGain { 0.0f };
    float postMidFreq { 1200.0f }, postMidGain { 0.0f }, postMidQ { 1.0f };
    float postHighFreq { 6000.0f }, postHighGain { 0.0f };

    // ---- ducking ----------------------------------------------------------
    float duckAmount { 0.0f };
    float duckAttackMs { 10.0f };
    float duckReleaseMs { 250.0f };
};

namespace ids
{
    inline const juce::String bypass       { "bypass" };
    inline const juce::String mix          { "mix" };
    inline const juce::String predelay     { "predelay" };
    inline const juce::String size         { "size" };
    inline const juce::String decay        { "decay" };
    inline const juce::String decayLow     { "decayLow" };
    inline const juce::String decayLowFreq { "decayLowFreq" };
    inline const juce::String decayHigh    { "decayHigh" };
    inline const juce::String decayHighFreq { "decayHighFreq" };
    inline const juce::String diffusion    { "diffusion" };
    inline const juce::String motion       { "motion" };
    inline const juce::String width        { "width" };
    inline const juce::String freeze       { "freeze" };
    inline const juce::String preLowCut    { "preLowCut" };
    inline const juce::String preHighCut   { "preHighCut" };
    inline const juce::String postLowFreq  { "postLowFreq" };
    inline const juce::String postLowGain  { "postLowGain" };
    inline const juce::String postMidFreq  { "postMidFreq" };
    inline const juce::String postMidGain  { "postMidGain" };
    inline const juce::String postMidQ     { "postMidQ" };
    inline const juce::String postHighFreq { "postHighFreq" };
    inline const juce::String postHighGain { "postHighGain" };
    inline const juce::String duckAmount   { "duckAmount" };
    inline const juce::String duckAttack   { "duckAttack" };
    inline const juce::String duckRelease  { "duckRelease" };
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
VerbSettings readSettings (juce::AudioProcessorValueTreeState& state);

/** The length of each delay line, in samples, at a given size. Shared so the
    engine, the decay design and the tests all agree on what a line is.
*/
int lineLengthSamples (int line, float size, double sampleRate);

inline bool postEqActive (const VerbSettings& s) noexcept
{
    return std::abs (s.postLowGain) > 0.01f
        || std::abs (s.postMidGain) > 0.01f
        || std::abs (s.postHighGain) > 0.01f;
}
} // namespace nodo::verb
