#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace nodo
{
/** Value <-> text conversion shared by every plugin in the suite, so that a
    frequency reads the same way in the EQ, the delay and the compressor.
*/
namespace format
{
    /** 20 Hz, 440 Hz, 1.20 kHz, 12.0 kHz */
    juce::String frequency (float hz, int /*maxLen*/ = 0);
    float        frequencyFromText (const juce::String& text);

    /** -6.0 dB, +3.5 dB */
    juce::String decibels (float db, int /*maxLen*/ = 0);
    float        decibelsFromText (const juce::String& text);

    /** 0.71, 4.20 */
    juce::String quality (float q, int /*maxLen*/ = 0);

    /** 12 dB/oct */
    juce::String slope (float dbPerOct, int /*maxLen*/ = 0);

    /** 250 ms, 1.50 s */
    juce::String milliseconds (float ms, int /*maxLen*/ = 0);

    /** 50 % */
    juce::String percent (float zeroToOne, int /*maxLen*/ = 0);

    /** Musical note name nearest to a frequency, e.g. "A4". */
    juce::String noteName (float hz);

    /** 4.0:1, or "inf" at the top of the range. */
    juce::String ratio (float value, int /*maxLen*/ = 0);
}

/** Normalisable ranges shared across plugins. Skew factors are picked so the
    knob feels right, not so the maths is tidy.
*/
namespace ranges
{
    juce::NormalisableRange<float> frequency (float min = 20.0f, float max = 20000.0f);
    juce::NormalisableRange<float> gainDb (float rangeDb = 24.0f);
    juce::NormalisableRange<float> quality();

    /** 1:1 up to 20:1, weighted so the useful low ratios get most of the travel. */
    juce::NormalisableRange<float> ratio();

    /** Attack and release times, skewed so the short end is not a hair trigger. */
    juce::NormalisableRange<float> timeMs (float min, float max, float centre);
}
} // namespace nodo
