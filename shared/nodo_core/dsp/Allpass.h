#pragma once

#include <cmath>
#include <vector>
#include <juce_core/juce_core.h>

namespace nodo::dsp
{
/** A Schroeder allpass: a delay line with a feedforward and a feedback path of
    equal and opposite gain.

    Flat in magnitude and a mess in phase, which is exactly what a reverb wants
    at its input. A delay on its own turns one hit into two hits; an allpass
    turns one hit into a burst that gets denser and never changes the tone. Put
    four of them in series with lengths that share no factors and a single
    impulse comes out the other end as a cloud.

    The length is fixed at prepare() but the read distance can be modulated,
    which is what stops the same four delays ringing on the same four
    frequencies for ever.
*/
class Allpass
{
public:
    void prepare (int maximumDelaySamples)
    {
        length = juce::jmax (8, maximumDelaySamples + 4);
        buffer.assign ((size_t) length, 0.0f);
        writePos = 0;
    }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    void setCoefficient (float newCoefficient) noexcept
    {
        // Past about 0.8 the ringing stops being diffusion and starts being a
        // resonance you can hum.
        coefficient = juce::jlimit (-0.85f, 0.85f, newCoefficient);
    }

    /** delaySamples may be fractional and may change every sample. */
    inline float process (float input, float delaySamples) noexcept
    {
        if (length <= 0)
            return input;

        const auto delayed = read (delaySamples);
        const auto v = input - coefficient * delayed;

        buffer[(size_t) writePos] = v;

        if (++writePos >= length)
            writePos = 0;

        return coefficient * v + delayed;
    }

private:
    inline float read (float delaySamples) const noexcept
    {
        const auto clamped = juce::jlimit (1.0f, (float) (length - 3), delaySamples);
        const auto whole = (int) clamped;
        const auto frac = clamped - (float) whole;

        auto index = [this] (int offset) noexcept
        {
            auto i = writePos - offset;

            while (i < 0)      i += length;
            while (i >= length) i -= length;

            return (size_t) i;
        };

        // Linear here rather than the four point interpolation the delay lines
        // use. An allpass inside a diffuser is smeared by design and by the
        // three that follow it; the difference is not audible and this runs
        // eight times per sample.
        const auto a = buffer[index (whole)];
        const auto b = buffer[index (whole + 1)];

        return a + (b - a) * frac;
    }

    std::vector<float> buffer;
    int length { 0 }, writePos { 0 };
    float coefficient { 0.6f };
};

/** A one-sample delay applied to a whole vector, which is what a feedback delay
    network's "unit delay" is when the matrix is applied before the lines.
    Kept here so the reverb's process loop reads as the block diagram does.
*/
template <int N>
struct DelayedVector
{
    std::array<float, N> value {};

    void reset() noexcept { value.fill (0.0f); }
};
} // namespace nodo::dsp
