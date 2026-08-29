#include "DecayCurve.h"

namespace nodo::verb
{
namespace
{
    constexpr double shelfQ = 0.707;

    /*  The shelves are normalised at a one decibel gain rather than at the gain
        they will actually be given.

        The gains involved are tiny — a two second decay on a fifteen hundred
        sample line is under a decibel of attenuation per pass — and in that
        region an RBJ shelf's shape is as good as independent of its gain. Using
        a fixed small gain to define the shape means the curve the display draws
        is one expression rather than sixteen slightly different ones, and the
        test that measures the real decay says how much that costs: it does not
        cost anything that can be measured.
    */
    constexpr double referenceGainDb = 1.0;

    double normalisedShelf (bool low, float hz, float cornerHz, double sampleRate)
    {
        const auto corner = juce::jlimit (20.0, sampleRate * 0.45, (double) cornerHz);

        const auto coefficients = low
            ? dsp::BiquadCoefficients::lowShelf (sampleRate, corner, shelfQ, referenceGainDb)
            : dsp::BiquadCoefficients::highShelf (sampleRate, corner, shelfQ, referenceGainDb);

        const auto magnitude = coefficients.magnitudeAt (juce::jlimit (1.0, sampleRate * 0.499,
                                                                       (double) hz),
                                                         sampleRate);

        return 20.0 * std::log10 (juce::jmax (1.0e-9, magnitude)) / referenceGainDb;
    }
}

float DecayCurve::shapeAt (float hz, double sampleRate) const
{
    const auto low = normalisedShelf (true, hz, lowFrequency, sampleRate);
    const auto high = normalisedShelf (false, hz, highFrequency, sampleRate);

    const auto shape = 1.0
                     + low * (1.0 / juce::jmax (0.05f, lowMultiplier) - 1.0)
                     + high * (1.0 / juce::jmax (0.05f, highMultiplier) - 1.0);

    return (float) juce::jmax (0.05, shape);
}

float LineDamping::responseDb (float hz, double sampleRate) const
{
    const auto frequency = juce::jlimit (1.0, sampleRate * 0.499, (double) hz);

    const auto shelves = lowShelf.magnitudeAt (frequency, sampleRate)
                       * highShelf.magnitudeAt (frequency, sampleRate);

    return 20.0f * std::log10 ((float) juce::jmax (1.0e-9, shelves))
         + 20.0f * std::log10 (juce::jmax (1.0e-9f, broadbandGain));
}

LineDamping designLineDamping (const DecayCurve& curve, int lineLengthSamples, double sampleRate)
{
    LineDamping damping;

    const auto length = (double) juce::jmax (1, lineLengthSamples);
    const auto seconds = juce::jmax (0.02, (double) curve.midSeconds);

    /*  The number the whole reverb hangs from: what one pass round a line of
        this length has to be multiplied by for the level to fall 60 dB in the
        decay time.
    */
    const auto midDb = -60.0 * length / (seconds * sampleRate);

    // The shelves lift or lower that figure at the two ends. A multiplier of
    // two means half as many decibels of attenuation, so the shelf gain is
    // midDb * (1/2 - 1) — a positive number, since midDb is negative.
    const auto lowGain = midDb * (1.0 / juce::jmax (0.05f, curve.lowMultiplier) - 1.0);
    const auto highGain = midDb * (1.0 / juce::jmax (0.05f, curve.highMultiplier) - 1.0);

    damping.lowShelf = dsp::BiquadCoefficients::lowShelf (
        sampleRate, juce::jlimit (20.0, sampleRate * 0.45, (double) curve.lowFrequency),
        shelfQ, lowGain);

    damping.highShelf = dsp::BiquadCoefficients::highShelf (
        sampleRate, juce::jlimit (20.0, sampleRate * 0.45, (double) curve.highFrequency),
        shelfQ, highGain);

    damping.broadbandGain = (float) std::pow (10.0, midDb / 20.0);

    return damping;
}
} // namespace nodo::verb
