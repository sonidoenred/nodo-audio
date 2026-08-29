#pragma once

#include <array>
#include <limits>
#include "Biquad.h"

namespace nodo::dsp
{
/** Analog-matched biquad design: the cure for cramping.

    THE PROBLEM. The bilinear transform used by the RBJ cookbook maps the whole
    infinite analog frequency axis onto the finite digital one. It pre-warps the
    centre frequency so that lands correctly, but everything around it gets
    squeezed as it approaches Nyquist. Worse, a bilinear peaking filter is forced
    to exactly 0 dB at Nyquist no matter what: a wide bell at 12 kHz cannot tail
    off naturally, it gets yanked back to flat by 22 kHz. That squashed, narrowed
    top end is what people mean when they say a digital EQ sounds "brittle" or
    "cheap" up high, and it is why plugins advertise modes with names like
    Natural Phase.

    Oversampling hides it by moving Nyquist out of the way, at the cost of CPU,
    latency and a pair of resampling filters. This fixes it properly instead.

    THE METHOD (after Vicanek's matched second-order filters).

      1. Place the poles by the matched Z-transform, z = exp(sT), from the analog
         prototype's poles. This keeps the resonance exactly where the analog
         filter puts it, with no warping.

      2. Solve for the zeros by matching the squared magnitude response to the
         analog prototype at three frequencies: DC, the band frequency, and
         Nyquist. Matching at Nyquist is the crucial one — that is the constraint
         the bilinear transform cannot satisfy.

    The algebra behind step 2 comes from the identity

        |1 + a1 e^-jw + a2 e^-2jw|^2 = (1+a1+a2)^2 - 4s[a1(1+a2) + 4a2(1-s)]

    with s = sin^2(w/2), which turns the squared response into a quadratic in s
    and makes the three-point match a small linear system.

    SAFETY. Some corners of the parameter space have no valid solution (the
    recovery step needs two discriminants to stay non-negative), and a large
    shelf boost can push the prototype's natural frequency past Nyquist. In
    those cases the design falls back to the plain RBJ filter, which is always
    well behaved. A cramped filter is a small problem; an unstable one is not.
*/

/** Analog prototype magnitudes, as a function of u = frequency / band frequency.
    A is sqrt(gain), so A^2 is the linear gain at the band's extreme.
    These return magnitude SQUARED.
*/
namespace analog
{
    inline double peakMagnitudeSquared (double u, double A, double q) noexcept
    {
        const auto u2 = u * u;
        const auto flat = (1.0 - u2) * (1.0 - u2);
        const auto num = flat + (A * u / q) * (A * u / q);
        const auto den = flat + (u / (A * q)) * (u / (A * q));
        return den > 0.0 ? num / den : 1.0;
    }

    inline double lowShelfMagnitudeSquared (double u, double A, double q) noexcept
    {
        const auto u2 = u * u;
        const auto common = A * u2 / (q * q);
        const auto num = (A - u2) * (A - u2) + common;
        const auto den = (1.0 - A * u2) * (1.0 - A * u2) + common;
        return den > 0.0 ? A * A * num / den : A * A;
    }

    /** Second order low pass: H(s) = 1 / (s^2 + s/q + 1). */
    inline double lowPassMagnitudeSquared (double u, double q) noexcept
    {
        const auto u2 = u * u;
        const auto den = (1.0 - u2) * (1.0 - u2) + (u / q) * (u / q);
        return den > 0.0 ? 1.0 / den : 1.0;
    }

    /** Second order high pass: H(s) = s^2 / (s^2 + s/q + 1). */
    inline double highPassMagnitudeSquared (double u, double q) noexcept
    {
        const auto u2 = u * u;
        const auto den = (1.0 - u2) * (1.0 - u2) + (u / q) * (u / q);
        return den > 0.0 ? u2 * u2 / den : 1.0;
    }

    inline double highShelfMagnitudeSquared (double u, double A, double q) noexcept
    {
        const auto u2 = u * u;
        const auto common = A * u2 / (q * q);
        const auto num = (A * u2 - 1.0) * (A * u2 - 1.0) + common;
        const auto den = (u2 - A) * (u2 - A) + common;
        return den > 0.0 ? A * A * num / den : A * A;
    }
}

/** @returns true when the analog-matched design succeeded; false means the
    caller should use the RBJ design instead.
*/
bool designMatchedPeak (double sampleRate, double frequency, double q, double gainDb,
                        BiquadCoefficients& result) noexcept;

bool designMatchedLowShelf (double sampleRate, double frequency, double q, double gainDb,
                            BiquadCoefficients& result) noexcept;

bool designMatchedHighShelf (double sampleRate, double frequency, double q, double gainDb,
                             BiquadCoefficients& result) noexcept;

/** Cuts, one second order section at a time.

    A bilinear low pass has a double zero at Nyquist, so its response there is
    minus infinity while the analog prototype it claims to be still has a real,
    finite value. At 44.1 kHz a 16 kHz high cut is close enough to Nyquist for
    that to be the difference between a filter that sounds like the one you
    drew and one that is noticeably steeper at the top. The high pass has the
    mirror problem in the corner rather than at Nyquist, where the bilinear
    warping squeezes the shape.

    A cut of more than 12 dB/oct is a cascade of these, so each section is
    matched on its own with its own Butterworth Q.
*/
bool designMatchedLowPass (double sampleRate, double frequency, double q,
                           BiquadCoefficients& result) noexcept;

bool designMatchedHighPass (double sampleRate, double frequency, double q,
                            BiquadCoefficients& result) noexcept;

/** Convenience wrappers that silently fall back to the RBJ design. */
BiquadCoefficients matchedPeak      (double sampleRate, double frequency, double q, double gainDb) noexcept;
BiquadCoefficients matchedLowShelf  (double sampleRate, double frequency, double q, double gainDb) noexcept;
BiquadCoefficients matchedHighShelf (double sampleRate, double frequency, double q, double gainDb) noexcept;
BiquadCoefficients matchedLowPass   (double sampleRate, double frequency, double q) noexcept;
BiquadCoefficients matchedHighPass  (double sampleRate, double frequency, double q) noexcept;
} // namespace nodo::dsp
