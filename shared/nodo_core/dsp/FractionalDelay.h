#pragma once

#include <cmath>
#include <vector>
#include <juce_core/juce_core.h>

namespace nodo::dsp
{
/** A delay line that can be read at a fractional distance.

    This is the piece that makes a delay sound like an instrument rather than a
    buffer. Reading at a whole number of samples is easy and useless: the moment
    the delay time moves — because a knob moved, or because an LFO is wobbling
    it — the read position lands between two samples, and how you decide what is
    there is most of the character of the effect.

    Four point Catmull-Rom rather than linear interpolation. Linear is a low pass
    whose corner depends on the fractional part, so a modulated delay built on it
    gets audibly duller and brighter as the wobble goes past .5 — the sound
    breathes in a way nobody asked for. Catmull-Rom costs a handful of multiplies
    and does not do that.

    Nothing here allocates outside prepare().
*/
class FractionalDelay
{
public:
    void prepare (int maximumDelaySamples)
    {
        // Three samples of headroom: the interpolator reaches one sample either
        // side of the pair it is between, and the write position needs somewhere
        // to be that is not inside the window being read.
        length = juce::jmax (8, maximumDelaySamples + 4);
        buffer.assign ((size_t) length, 0.0f);
        writePos = 0;
    }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    int getLength() const noexcept { return length; }

    inline void write (float value) noexcept
    {
        if (length <= 0)
            return;

        buffer[(size_t) writePos] = value;
    }

    /** Advances the write head. Call once per sample, after write() and after
        every read that belongs to this sample.
    */
    inline void advance() noexcept
    {
        if (++writePos >= length)
            writePos = 0;
    }

    /** Reads `delaySamples` behind the write head. The distance may be
        fractional and may change every sample.
    */
    inline float read (float delaySamples) const noexcept
    {
        if (length <= 0)
            return 0.0f;

        const auto clamped = juce::jlimit (1.0f, (float) (length - 3), delaySamples);
        const auto whole = (int) clamped;
        const auto frac = clamped - (float) whole;

        auto index = [this] (int offset) noexcept
        {
            auto i = writePos - offset;

            while (i < 0)
                i += length;

            while (i >= length)
                i -= length;

            return (size_t) i;
        };

        // y0 is one sample *closer* to the write head than the pair being
        // interpolated between, which is what gives Catmull-Rom its slope.
        const auto y0 = buffer[index (whole - 1)];
        const auto y1 = buffer[index (whole)];
        const auto y2 = buffer[index (whole + 1)];
        const auto y3 = buffer[index (whole + 2)];

        const auto c0 = y1;
        const auto c1 = 0.5f * (y2 - y0);
        const auto c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const auto c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }

private:
    std::vector<float> buffer;
    int length { 0 };
    int writePos { 0 };
};
} // namespace nodo::dsp
