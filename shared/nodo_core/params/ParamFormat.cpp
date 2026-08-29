namespace nodo::format
{
juce::String frequency (float hz, int)
{
    if (hz >= 10000.0f) return juce::String (hz / 1000.0f, 1) + " kHz";
    if (hz >= 1000.0f)  return juce::String (hz / 1000.0f, 2) + " kHz";
    if (hz >= 100.0f)   return juce::String (juce::roundToInt (hz)) + " Hz";
    return juce::String (hz, 1) + " Hz";
}

float frequencyFromText (const juce::String& text)
{
    auto t = text.trim().toLowerCase();
    const auto value = t.getFloatValue();
    return t.contains ("k") ? value * 1000.0f : value;
}

juce::String decibels (float db, int)
{
    const auto rounded = std::abs (db) < 0.05f ? 0.0f : db;
    return (rounded > 0.0f ? "+" : "") + juce::String (rounded, 1) + " dB";
}

float decibelsFromText (const juce::String& text)
{
    return text.trim().getFloatValue();
}

juce::String quality (float q, int)
{
    return juce::String (q, q < 10.0f ? 2 : 1);
}

juce::String slope (float dbPerOct, int)
{
    return juce::String (juce::roundToInt (dbPerOct)) + " dB/oct";
}

juce::String milliseconds (float ms, int)
{
    if (ms >= 1000.0f) return juce::String (ms / 1000.0f, 2) + " s";
    if (ms >= 100.0f)  return juce::String (juce::roundToInt (ms)) + " ms";
    return juce::String (ms, 1) + " ms";
}

juce::String percent (float zeroToOne, int)
{
    return juce::String (juce::roundToInt (zeroToOne * 100.0f)) + " %";
}

juce::String ratio (float value, int)
{
    return value >= 19.5f ? juce::String ("inf:1")
                          : juce::String (value, value < 10.0f ? 1 : 0) + ":1";
}

juce::String noteName (float hz)
{
    if (hz <= 0.0f)
        return {};

    static const char* names[] = { "C", "C#", "D", "D#", "E", "F",
                                   "F#", "G", "G#", "A", "A#", "B" };

    // MIDI note 69 == A4 == 440 Hz.
    const auto midi = juce::roundToInt (69.0 + 12.0 * std::log2 (hz / 440.0));
    const auto clamped = juce::jlimit (0, 127, midi);
    return juce::String (names[clamped % 12]) + juce::String (clamped / 12 - 1);
}
} // namespace nodo::format

namespace nodo::ranges
{
juce::NormalisableRange<float> frequency (float min, float max)
{
    juce::NormalisableRange<float> r { min, max };
    // Logarithmic: an octave takes the same knob travel everywhere.
    r.setSkewForCentre (std::sqrt (min * max));
    return r;
}

juce::NormalisableRange<float> gainDb (float rangeDb)
{
    juce::NormalisableRange<float> r { -rangeDb, rangeDb, 0.01f };
    r.symmetricSkew = true;
    r.setSkewForCentre (0.0f);
    return r;
}

juce::NormalisableRange<float> quality()
{
    juce::NormalisableRange<float> r { 0.1f, 18.0f };
    r.setSkewForCentre (1.0f);
    return r;
}

juce::NormalisableRange<float> ratio()
{
    juce::NormalisableRange<float> r { 1.0f, 20.0f };
    r.setSkewForCentre (3.0f);
    return r;
}

juce::NormalisableRange<float> timeMs (float min, float max, float centre)
{
    juce::NormalisableRange<float> r { min, max };
    r.setSkewForCentre (centre);
    return r;
}
} // namespace nodo::ranges
