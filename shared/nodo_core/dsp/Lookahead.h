#pragma once

#include <cmath>
#include <vector>
#include <juce_core/juce_core.h>

namespace nodo::dsp
{
/** Running minimum over the last N samples, in constant time per sample.

    This is the piece that makes a limiter a limiter rather than a fast
    compressor. A compressor reacts after the peak has arrived and lets the
    first few samples through; a limiter delays the audio, looks at the gain the
    peak is going to demand, and starts moving *before* it gets there. Holding
    the minimum over the lookahead window is what turns "the gain I need right
    now" into "the gain I will need soon".

    Implemented as a monotonic deque: each sample is pushed once and popped at
    most once, so the cost per sample is constant no matter how long the window
    is. The naive version — scanning the window every sample — is O(N) and turns
    a 20 ms lookahead at 96 kHz into two thousand comparisons per sample.
*/
class SlidingMinimum
{
public:
    void prepare (int maximumWindow)
    {
        capacity = juce::jmax (2, maximumWindow + 2);
        values.assign ((size_t) capacity, 0.0f);
        positions.assign ((size_t) capacity, 0);
        reset();
    }

    void setWindow (int newWindow) noexcept
    {
        if (capacity < 3)
            return;

        window = juce::jlimit (1, capacity - 2, newWindow);
    }

    void reset() noexcept
    {
        head = tail = 0;
        counter = 0;
        std::fill (values.begin(), values.end(), 0.0f);
        std::fill (positions.begin(), positions.end(), 0);
    }

    /** Returns the minimum of the last `window` inputs, this one included. */
    inline float process (float input) noexcept
    {
        if (capacity < 3)
            return input;

        // Drop everything at the back that this sample makes irrelevant: those
        // values can never be the minimum again while they are in the window.
        while (head != tail)
        {
            const auto back = (tail + capacity - 1) % capacity;

            if (values[(size_t) back] < input)
                break;

            tail = back;
        }

        values[(size_t) tail] = input;
        positions[(size_t) tail] = counter;
        tail = (tail + 1) % capacity;

        // Drop the front if it has fallen out of the window.
        while (positions[(size_t) head] <= counter - window)
            head = (head + 1) % capacity;

        ++counter;

        return values[(size_t) head];
    }

private:
    std::vector<float> values;
    std::vector<juce::int64> positions;
    int capacity { 0 };
    int window { 1 };
    int head { 0 }, tail { 0 };
    juce::int64 counter { 0 };
};

/** Boxcar average over a fixed number of samples.

    Two of these in series turn the staircase that comes out of the sliding
    minimum into a smooth ramp. One gives a straight line, two give a curve with
    no corner at either end, which is what stops a limiter from clicking on the
    way into gain reduction.
*/
class MovingAverage
{
public:
    void prepare (int maximumLength)
    {
        capacity = juce::jmax (1, maximumLength + 1);
        buffer.assign ((size_t) capacity, 0.0f);
        reset();
    }

    void setLength (int newLength) noexcept
    {
        if (capacity < 2)
            return;

        const auto wanted = juce::jlimit (1, capacity - 1, newLength);

        if (wanted != length)
        {
            length = wanted;
            reset();
        }
    }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        sum = 0.0;
        writePos = 0;
        primed = false;
    }

    /** Fills the window with `value` so the first block does not ramp up from
        silence. A limiter that starts at unity for the length of its smoothing
        would let the first transient of every take straight through.
    */
    /*  Callable before prepare(): a host is allowed to construct a plugin and
        tear it down again without ever preparing it, and reset() runs on that
        path. Writing into an unallocated window is how that becomes a crash on
        someone else's machine rather than a no-op on ours.
    */
    void primeWith (float value) noexcept
    {
        if ((int) buffer.size() < length)
            return;

        std::fill (buffer.begin(), buffer.begin() + length, value);
        sum = (double) value * length;
        writePos = 0;
        primed = true;
    }

    inline float process (float input) noexcept
    {
        if (capacity < 2)
            return input;

        if (! primed)
            primeWith (input);

        sum -= buffer[(size_t) writePos];
        buffer[(size_t) writePos] = input;
        sum += input;

        writePos = (writePos + 1) % length;

        return (float) (sum / (double) length);
    }

    int getLength() const noexcept { return length; }

private:
    std::vector<float> buffer;
    double sum { 0.0 };
    int capacity { 0 }, length { 1 }, writePos { 0 };
    bool primed { false };
};
} // namespace nodo::dsp
