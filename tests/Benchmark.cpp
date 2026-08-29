/*
    Throughput measurement for the engines whose cost is worth knowing: the EQ,
    the compressor and the reverb.

    The number that matters is not milliseconds, it is what share of one CPU core
    the plugin needs to keep up with real time. A figure of 1.0% means a single
    core could run a hundred instances, which is the question a mixer actually
    has: how many of these can I put on a session before the DAW complains.

    Absolute figures depend on the machine, so the interesting comparisons are
    the ones inside this table: static versus dynamic, idle versus a parameter
    being swept, and how the cost scales with band count.
*/

#include <nodo_core/nodo_core.h>
#include "EqEngine.h"
#include "../plugins/nodo_comp/source/CompEngine.h"
#include "../plugins/nodo_verb/source/VerbEngine.h"

#include <chrono>
#include <cstdio>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 256;
constexpr double secondsOfAudio = 20.0;

struct Case
{
    const char* name;
    int numBands;
    bool dynamic;
    bool sweeping;
    nodo::eq::FilterMode mode { nodo::eq::FilterMode::analogMatched };
};

std::array<nodo::eq::BandSettings, nodo::eq::numBands> makeBands (int count, bool dynamic)
{
    std::array<nodo::eq::BandSettings, nodo::eq::numBands> bands {};

    for (int i = 0; i < count && i < nodo::eq::numBands; ++i)
    {
        auto& b = bands[(size_t) i];
        b.enabled = true;
        b.type = nodo::eq::FilterType::bell;
        b.frequency = (float) (60.0 * std::pow (1.35, i));
        b.gainDb = (i % 2 == 0) ? 4.0f : -4.0f;
        b.q = 1.2f;
        b.dynamic = dynamic;
        b.thresholdDb = -30.0f;
        b.ratio = 4.0f;
        b.attackMs = 8.0f;
        b.releaseMs = 120.0f;
    }

    return bands;
}

double runCase (const Case& testCase)
{
    nodo::eq::EqEngine engine;
    engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    engine.setFilterMode (testCase.mode);

    auto bands = makeBands (testCase.numBands, testCase.dynamic);
    engine.setTargets (bands);
    engine.snapToTargets();

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::Random random (7);

    const auto totalBlocks = (int) (secondsOfAudio * sampleRate / blockSize);
    const auto start = std::chrono::steady_clock::now();

    // The same flush-to-zero state every processBlock runs in. Without it the
    // measurement includes denormal arithmetic the plugin never actually pays.
    juce::ScopedNoDenormals noDenormals;

    for (int block = 0; block < totalBlocks; ++block)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);

            for (int i = 0; i < blockSize; ++i)
                data[i] = random.nextFloat() * 0.5f - 0.25f;
        }

        if (testCase.sweeping)
        {
            // Move every band's frequency continuously, which forces a
            // coefficient rebuild on every 32-sample chunk. This is the worst
            // case: a user dragging a node, or heavy automation.
            const auto phase = (float) block / (float) totalBlocks;

            for (int i = 0; i < testCase.numBands && i < nodo::eq::numBands; ++i)
                bands[(size_t) i].frequency = (float) (60.0 * std::pow (1.35, i))
                                            * (1.0f + 0.5f * std::sin (phase * 40.0f));

            engine.setTargets (bands);
        }

        juce::dsp::AudioBlock<float> audioBlock (buffer);
        engine.process (audioBlock);
    }

    const auto elapsed = std::chrono::duration<double> (
        std::chrono::steady_clock::now() - start).count();

    return 100.0 * elapsed / secondsOfAudio;
}
}

int main()
{
    const Case cases[] = {
        { "4 bands, static",              4,  false, false },
        { "8 bands, static",              8,  false, false },
        { "24 bands, static",             24, false, false },
        { "8 bands, static, sweeping",    8,  false, true  },
        { "24 bands, static, sweeping",   24, false, true  },
        { "4 bands, dynamic",             4,  true,  false },
        { "8 bands, dynamic",             8,  true,  false },
        { "24 bands, dynamic",            24, true,  false },
        { "24 bands, dynamic, sweeping",  24, true,  true  },
        { "  same, Digital filters",      24, true,  true, nodo::eq::FilterMode::digital },
        { "24 bands, sweeping, Analog",   24, false, true, nodo::eq::FilterMode::analogMatched },
        { "  same, Digital filters",      24, false, true, nodo::eq::FilterMode::digital },
    };

    std::printf ("Nodo EQ throughput  (%.0f kHz, stereo, %d-sample blocks)\n",
                 sampleRate / 1000.0, blockSize);
    std::printf ("==========================================================\n");
    std::printf ("%-32s %10s %14s\n", "configuration", "% of core", "instances/core");

    for (const auto& testCase : cases)
    {
        const auto percent = runCase (testCase);
        std::printf ("%-32s %9.2f%% %14.0f\n", testCase.name, percent,
                     percent > 0.0 ? 100.0 / percent : 0.0);
    }

    // ---- Compressor --------------------------------------------------------
    struct CompCase
    {
        const char* name;
        nodo::comp::CompStyle style;
        nodo::comp::Detection detection;
        float link;
        float lookaheadMs;
    };

    const CompCase compCases[] = {
        { "Clean, peak, linked",        nodo::comp::CompStyle::clean, nodo::comp::Detection::peak, 1.0f, 0.0f },
        { "Clean, peak, unlinked",      nodo::comp::CompStyle::clean, nodo::comp::Detection::peak, 0.0f, 0.0f },
        { "Clean, RMS",                 nodo::comp::CompStyle::clean, nodo::comp::Detection::rms,  1.0f, 0.0f },
        { "Bus, RMS, dual stage",       nodo::comp::CompStyle::bus,   nodo::comp::Detection::rms,  1.0f, 0.0f },
        { "Clean with 5 ms lookahead",  nodo::comp::CompStyle::clean, nodo::comp::Detection::peak, 1.0f, 5.0f }
    };

    std::printf ("\n\nNodo Comp throughput  (%.0f kHz, stereo, %d-sample blocks)\n",
                 sampleRate / 1000.0, blockSize);
    std::printf ("==========================================================\n");
    std::printf ("%-32s %10s %14s\n", "configuration", "% of core", "instances/core");

    for (const auto& testCase : compCases)
    {
        nodo::comp::CompEngine engine;
        engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });

        nodo::comp::CompSettings settings;
        settings.thresholdDb = -30.0f;
        settings.ratio = 4.0f;
        settings.kneeDb = 6.0f;
        settings.attackMs = 8.0f;
        settings.releaseMs = 120.0f;
        settings.style = testCase.style;
        settings.detection = testCase.detection;
        settings.stereoLink = testCase.link;
        settings.lookaheadMs = testCase.lookaheadMs;
        engine.setSettings (settings);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::Random random (7);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < blockSize; ++i)
                buffer.setSample (ch, i, random.nextFloat() * 0.6f - 0.3f);

        const auto blocks = (int) (secondsOfAudio * sampleRate / blockSize);
        const auto start = std::chrono::steady_clock::now();

        juce::ScopedNoDenormals noDenormals;

        for (int i = 0; i < blocks; ++i)
        {
            juce::dsp::AudioBlock<float> block (buffer);
            engine.process (block, nullptr);
        }

        const auto elapsed = std::chrono::duration<double> (
            std::chrono::steady_clock::now() - start).count();
        const auto percent = 100.0 * elapsed / secondsOfAudio;

        std::printf ("%-32s %9.2f%% %14.0f\n", testCase.name, percent,
                     percent > 0.0 ? 100.0 / percent : 0.0);
    }

    // ---- Reverb ------------------------------------------------------------
    /*  The most expensive engine of the suite and the one whose cost is least
        obvious from the outside: eight delay lines with two shelves each run
        whatever the settings say, so the interesting question is not "how much
        does a long reverb cost" — the answer is the same as a short one — but
        how much the parts that can be switched off are worth.
    */
    struct VerbCase
    {
        const char* name;
        float decaySeconds;
        float motion;
        float duckAmount;
        float postGainDb;
        bool freeze;
    };

    const VerbCase verbCases[] = {
        { "2 s, still",                 2.0f,  0.0f,  0.0f, 0.0f, false },
        { "2 s, moving",                2.0f,  0.25f, 0.0f, 0.0f, false },
        { "10 s, moving",              10.0f,  0.25f, 0.0f, 0.0f, false },
        { "10 s, moving, post EQ",     10.0f,  0.25f, 0.0f, 4.0f, false },
        { "  same, plus ducking",      10.0f,  0.25f, 0.6f, 4.0f, false },
        { "frozen",                    10.0f,  0.25f, 0.0f, 0.0f, true  }
    };

    std::printf ("\n\nNodo Verb throughput  (%.0f kHz, stereo, %d-sample blocks)\n",
                 sampleRate / 1000.0, blockSize);
    std::printf ("==========================================================\n");
    std::printf ("%-32s %10s %14s\n", "configuration", "% of core", "instances/core");

    for (const auto& testCase : verbCases)
    {
        nodo::verb::VerbEngine engine;
        engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });

        nodo::verb::VerbSettings settings;
        settings.mix = 0.3f;
        settings.decay.midSeconds = testCase.decaySeconds;
        settings.motion = testCase.motion;
        settings.duckAmount = testCase.duckAmount;
        settings.postLowGain = testCase.postGainDb;
        settings.postHighGain = -testCase.postGainDb;
        settings.freeze = testCase.freeze;
        engine.setSettings (settings);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::Random random (11);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < blockSize; ++i)
                buffer.setSample (ch, i, random.nextFloat() * 0.6f - 0.3f);

        const auto blocks = (int) (secondsOfAudio * sampleRate / blockSize);
        const auto start = std::chrono::steady_clock::now();

        /*  Same flush-to-zero state the host gives processBlock. Without it a
            frozen tail measures three times its real cost: nothing new goes
            into the network, the diffusers keep decaying towards zero, and the
            denormal arithmetic that produces is the slowest thing on the chip.
            Measuring that would be measuring a state the plugin never runs in.
        */
        juce::ScopedNoDenormals noDenormals;

        for (int i = 0; i < blocks; ++i)
        {
            juce::dsp::AudioBlock<float> block (buffer);
            engine.process (block);
        }

        const auto elapsed = std::chrono::duration<double> (
            std::chrono::steady_clock::now() - start).count();
        const auto percent = 100.0 * elapsed / secondsOfAudio;

        std::printf ("%-32s %9.2f%% %14.0f\n", testCase.name, percent,
                     percent > 0.0 ? 100.0 / percent : 0.0);
    }

    std::printf ("\nMeasured on whatever machine ran this; the useful reading is the\n"
                 "ratios between rows, not the absolute numbers.\n");
    return 0;
}
