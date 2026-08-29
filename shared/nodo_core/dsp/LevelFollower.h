#pragma once

#include <atomic>
#include <cmath>
#include <juce_core/juce_core.h>

namespace nodo::dsp
{
/** Peak follower with an instant attack and an exponential release, written to
    from the audio thread and read from the GUI thread without locking.

    The value is stored as a plain float in an atomic: the GUI only ever needs a
    recent value, never a precise history, so a relaxed load is enough.
*/
class LevelFollower
{
public:
    void prepare (double sampleRate, float releaseSeconds = 0.3f) noexcept
    {
        // Time constant for a one-pole release: coeff = exp(-1 / (t * fs))
        releaseCoeff = std::exp (-1.0f / (juce::jmax (1.0e-4f, releaseSeconds)
                                          * (float) juce::jmax (1.0, sampleRate)));
        current = 0.0f;
        level.store (0.0f, std::memory_order_relaxed);
    }

    void reset() noexcept
    {
        current = 0.0f;
        level.store (0.0f, std::memory_order_relaxed);
    }

    /** Audio thread. */
    void push (const float* samples, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const auto mag = std::abs (samples[i]);
            current = mag > current ? mag : current * releaseCoeff;
        }

        if (! std::isfinite (current))
            current = 0.0f;

        level.store (current, std::memory_order_relaxed);
    }

    /** GUI thread. Linear magnitude, 1.0 == 0 dBFS. */
    float getLevel() const noexcept { return level.load (std::memory_order_relaxed); }

private:
    float current { 0.0f };
    float releaseCoeff { 0.999f };
    std::atomic<float> level { 0.0f };
};
} // namespace nodo::dsp
