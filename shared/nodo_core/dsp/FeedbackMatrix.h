#pragma once

#include <array>
#include <cmath>
#include <juce_core/juce_core.h>

namespace nodo::dsp
{
/** The mixing matrix at the heart of a feedback delay network.

    What the matrix has to be is *orthogonal* — length preserving. That is the
    whole trick: if the matrix neither adds nor removes energy, then how fast
    the reverb decays is decided entirely by the gains on the delay lines, and
    those can be filters. Decay time becomes something you design rather than
    something you discover, which is what makes a decay time per frequency band
    possible at all.

    A Hadamard matrix is orthogonal and its entries are all plus or minus one
    over root N, so applying it costs only additions and subtractions — no
    multiplies at all beyond one scaling. Done as a fast Walsh-Hadamard
    transform it is N log N of those: 24 add-subtracts for eight channels,
    against 64 multiply-accumulates for a general matrix.

    N must be a power of two.
*/
template <int N>
inline void hadamardInPlace (std::array<float, N>& x) noexcept
{
    static_assert (N > 0 && (N & (N - 1)) == 0, "Hadamard needs a power of two");

    for (int step = 1; step < N; step <<= 1)
    {
        for (int i = 0; i < N; i += step << 1)
        {
            for (int j = i; j < i + step; ++j)
            {
                const auto a = x[(size_t) j];
                const auto b = x[(size_t) (j + step)];

                x[(size_t) j] = a + b;
                x[(size_t) (j + step)] = a - b;
            }
        }
    }

    // The transform above has a gain of root N per stage pair; normalising here
    // once is what makes the whole thing energy preserving.
    const auto normalise = 1.0f / std::sqrt ((float) N);

    for (auto& value : x)
        value *= normalise;
}

/** Delay line lengths that share no common factors.

    Two lines whose lengths have a common factor put their echoes on top of each
    other over and over, and what should be a cloud comes out as a pitch. Primes
    are the cheap way to guarantee it, and they are picked here rather than
    computed so that the same lengths come out on every machine and every run.
*/
inline constexpr std::array<int, 16> reverbPrimes {
    1123, 1187, 1289, 1373, 1493, 1613, 1741, 1861,
    1973, 2081, 2213, 2333, 2447, 2579, 2683, 2803
};
} // namespace nodo::dsp
