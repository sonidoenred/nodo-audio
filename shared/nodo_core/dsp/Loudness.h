#pragma once

#include <array>
#include <cmath>
#include <juce_core/juce_core.h>

#include "Biquad.h"

namespace nodo::dsp
{
/** Loudness measurement to ITU-R BS.1770-4 / EBU R128.

    Three numbers, all in LUFS:

    - Momentary: mean over the last 400 ms.
    - Short term: mean over the last 3 s.
    - Integrated: mean over everything since the meter was reset, with the two
      gates the standard requires — an absolute one at -70 LUFS and a relative
      one 10 LU below the ungated mean. The gates are the whole point: without
      them a track's integrated loudness would be dragged down by its own
      silence, and a quiet intro would count as much as the chorus.

    The K-weighting is a high shelf and a high pass, both defined in the standard
    by their analog parameters rather than by fixed coefficients, so they are
    re-derived here at whatever rate the host is running instead of being
    hard-coded for 48 kHz.

    Nothing here allocates or grows: the integrated figure is accumulated into a
    histogram of 0.1 LU bins rather than a list of blocks, which is both what the
    reference implementation does and the only way to keep an hour-long session
    from turning the meter into a memory leak on the audio thread.
*/
class LoudnessMeter
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;

        // BS.1770-4: the two stages of the K-weighting, as analog prototypes.
        const auto shelf = BiquadCoefficients::highShelf (sampleRate,
                                                          1681.974450955533,
                                                          0.7071752369554196,
                                                          3.999843853973347);
        const auto highPass = BiquadCoefficients::highPass (sampleRate,
                                                            38.13547087602444,
                                                            0.5003270373238773);

        for (int ch = 0; ch < 2; ++ch)
        {
            shelfFilter[(size_t) ch].setCoefficients (shelf);
            highPassFilter[(size_t) ch].setCoefficients (highPass);
        }

        // The standard measures 400 ms blocks overlapping by 75 %, so a new
        // block boundary comes round every 100 ms.
        stepSamples = juce::jmax (1, (int) std::round (0.100 * sampleRate));

        reset();
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            shelfFilter[(size_t) ch].reset();
            highPassFilter[(size_t) ch].reset();
        }

        squareSum = 0.0;
        samplesInStep = 0;
        stepsSeen = 0;
        stepWrite = 0;
        recentPowers.fill (0.0);

        binPower.fill (0.0);
        binCount.fill (0);

        momentary.store (silentLufs, std::memory_order_relaxed);
        shortTerm.store (silentLufs, std::memory_order_relaxed);
        integrated.store (silentLufs, std::memory_order_relaxed);
    }

    /** Audio thread. */
    void push (const float* const* data, int numChannels, int numSamples) noexcept
    {
        const auto used = juce::jlimit (1, 2, numChannels);

        for (int i = 0; i < numSamples; ++i)
        {
            double sum = 0.0;

            for (int ch = 0; ch < used; ++ch)
            {
                auto sample = shelfFilter[(size_t) ch].processSample (data[ch][i]);
                sample = highPassFilter[(size_t) ch].processSample (sample);

                // Both channels of a stereo programme weigh the same; the
                // weightings above 1.0 in the standard are for surround channels
                // we do not have.
                sum += (double) sample * (double) sample;
            }

            squareSum += sum;

            if (++samplesInStep >= stepSamples)
                pushStep();
        }

        for (int ch = 0; ch < 2; ++ch)
        {
            shelfFilter[(size_t) ch].snapToZero();
            highPassFilter[(size_t) ch].snapToZero();
        }
    }

    float getMomentaryLufs()  const noexcept { return momentary.load (std::memory_order_relaxed); }
    float getShortTermLufs()  const noexcept { return shortTerm.load (std::memory_order_relaxed); }
    float getIntegratedLufs() const noexcept { return integrated.load (std::memory_order_relaxed); }

    /** The constant in front of every loudness figure in the standard. */
    static constexpr double offsetDb = -0.691;
    static constexpr float silentLufs = -200.0f;

    static float powerToLufs (double meanSquare) noexcept
    {
        return meanSquare > 0.0 ? (float) (offsetDb + 10.0 * std::log10 (meanSquare))
                                : silentLufs;
    }

private:
    static constexpr int numBins = 751;          // -70.0 to +5.0 LUFS in 0.1 LU
    static constexpr double absoluteGate = -70.0;

    static double lufsForBin (int index) noexcept { return absoluteGate + 0.1 * index; }

    void pushStep() noexcept
    {
        recentPowers[(size_t) stepWrite] = squareSum / (double) stepSamples;
        stepWrite = (stepWrite + 1) % (int) recentPowers.size();
        ++stepsSeen;

        squareSum = 0.0;
        samplesInStep = 0;

        const auto meanOverLastSteps = [this] (int steps) -> double
        {
            if (stepsSeen < steps)
                return -1.0;

            double sum = 0.0;

            for (int i = 1; i <= steps; ++i)
            {
                const auto index = (stepWrite - i + (int) recentPowers.size())
                                 % (int) recentPowers.size();
                sum += recentPowers[(size_t) index];
            }

            return sum / (double) steps;
        };

        const auto momentaryPower = meanOverLastSteps (4);    // 400 ms
        const auto shortTermPower = meanOverLastSteps (30);   // 3 s

        if (momentaryPower >= 0.0)
        {
            const auto lufs = powerToLufs (momentaryPower);
            momentary.store (lufs, std::memory_order_relaxed);

            if (lufs > absoluteGate)
            {
                const auto bin = juce::jlimit (0, numBins - 1,
                                               (int) std::lround ((lufs - absoluteGate) * 10.0));
                binPower[(size_t) bin] += momentaryPower;
                ++binCount[(size_t) bin];

                updateIntegrated();
            }
        }

        if (shortTermPower >= 0.0)
            shortTerm.store (powerToLufs (shortTermPower), std::memory_order_relaxed);
    }

    void updateIntegrated() noexcept
    {
        double sum = 0.0;
        juce::int64 count = 0;

        for (int bin = 0; bin < numBins; ++bin)
        {
            sum += binPower[(size_t) bin];
            count += binCount[(size_t) bin];
        }

        if (count == 0)
        {
            integrated.store (silentLufs, std::memory_order_relaxed);
            return;
        }

        // Relative gate: 10 LU below the mean of everything above the absolute
        // gate. Applied once, not iterated, exactly as the standard says.
        const auto relativeGate = powerToLufs (sum / (double) count) - 10.0f;

        double gatedSum = 0.0;
        juce::int64 gatedCount = 0;

        for (int bin = 0; bin < numBins; ++bin)
        {
            if (binCount[(size_t) bin] == 0 || lufsForBin (bin) <= relativeGate)
                continue;

            gatedSum += binPower[(size_t) bin];
            gatedCount += binCount[(size_t) bin];
        }

        integrated.store (gatedCount > 0 ? powerToLufs (gatedSum / (double) gatedCount)
                                         : silentLufs,
                          std::memory_order_relaxed);
    }

    double sampleRate { 48000.0 };
    int stepSamples { 4800 };

    std::array<Biquad, 2> shelfFilter, highPassFilter;

    double squareSum { 0.0 };
    int samplesInStep { 0 };
    juce::int64 stepsSeen { 0 };
    int stepWrite { 0 };
    std::array<double, 30> recentPowers {};      // 100 ms steps covering 3 s

    std::array<double, numBins> binPower {};
    std::array<juce::int64, numBins> binCount {};

    std::atomic<float> momentary { silentLufs }, shortTerm { silentLufs }, integrated { silentLufs };
};
} // namespace nodo::dsp
