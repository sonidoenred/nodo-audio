/*
    Verification for the gate and expander.

    Most of the time-domain work here is measured through the external
    sidechain, which turns out to be the only honest way to watch a gate's
    envelope: the thing being measured is the gain, and once the gate has closed
    there is no signal left at the output to measure it with. Feeding a constant
    tone to the input and the *changing* signal to the sidechain makes the output
    a direct readout of the gain, in dB, sample by sample.

    Square waves rather than sines wherever a level is being measured. A
    rectified square is constant, so the peak detector sits exactly on it; a
    rectified sine falls to zero twice per cycle and the detector ripples, which
    on a steep expansion slope would be read here as an error in the curve.
*/

#include "GateTests.h"

#include <nodo_core/nodo_core.h>
#include "../plugins/nodo_gate/source/GateEngine.h"
#include "../plugins/nodo_gate/source/FactoryPresets.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace nodo::tests
{
namespace
{
int checks = 0;
int failures = 0;

void check (bool condition, const juce::String& description, double actual, double expected)
{
    ++checks;

    if (condition)
    {
        std::printf ("  ok    %-56s  %8.3f\n", description.toRawUTF8(), actual);
    }
    else
    {
        ++failures;
        std::printf ("  FAIL  %-56s  %8.3f  (expected %.3f)\n",
                     description.toRawUTF8(), actual, expected);
    }
}

void checkClose (double actual, double expected, double tolerance, const juce::String& description)
{
    check (std::abs (actual - expected) <= tolerance, description, actual, expected);
}

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 256;

double toDb (double linear)
{
    return 20.0 * std::log10 (juce::jmax (1.0e-12, linear));
}

gate::GateSettings defaultSettings()
{
    gate::GateSettings s;
    s.thresholdDb = -30.0f;
    s.ratio = 200.0f;
    s.rangeDb = 60.0f;
    s.attackMs = 1.0f;
    s.holdMs = 0.0f;
    s.releaseMs = 100.0f;
    s.kneeDb = 0.0f;
    s.hysteresisDb = 0.0f;
    s.lookaheadMs = 0.0f;
    s.mix = 1.0f;
    s.style = gate::GateStyle::clean;
    s.direction = gate::Direction::gate;
    s.channelMode = gate::ChannelMode::linked;
    s.trigger = gate::Trigger::audio;
    s.sidechain = gate::SidechainSource::internal;
    return s;
}

using Signal = std::vector<std::vector<float>>;

/** A square wave. Its rectified envelope is constant, which is the whole point:
    the peak detector reads exactly the amplitude and nothing ripples.
*/
Signal makeSquare (double frequency, double amplitude, int samples, int channels = 2)
{
    Signal signal ((size_t) channels, std::vector<float> ((size_t) samples, 0.0f));
    const auto period = sampleRate / frequency;

    for (int i = 0; i < samples; ++i)
    {
        const auto phase = std::fmod ((double) i, period) / period;
        const auto value = (float) (phase < 0.5 ? amplitude : -amplitude);

        for (int ch = 0; ch < channels; ++ch)
            signal[(size_t) ch][(size_t) i] = value;
    }

    return signal;
}

/** A sine, for the one test where harmonics are the thing being avoided: a
    square wave is mostly edges, and a high passed square is *entirely* edges, so
    it would sail through a detector filter that a real low tone could not.
*/
Signal makeSine (double frequency, double amplitude, int samples)
{
    Signal signal (2, std::vector<float> ((size_t) samples, 0.0f));
    const auto omega = 2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;

    for (int i = 0; i < samples; ++i)
    {
        const auto value = (float) (amplitude * std::sin (omega * i));
        signal[0][(size_t) i] = value;
        signal[1][(size_t) i] = value;
    }

    return signal;
}

Signal runEngine (gate::GateEngine& engine, const Signal& input, const Signal* sidechain = nullptr)
{
    const auto channels = (int) input.size();
    const auto total = (int) input[0].size();

    Signal output ((size_t) channels, std::vector<float> ((size_t) total, 0.0f));

    juce::AudioBuffer<float> buffer (channels, blockSize);
    juce::AudioBuffer<float> scBuffer (channels, blockSize);

    for (int position = 0; position < total; position += blockSize)
    {
        const auto thisBlock = juce::jmin (blockSize, total - position);
        buffer.setSize (channels, thisBlock, false, false, true);
        scBuffer.setSize (channels, thisBlock, false, false, true);

        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < thisBlock; ++i)
            {
                buffer.setSample (ch, i, input[(size_t) ch][(size_t) (position + i)]);

                if (sidechain != nullptr)
                    scBuffer.setSample (ch, i, (*sidechain)[(size_t) ch][(size_t) (position + i)]);
            }

        juce::dsp::AudioBlock<float> block (buffer);

        if (sidechain != nullptr)
        {
            juce::dsp::AudioBlock<float> scBlock (scBuffer);
            const juce::dsp::AudioBlock<const float> scConst (scBlock);
            engine.process (block, &scConst);
        }
        else
        {
            engine.process (block, nullptr);
        }

        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < thisBlock; ++i)
                output[(size_t) ch][(size_t) (position + i)] = buffer.getSample (ch, i);
    }

    return output;
}

/** Peak of a window of samples, in dB. On a square wave this is exact. */
double peakDbBetween (const std::vector<float>& channel, int from, int to)
{
    auto peak = 0.0;

    for (auto i = (size_t) juce::jmax (0, from);
         i < (size_t) juce::jmin ((int) channel.size(), to); ++i)
        peak = juce::jmax (peak, (double) std::abs (channel[i]));

    return toDb (peak);
}

double settledPeakDb (const std::vector<float>& channel, double fraction = 0.2)
{
    const auto from = (int) ((double) channel.size() * (1.0 - fraction));
    return peakDbBetween (channel, from, (int) channel.size());
}

/** A constant carrier on the input with the decision signal on the sidechain,
    so the output is a direct readout of the gain in dB.
*/
struct GainProbe
{
    Signal input, sidechain;
    double carrierDb { -20.0 };
};

GainProbe makeProbe (int totalSamples, int switchSample, double loudDb, double quietDb)
{
    GainProbe probe;

    probe.input = makeSquare (1000.0, juce::Decibels::decibelsToGain (probe.carrierDb), totalSamples);
    probe.sidechain = makeSquare (1000.0, 1.0, totalSamples);

    const auto loud = (float) juce::Decibels::decibelsToGain (loudDb);
    const auto quiet = (float) juce::Decibels::decibelsToGain (quietDb);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < totalSamples; ++i)
            probe.sidechain[(size_t) ch][(size_t) i] *= i < switchSample ? loud : quiet;

    return probe;
}

void testStaticCurve()
{
    std::printf ("\nThe expansion curve\n");

    /*  The closed form first, before anything is run through the engine. If the
        curve is wrong then every measurement below is measuring the wrong thing
        accurately.
    */
    constexpr float threshold = -30.0f;
    constexpr float slope = 2.0f;      // 3:1 expansion
    constexpr float knee = 8.0f;
    constexpr float range = 60.0f;

    checkClose (gate::expansionGainDb (0.0f, threshold, slope, knee, range), 0.0, 1.0e-6,
                "well above the threshold, nothing at all");

    checkClose (gate::expansionGainDb (threshold + knee * 0.5f, threshold, slope, knee, range),
                0.0, 1.0e-6, "at the top of the knee, still nothing");

    // The number the whole suite pins: at the threshold itself a quadratic
    // tangent knee is exactly slope * knee / 8 down.
    checkClose (gate::expansionGainDb (threshold, threshold, slope, knee, range),
                -(double) slope * knee / 8.0, 1.0e-4,
                "at the threshold, exactly slope x knee / 8");

    // Below the knee it is a straight line of the stated slope.
    checkClose (gate::expansionGainDb (threshold - 20.0f, threshold, slope, knee, range),
                -40.0, 1.0e-4, "20 dB under, at 3:1, is 40 dB of expansion");

    // And the range stops it.
    checkClose (gate::expansionGainDb (threshold - 60.0f, threshold, slope, knee, range),
                -60.0, 1.0e-4, "range caps how far it may go");

    // Continuity where the knee meets the line, which is what stops a knee
    // sounding like a step.
    const auto justInside = gate::expansionGainDb (threshold - knee * 0.5f + 0.001f,
                                                   threshold, slope, knee, range);
    const auto justOutside = gate::expansionGainDb (threshold - knee * 0.5f - 0.001f,
                                                    threshold, slope, knee, range);
    checkClose (justInside - justOutside, 0.0, 0.02, "the knee joins the line without a step");

    // Monotonic: a quieter input can never come out louder.
    auto monotonic = true;
    auto previous = 0.0f;

    for (auto db = 0.0f; db > -100.0f; db -= 0.05f)
    {
        const auto value = db + gate::expansionGainDb (db, threshold, slope, knee, range);

        if (db < -0.01f && value > previous + 1.0e-4f)
            monotonic = false;

        previous = value;
    }

    check (monotonic, "the transfer curve never turns back on itself", 1.0, 1.0);

    // With no knee the curve is exactly two straight lines.
    checkClose (gate::expansionGainDb (threshold - 0.001f, threshold, slope, 0.0f, range),
                -0.002, 0.001, "with no knee it starts exactly at the threshold");
}

void testSteadyState()
{
    std::printf ("\nSteady state through the engine\n");

    /*  A square wave 20 dB under the threshold, with a 3:1 expander. The
        detector sits exactly on the amplitude, the envelope has nothing to
        smooth, and the answer should be the closed form to a hundredth.
    */
    auto settings = defaultSettings();
    settings.ratio = 3.0f;
    settings.rangeDb = 60.0f;
    settings.thresholdDb = -30.0f;

    const auto inputDb = -50.0;
    const auto amplitude = juce::Decibels::decibelsToGain (inputDb);

    gate::GateEngine engine;
    engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    engine.setSettings (settings);

    const auto output = runEngine (engine, makeSquare (1000.0, amplitude, 24000));

    checkClose (settledPeakDb (output[0]), inputDb - 40.0, 0.05,
                "20 dB under at 3:1 comes out 40 dB further down");

    // Above the threshold it is untouched, and untouched means bit for bit.
    gate::GateEngine open;
    open.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    open.setSettings (settings);

    const auto loud = makeSquare (1000.0, juce::Decibels::decibelsToGain (-6.0), 24000);
    const auto passed = runEngine (open, loud);

    auto worst = 0.0;

    for (size_t i = 2400; i < loud[0].size(); ++i)
        worst = juce::jmax (worst, std::abs ((double) passed[0][i] - (double) loud[0][i]));

    checkClose (worst, 0.0, 1.0e-7, "open, it is transparent sample for sample");

    // The range clamp, measured rather than computed.
    settings.ratio = 200.0f;
    settings.rangeDb = 18.0f;

    gate::GateEngine limited;
    limited.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    limited.setSettings (settings);

    checkClose (settledPeakDb (runEngine (limited, makeSquare (1000.0, amplitude, 24000))[0]),
                inputDb - 18.0, 0.05, "a gate with an 18 dB range takes off 18 dB");
}

void testHysteresis()
{
    std::printf ("\nHysteresis\n");

    /*  The reason a gate needs two thresholds. The sidechain sits 2 dB above the
        threshold, then drops to 2 dB below it — inside the hysteresis band. With
        hysteresis the gate stays open; without it, it shuts.
    */
    constexpr int total = 48000;
    constexpr int switchAt = 24000;

    auto settings = defaultSettings();
    settings.thresholdDb = -30.0f;
    settings.hysteresisDb = 6.0f;
    settings.attackMs = 1.0f;
    settings.releaseMs = 20.0f;
    settings.sidechain = gate::SidechainSource::external;

    const auto probe = makeProbe (total, switchAt, -28.0, -32.0);

    auto gainAfter = [&] (float hysteresis)
    {
        auto used = settings;
        used.hysteresisDb = hysteresis;

        gate::GateEngine engine;
        engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
        engine.setSettings (used);

        const auto output = runEngine (engine, probe.input, &probe.sidechain);

        // A long way after the drop, so the release has finished either way.
        return peakDbBetween (output[0], total - 4000, total) - probe.carrierDb;
    };

    checkClose (gainAfter (6.0f), 0.0, 0.05,
                "inside the hysteresis band the gate stays open");

    check (gainAfter (0.0f) < -30.0, "without it, the same signal shuts the gate",
           gainAfter (0.0f), -60.0);
}

void testHoldAndTimes()
{
    std::printf ("\nAttack, hold and release\n");

    constexpr int total = 48000;
    constexpr int switchAt = 24000;

    auto settings = defaultSettings();
    settings.thresholdDb = -30.0f;
    settings.attackMs = 1.0f;
    settings.releaseMs = 100.0f;
    settings.sidechain = gate::SidechainSource::external;
    settings.rangeDb = 60.0f;

    const auto probe = makeProbe (total, switchAt, -6.0, -90.0);

    auto gainAtMs = [&] (const std::vector<float>& output, double afterMs)
    {
        const auto index = switchAt + (int) (afterMs * 0.001 * sampleRate);
        return peakDbBetween (output, index, index + 48) - probe.carrierDb;
    };

    // --- hold --------------------------------------------------------------
    auto withHold = [&] (float holdMs)
    {
        auto used = settings;
        used.holdMs = holdMs;
        used.releaseMs = 20.0f;

        gate::GateEngine engine;
        engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
        engine.setSettings (used);

        return runEngine (engine, probe.input, &probe.sidechain);
    };

    const auto held = withHold (150.0f);
    const auto unheld = withHold (0.0f);

    checkClose (gainAtMs (held[0], 100.0), 0.0, 0.05,
                "with 150 ms of hold the gate has not moved after 100 ms");

    check (gainAtMs (unheld[0], 100.0) < -50.0,
           "without hold it has closed completely by then",
           gainAtMs (unheld[0], 100.0), -60.0);

    check (gainAtMs (held[0], 250.0) < -50.0, "and the held one closes once hold runs out",
           gainAtMs (held[0], 250.0), -60.0);

    // --- release time ------------------------------------------------------
    /*  Measured as a ratio between two points on the decay rather than from the
        moment the sidechain drops, and that is not a convenience — it is the
        only correct way to do it here.

        The detector has ballistics of its own: a peak follower with a 15 ms
        release does not report silence the instant the signal stops, it decays
        towards it, and from -6 dB it takes about 40 ms to cross a -30 dB
        threshold. The gain's release only starts then. Measuring from the drop
        would therefore have measured the sum of two different time constants and
        called the answer the release time.

        The decay is g(t) = -R (1 - exp(-(t - t0) / tau)), so
        (g + R) at two points a tau apart differ by exactly a factor of e,
        whatever t0 turns out to be.
    */
    gate::GateEngine releaseEngine;
    releaseEngine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    releaseEngine.setSettings (settings);

    const auto released = runEngine (releaseEngine, probe.input, &probe.sidechain);

    const auto early = gainAtMs (released[0], 100.0) + 60.0;
    const auto late  = gainAtMs (released[0], 200.0) + 60.0;

    checkClose (late / juce::jmax (1.0e-6, early), std::exp (-1.0), 0.05,
                "one release time apart, the remaining distance is 1/e");

    // --- attack time -------------------------------------------------------
    /*  The other direction, with the sidechain starting quiet and going loud, so
        the gate is opening rather than closing.
    */
    auto attackProbe = makeProbe (total, switchAt, -90.0, -6.0);

    auto attackSettings = settings;
    attackSettings.attackMs = 20.0f;

    gate::GateEngine attackEngine;
    attackEngine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    attackEngine.setSettings (attackSettings);

    const auto attacked = runEngine (attackEngine, attackProbe.input, &attackProbe.sidechain);

    const auto atAttack = peakDbBetween (attacked[0],
                                         switchAt + (int) (0.020 * sampleRate),
                                         switchAt + (int) (0.020 * sampleRate) + 48)
                        - attackProbe.carrierDb;

    checkClose (atAttack, -60.0 * (1.0 - 0.632), 2.5,
                "20 ms into a 20 ms attack, 63 % of the way open");
}

void testDirectionAndTrigger()
{
    std::printf ("\nDucking and the MIDI trigger\n");

    constexpr int total = 24000;

    /*  Ducking: the sidechain 12 dB over the threshold at 4:1 asks for 9 dB of
        reduction, which is the same closed form the compressor uses.
    */
    auto settings = defaultSettings();
    settings.direction = gate::Direction::ducking;
    settings.thresholdDb = -30.0f;
    settings.ratio = 4.0f;
    settings.rangeDb = 60.0f;
    settings.kneeDb = 0.0f;
    settings.hysteresisDb = 0.0f;
    settings.attackMs = 1.0f;
    settings.releaseMs = 50.0f;
    settings.sidechain = gate::SidechainSource::external;

    auto probe = makeProbe (total, total, -18.0, -18.0);   // loud throughout

    gate::GateEngine ducker;
    ducker.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    ducker.setSettings (settings);

    const auto ducked = runEngine (ducker, probe.input, &probe.sidechain);

    checkClose (settledPeakDb (ducked[0]) - probe.carrierDb, -9.0, 0.1,
                "ducking: 12 dB over at 4:1 pulls the signal down 9 dB");

    // And the range caps a duck exactly as it caps a gate.
    settings.rangeDb = 4.0f;
    gate::GateEngine capped;
    capped.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    capped.setSettings (settings);

    checkClose (settledPeakDb (runEngine (capped, probe.input, &probe.sidechain)[0])
                    - probe.carrierDb,
                -4.0, 0.1, "and the range caps the duck");

    // --- MIDI trigger ------------------------------------------------------
    /*  With the trigger on, the detector stops deciding: a signal well above the
        threshold stays shut until a note arrives.
    */
    auto triggered = defaultSettings();
    triggered.trigger = gate::Trigger::midi;
    triggered.thresholdDb = -60.0f;      // the signal is far above this
    triggered.rangeDb = 60.0f;
    triggered.attackMs = 0.5f;

    const auto loud = makeSquare (1000.0, juce::Decibels::decibelsToGain (-6.0), total);

    gate::GateEngine shut;
    shut.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    shut.setSettings (triggered);
    shut.setTriggerOpen (false);

    check (settledPeakDb (runEngine (shut, loud)[0]) < -60.0,
           "no note, no signal, however loud it is",
           settledPeakDb (runEngine (shut, loud)[0]), -66.0);

    gate::GateEngine opened;
    opened.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    opened.setSettings (triggered);
    opened.setTriggerOpen (true);

    checkClose (settledPeakDb (runEngine (opened, loud)[0]), -6.0, 0.05,
                "with a note held it passes untouched");
}

void testChannelModes()
{
    std::printf ("\nChannel modes\n");

    constexpr int total = 24000;

    auto settings = defaultSettings();
    settings.thresholdDb = -30.0f;
    settings.rangeDb = 60.0f;
    settings.attackMs = 1.0f;
    settings.releaseMs = 50.0f;

    // Loud on the left, far too quiet on the right.
    auto lopsided = makeSquare (1000.0, juce::Decibels::decibelsToGain (-6.0), total);

    for (auto& sample : lopsided[1])
        sample *= (float) juce::Decibels::decibelsToGain (-60.0);

    auto rightAfter = [&] (gate::ChannelMode mode)
    {
        auto used = settings;
        used.channelMode = mode;

        gate::GateEngine engine;
        engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
        engine.setSettings (used);

        return settledPeakDb (runEngine (engine, lopsided)[1]) + 66.0;
    };

    checkClose (rightAfter (gate::ChannelMode::linked), 0.0, 0.05,
                "linked, the loud side holds the quiet one open");

    check (rightAfter (gate::ChannelMode::dual) < -50.0,
           "unlinked, the quiet side gates on its own",
           rightAfter (gate::ChannelMode::dual), -60.0);

    /*  Mid/side. A loud centred signal with a quiet side: the side gate shuts,
        the mid gate stays open, and what comes out is the same on both channels
        — which is the measurable form of "the width has gone".
    */
    auto centred = makeSquare (1000.0, juce::Decibels::decibelsToGain (-6.0), total);

    for (int i = 0; i < total; ++i)
    {
        const auto side = 0.0004f * (i % 2 == 0 ? 1.0f : -1.0f);   // about -68 dB
        centred[0][(size_t) i] += side;
        centred[1][(size_t) i] -= side;
    }

    auto midSideSettings = settings;
    midSideSettings.channelMode = gate::ChannelMode::midSide;

    gate::GateEngine ms;
    ms.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    ms.setSettings (midSideSettings);

    const auto processed = runEngine (ms, centred);

    auto widest = 0.0;

    for (size_t i = 12000; i < (size_t) total; ++i)
        widest = juce::jmax (widest, std::abs ((double) processed[0][i] - (double) processed[1][i]));

    check (toDb (widest * 0.5) < -60.0, "mid/side: the quiet side is gated away on its own",
           toDb (widest * 0.5), -66.0);

    // And the middle survives it, which is the half people forget to check.
    checkClose (settledPeakDb (processed[0]), -6.0, 0.1, "while the middle passes untouched");
}

void testLookahead()
{
    std::printf ("\nLookahead\n");

    auto settings = defaultSettings();
    settings.lookaheadMs = 2.0f;
    settings.thresholdDb = -90.0f;
    settings.rangeDb = 0.0f;

    gate::GateEngine engine;
    engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    engine.setSettings (settings);

    const auto expected = (int) std::round (0.002 * sampleRate);
    checkClose (engine.computeLatencySamples(), expected, 0.5,
                "reported latency is the lookahead");

    // An impulse comes out exactly that many samples later.
    Signal impulse (2, std::vector<float> (4096, 0.0f));
    impulse[0][100] = 1.0f;
    impulse[1][100] = 1.0f;

    const auto output = runEngine (engine, impulse);

    auto peakIndex = 0;
    auto peak = 0.0f;

    for (size_t i = 0; i < output[0].size(); ++i)
        if (std::abs (output[0][i]) > peak)
        {
            peak = std::abs (output[0][i]);
            peakIndex = (int) i;
        }

    checkClose (peakIndex - 100, expected, 0.5, "and the audio really is delayed by it");
    checkClose (peak, 1.0, 1.0e-6, "with the impulse itself intact");
}

void testStyles()
{
    std::printf ("\nStyles\n");

    /*  Styles are multipliers, and the only interesting question about a
        multiplier is whether it points the right way. Percussive has to be
        faster than Smooth, and Bus slower than either — measured, not asserted
        from the table.
    */
    constexpr int total = 144000;      // three seconds: the bus style needs them
    constexpr int switchAt = 24000;

    auto settings = defaultSettings();
    settings.thresholdDb = -30.0f;
    settings.releaseMs = 100.0f;
    settings.holdMs = 0.0f;
    settings.rangeDb = 60.0f;
    settings.sidechain = gate::SidechainSource::external;

    const auto probe = makeProbe (total, switchAt, -6.0, -90.0);

    /*  How long each style takes to travel from 3 dB down to 12 dB down, rather
        than how long it takes to get there from the drop.

        Two reasons. The detector's own 15 ms release adds the same forty-odd
        milliseconds to every style before any of them start moving, and
        including that in the measurement squashes the differences the test is
        looking for. And at any fixed instant the two slow styles have not
        started yet and compare equal, which says nothing at all. The interval
        between two points on the slope is the release and nothing else.
    */
    auto slopeTimeMs = [&] (gate::GateStyle style)
    {
        auto used = settings;
        used.style = style;

        gate::GateEngine engine;
        engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
        engine.setSettings (used);

        const auto output = runEngine (engine, probe.input, &probe.sidechain);

        auto crossing = [&] (double belowDb)
        {
            for (int i = switchAt; i < total; ++i)
                if (toDb (std::abs (output[0][(size_t) i])) < probe.carrierDb - belowDb)
                    return i;

            return -1;
        };

        const auto from = crossing (3.0);
        const auto to = crossing (12.0);

        if (from < 0 || to < 0)
            return 1.0e9;

        return (double) (to - from) / sampleRate * 1000.0;
    };

    const auto percussive = slopeTimeMs (gate::GateStyle::percussive);
    const auto clean = slopeTimeMs (gate::GateStyle::clean);
    const auto smooth = slopeTimeMs (gate::GateStyle::smooth);
    const auto bus = slopeTimeMs (gate::GateStyle::bus);

    std::printf ("        3 dB to 12 dB down takes: percussive %.0f, clean %.0f, "
                 "smooth %.0f, bus %.0f ms\n", percussive, clean, smooth, bus);

    check (percussive < clean * 0.8, "percussive closes faster than clean",
           percussive, clean * 0.8);
    check (smooth > clean * 1.5, "smooth closes a good deal slower", smooth, clean * 1.5);
    check (bus > smooth, "and bus is the slowest of the lot", bus, smooth);

    // Every style has to be able to open all the way, or it is not a gate, it is
    // an attenuator.
    auto worstOpen = 0.0;

    for (int i = 0; i < gate::numStyles; ++i)
    {
        auto used = settings;
        used.style = (gate::GateStyle) i;
        used.sidechain = gate::SidechainSource::internal;

        gate::GateEngine engine;
        engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
        engine.setSettings (used);

        const auto loud = makeSquare (1000.0, juce::Decibels::decibelsToGain (-6.0), 48000);
        worstOpen = juce::jmax (worstOpen, std::abs (settledPeakDb (runEngine (engine, loud)[0]) + 6.0));
    }

    checkClose (worstOpen, 0.0, 0.05, "and all six open fully on a loud signal");
}

void testSidechainFilter()
{
    std::printf ("\nSidechain filtering\n");

    /*  The reason a gate needs filters on its detector: a snare mic with a kick
        bleeding into it. Filter the low end out of the decision and the kick
        stops opening the gate.
    */
    constexpr int total = 24000;

    auto settings = defaultSettings();
    settings.thresholdDb = -20.0f;
    settings.rangeDb = 60.0f;
    settings.attackMs = 1.0f;
    settings.releaseMs = 30.0f;

    /*  A 60 Hz *sine*, loud. A square would have been the wrong choice and it
        is worth saying why: a square is mostly edges, so what survives a high
        pass is a train of full-height spikes and the detector barely notices the
        filter. The thing being tested is what a filter does to a low tone, so
        the tone has to be one.
    */
    const auto low = makeSine (60.0, juce::Decibels::decibelsToGain (-6.0), total);

    gate::GateEngine unfiltered;
    unfiltered.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    unfiltered.setSettings (settings);

    checkClose (settledPeakDb (runEngine (unfiltered, low)[0]), -6.0, 0.3,
                "unfiltered, a loud low tone opens the gate");

    settings.scHighpassHz = 800.0f;

    gate::GateEngine filtered;
    filtered.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    filtered.setSettings (settings);

    const auto after = settledPeakDb (runEngine (filtered, low)[0]);

    check (after < -30.0, "with the detector high passed at 800 Hz it does not",
           after, -40.0);

    // And the filter is on the detector only: the audio never goes through it.
    auto listenSettings = defaultSettings();
    listenSettings.thresholdDb = -90.0f;   // wide open
    listenSettings.rangeDb = 0.0f;
    listenSettings.scHighpassHz = 800.0f;

    gate::GateEngine transparent;
    transparent.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    transparent.setSettings (listenSettings);

    checkClose (settledPeakDb (runEngine (transparent, low)[0]), -6.0, 0.05,
                "and the sound itself never passes through it");
}

void testMix()
{
    std::printf ("\nMix\n");

    auto settings = defaultSettings();
    settings.thresholdDb = -30.0f;
    settings.rangeDb = 60.0f;
    settings.mix = 0.5f;

    const auto inputDb = -50.0;
    const auto amplitude = juce::Decibels::decibelsToGain (inputDb);

    gate::GateEngine engine;
    engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    engine.setSettings (settings);

    const auto output = runEngine (engine, makeSquare (1000.0, amplitude, 24000));

    // Half of a fully closed gate is half the dry signal: -6 dB, no more.
    checkClose (settledPeakDb (output[0]), inputDb - 6.0206, 0.05,
                "50 % mix on a shut gate is exactly 6 dB down");
}

void testFactoryPresets()
{
    std::printf ("\nFactory presets\n");

    juce::AudioProcessorValueTreeState::ParameterLayout layout = gate::createParameterLayout();
    juce::AudioProcessor* dummy = nullptr;
    juce::ignoreUnused (dummy);

    // Build a parameter set once so every preset can be checked against the real
    // ranges rather than against a copy of them that could drift.
    struct Holder : juce::AudioProcessor
    {
        Holder() : juce::AudioProcessor (BusesProperties()
                                             .withInput ("In", juce::AudioChannelSet::stereo())
                                             .withOutput ("Out", juce::AudioChannelSet::stereo())) {}

        const juce::String getName() const override { return "holder"; }
        void prepareToPlay (double, int) override {}
        void releaseResources() override {}
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool hasEditor() const override { return false; }
        bool acceptsMidi() const override { return false; }
        bool producesMidi() const override { return false; }
        double getTailLengthSeconds() const override { return 0.0; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}
        void getStateInformation (juce::MemoryBlock&) override {}
        void setStateInformation (const void*, int) override {}
    };

    Holder holder;
    juce::AudioProcessorValueTreeState state (holder, nullptr, "NodoGate",
                                              gate::createParameterLayout());

    const auto presets = gate::buildFactoryPresets();

    check (presets.size() >= 8, "there are enough of them to be useful",
           (double) presets.size(), 8.0);

    auto unknownIds = 0;
    auto outOfRange = 0;
    auto missingText = 0;

    for (const auto& preset : presets)
    {
        if (preset.description.isEmpty() || preset.category.isEmpty())
            ++missingText;

        for (const auto& [id, value] : preset.assignments)
        {
            auto* parameter = state.getParameter (id);

            if (parameter == nullptr)
            {
                ++unknownIds;
                std::printf ("        unknown id: %s\n", id.toRawUTF8());
                continue;
            }

            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            {
                const auto normalised = ranged->convertTo0to1 (value);

                if (normalised < -1.0e-4f || normalised > 1.0f + 1.0e-4f)
                {
                    ++outOfRange;
                    std::printf ("        out of range: %s = %.3f\n", id.toRawUTF8(), value);
                }
            }
        }
    }

    check (unknownIds == 0, "every identifier they write exists", (double) unknownIds, 0.0);
    check (outOfRange == 0, "every value fits its parameter's range", (double) outOfRange, 0.0);
    check (missingText == 0, "and every one explains itself", (double) missingText, 0.0);

    /*  Mojibake canary. juce::String reads a plain const char* as ASCII, so a
        preset written in Spanish arrives on screen as "Sala pequeÃ±a" — the
        text is still a valid string, so nothing fails, and nobody finds out
        until they open the menu and look. These two characters cannot appear
        in correct Spanish, which makes them a cheap alarm.
    */
    auto mangled = 0;

    for (const auto& preset : presets)
        if ((preset.name + preset.category + preset.description)
                .containsAnyOf (juce::String::fromUTF8 ("\u00c3\u00c2")))
            ++mangled;

    check (mangled == 0, "and the accents survived the trip to the screen",
           (double) mangled, 0.0);

}
} // namespace

Result runGateTests()
{
    std::printf ("\n\nNodo Gate verification\n======================\n");

    testStaticCurve();
    testSteadyState();
    testHysteresis();
    testHoldAndTimes();
    testDirectionAndTrigger();
    testChannelModes();
    testLookahead();
    testStyles();
    testSidechainFilter();
    testMix();
    testFactoryPresets();

    return { checks, failures };
}
} // namespace nodo::tests
