#pragma once

#include <cmath>
#include <juce_core/juce_core.h>

namespace nodo::dsp
{
/** RBJ cookbook biquad coefficients, normalised so that a0 == 1.

    This exists instead of juce::dsp::IIR::Coefficients for one reason: the JUCE
    factory functions allocate a reference-counted object every time they are
    called. The EQ recomputes coefficients every 32 samples to keep parameter
    changes click-free, and allocating on the audio thread thousands of times a
    second is exactly the kind of thing that produces dropouts under load.
    These are plain values on the stack.
*/
struct BiquadCoefficients
{
    double b0 { 1.0 }, b1 { 0.0 }, b2 { 0.0 }, a1 { 0.0 }, a2 { 0.0 };

    static BiquadCoefficients peak      (double sampleRate, double frequency, double q, double gainDb) noexcept;
    static BiquadCoefficients lowShelf  (double sampleRate, double frequency, double q, double gainDb) noexcept;
    static BiquadCoefficients highShelf (double sampleRate, double frequency, double q, double gainDb) noexcept;
    static BiquadCoefficients notch     (double sampleRate, double frequency, double q) noexcept;
    static BiquadCoefficients highPass  (double sampleRate, double frequency, double q) noexcept;
    static BiquadCoefficients lowPass   (double sampleRate, double frequency, double q) noexcept;

    /** Constant peak gain band pass: unity at the centre, falling either side. */
    static BiquadCoefficients bandPass  (double sampleRate, double frequency, double q) noexcept;

    /** Flat magnitude, rotating phase through 360 degrees around the frequency.
        Useful for phase alignment between mics rather than for tone shaping.
    */
    static BiquadCoefficients allPass   (double sampleRate, double frequency, double q) noexcept;

    /** Linear magnitude of this section at a frequency. GUI use only. */
    double magnitudeAt (double frequency, double sampleRate) const noexcept;
};

/** Transposed direct form II biquad. Two state variables, no allocation, safe to
    call from the audio thread.

    TDF-II is the usual choice for time-varying filters: it behaves better than
    direct form I when coefficients change between samples, which is precisely
    what happens while a user drags a band around.
*/
class Biquad
{
public:
    void setCoefficients (const BiquadCoefficients& newCoefficients) noexcept
    {
        coefficients = newCoefficients;
    }

    void reset() noexcept
    {
        s1 = 0.0;
        s2 = 0.0;
    }

    inline float processSample (float input) noexcept
    {
        const auto x = (double) input;
        const auto y = coefficients.b0 * x + s1;

        s1 = coefficients.b1 * x - coefficients.a1 * y + s2;
        s2 = coefficients.b2 * x - coefficients.a2 * y;

        return (float) y;
    }

    /** Flushes denormals, which otherwise make the CPU crawl once a track goes
        silent and the filter state decays towards zero.
    */
    void snapToZero() noexcept
    {
        if (! std::isfinite (s1) || std::abs (s1) < 1.0e-15) s1 = 0.0;
        if (! std::isfinite (s2) || std::abs (s2) < 1.0e-15) s2 = 0.0;
    }

private:
    BiquadCoefficients coefficients;
    double s1 { 0.0 }, s2 { 0.0 };
};
} // namespace nodo::dsp
