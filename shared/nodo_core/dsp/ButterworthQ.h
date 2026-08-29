#pragma once

#include <array>
#include <cmath>

namespace nodo::dsp
{
/** The most stages a cut can be built from: 8 biquads == 96 dB per octave. */
inline constexpr int maxButterworthStages = 8;

/** Q values for a Butterworth cascade.

    A high/low cut of order 2N is built from N cascaded biquads. Each stage needs
    its own Q so that the combined response is maximally flat in the passband:

        Q_k = 1 / (2 * cos(pi * (2k + 1) / (2 * order)))   for k = 0 .. N-1

    Sanity check: order 2 gives 0.7071 (the textbook Butterworth Q); order 4
    gives 0.5412 and 1.3066. Give every stage the same 0.7071 instead and the
    passband develops a visible bump at the corner, which is the usual giveaway
    of a home-made steep filter.

    @param stages   number of biquads (order / 2), 1..8 covers 12..96 dB/oct
*/
inline std::array<float, maxButterworthStages> butterworthQ (int stages) noexcept
{
    std::array<float, maxButterworthStages> q {};
    q.fill (0.70710678f);

    if (stages <= 0)
        return q;

    const auto order = 2 * stages;

    for (int k = 0; k < stages && k < maxButterworthStages; ++k)
    {
        const auto angle = 3.14159265358979323846 * (2.0 * k + 1.0) / (2.0 * order);
        q[(size_t) k] = (float) (1.0 / (2.0 * std::cos (angle)));
    }

    return q;
}

/** 12..96 dB per octave -> 1..8 biquad stages. */
inline int stagesForSlope (int dbPerOctave) noexcept
{
    const auto stages = (dbPerOctave + 11) / 12;
    return stages < 1 ? 1 : (stages > maxButterworthStages ? maxButterworthStages : stages);
}
} // namespace nodo::dsp
