#pragma once

#include <nodo_core/nodo_core.h>

namespace nodo::verb
{
/** How long the reverb takes to fall 60 dB, as a function of frequency.

    This is the piece the whole plugin is built around, so it lives on its own
    and the engine, the display and the tests all read it. A reverb that draws a
    decay time and produces a different one is worse than one that draws
    nothing, and the only way to be sure is for there to be exactly one
    definition of what the answer is.

    The model is a mid decay time with a multiplier at each end: the low
    multiplier says how many times longer (or shorter) the bottom takes, the
    high multiplier the same for the top. Two is a bass that hangs on twice as
    long as the body — a real hall. A quarter at the top is a room with curtains
    in it.
*/
struct DecayCurve
{
    float midSeconds { 2.0f };
    float lowMultiplier { 1.0f };
    float lowFrequency { 250.0f };
    float highMultiplier { 0.6f };
    float highFrequency { 4000.0f };

    /** The number the mid decay is divided by at this frequency: 1 in the
        middle, 1/lowMultiplier at the bottom, 1/highMultiplier at the top, and
        the shelves' own shapes in between.
    */
    float shapeAt (float hz, double sampleRate) const;

    /** Decay time at a frequency, in seconds. What the display draws and what
        the tests measure against.
    */
    float secondsAt (float hz, double sampleRate) const
    {
        return midSeconds / juce::jmax (0.05f, shapeAt (hz, sampleRate));
    }
};

/** The damping filter for one delay line, derived from the curve above.

    In a feedback delay network with an energy-preserving mixing matrix, the
    decay is decided entirely by what each line multiplies itself by on the way
    round. For a line of L samples to decay 60 dB in T seconds, that multiplier
    must be -60 L / (T fs) decibels. Make T a function of frequency and the
    multiplier becomes a filter whose magnitude is that expression — which is
    the whole of the design, and the reason it can be checked against a
    measurement rather than against an opinion.
*/
struct LineDamping
{
    dsp::BiquadCoefficients lowShelf, highShelf;
    float broadbandGain { 0.0f };

    /** The attenuation this line applies per pass at a frequency, in dB. Used
        by the tests to check the design before any audio is run through it.
    */
    float responseDb (float hz, double sampleRate) const;
};

LineDamping designLineDamping (const DecayCurve& curve, int lineLengthSamples, double sampleRate);
} // namespace nodo::verb
