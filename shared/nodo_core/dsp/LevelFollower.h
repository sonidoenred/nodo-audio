#pragma once

#include <atomic>
#include <cmath>
#include <juce_core/juce_core.h>

namespace nodo::dsp
{
/** Peak follower with an instant attack and an exponential release, written to
    from the audio thread and read from the GUI thread without locking.

    It also carries a running RMS, because the two answer different questions:
    the peak tells you whether you are about to clip and the RMS tells you how
    loud the thing actually is. A mix sitting at -2 dB peak and -18 dB RMS and
    one at -2 dB peak and -8 dB RMS are not the same mix, and no peak meter will
    ever say so.

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

        /*  La ventana del RMS no sigue al release del pico a proposito. El pico
            tiene que caer despacio para que se vea el transitorio que ya ha
            pasado; el RMS tiene que integrar una ventana fija para que el
            numero signifique lo mismo en una voz que en un bombo. 300 ms es lo
            que usa medio mundo para un medidor de programa.
        */
        rmsCoeff = std::exp (-1.0f / (0.3f * (float) juce::jmax (1.0, sampleRate)));

        current = 0.0f;
        meanSquare = 0.0f;
        level.store (0.0f, std::memory_order_relaxed);
        rms.store (0.0f, std::memory_order_relaxed);
    }

    void reset() noexcept
    {
        current = 0.0f;
        meanSquare = 0.0f;
        level.store (0.0f, std::memory_order_relaxed);
        rms.store (0.0f, std::memory_order_relaxed);
    }

    /** Audio thread. */
    void push (const float* samples, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const auto sample = samples[i];
            const auto mag = std::abs (sample);

            current = mag > current ? mag : current * releaseCoeff;
            meanSquare = meanSquare * rmsCoeff + sample * sample * (1.0f - rmsCoeff);
        }

        if (! std::isfinite (current))
            current = 0.0f;

        if (! std::isfinite (meanSquare) || meanSquare < 0.0f)
            meanSquare = 0.0f;

        level.store (current, std::memory_order_relaxed);
        rms.store (std::sqrt (meanSquare), std::memory_order_relaxed);
    }

    /** GUI thread. Linear magnitude, 1.0 == 0 dBFS. */
    float getLevel() const noexcept { return level.load (std::memory_order_relaxed); }

    /** GUI thread. Linear RMS over roughly the last 300 ms. */
    float getRms() const noexcept { return rms.load (std::memory_order_relaxed); }

private:
    float current { 0.0f };
    float meanSquare { 0.0f };
    float releaseCoeff { 0.999f };
    float rmsCoeff { 0.999f };
    std::atomic<float> level { 0.0f };
    std::atomic<float> rms { 0.0f };
};
} // namespace nodo::dsp
