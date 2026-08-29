/*
    Verification for the limiter.

    A limiter has one promise: nothing leaves it above the ceiling. That promise
    is easy to keep badly — clip the output and it is technically true — so most
    of what is checked here is that the promise is kept by the *gain path*, with
    the safety clipper never doing any work. getSafetyClipCount() exists for
    exactly this reason: every ceiling test below also asserts that the clipper
    stayed at zero, which is the difference between a limiter and a clipper with
    good manners.
*/

#include "LimitTests.h"

#include <nodo_core/nodo_core.h>
#include "../plugins/nodo_limit/source/LimitEngine.h"

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

limit::LimitSettings defaultSettings()
{
    limit::LimitSettings s;
    s.inputGainDb = 0.0f;
    s.ceilingDb = -1.0f;
    s.lookaheadMs = 5.0f;
    s.releaseMs = 200.0f;
    s.style = limit::LimitStyle::transparent;
    s.truePeak = false;
    s.transientLink = 1.0f;
    s.releaseLink = 1.0f;
    s.dcFilter = false;
    s.externalTrigger = false;
    s.delta = false;
    s.dither = limit::DitherMode::off;
    return s;
}

/** Runs a signal through the limiter and hands back the output, so a test can
    ask whatever it likes of it.
*/
std::vector<std::vector<float>> runEngine (limit::LimitEngine& engine,
                                           const std::vector<std::vector<float>>& input)
{
    const auto channels = (int) input.size();
    const auto total = (int) input[0].size();

    std::vector<std::vector<float>> output ((size_t) channels,
                                            std::vector<float> ((size_t) total, 0.0f));

    juce::AudioBuffer<float> buffer (channels, blockSize);

    for (int position = 0; position < total; position += blockSize)
    {
        const auto thisBlock = juce::jmin (blockSize, total - position);
        buffer.setSize (channels, thisBlock, false, false, true);

        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < thisBlock; ++i)
                buffer.setSample (ch, i, input[(size_t) ch][(size_t) (position + i)]);

        juce::dsp::AudioBlock<float> block (buffer);
        engine.process (block, nullptr);

        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < thisBlock; ++i)
                output[(size_t) ch][(size_t) (position + i)] = buffer.getSample (ch, i);
    }

    return output;
}

double peakOf (const std::vector<std::vector<float>>& signal)
{
    auto peak = 0.0;

    for (const auto& channel : signal)
        for (auto sample : channel)
            peak = juce::jmax (peak, (double) std::abs (sample));

    return peak;
}

/** Four times oversampled peak of a signal, which is what a converter will
    reconstruct and what "true peak" means.
*/
double truePeakOf (const std::vector<float>& channel)
{
    juce::dsp::Oversampling<float> oversampler (1, 2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);

    oversampler.initProcessing ((size_t) blockSize);
    oversampler.reset();

    juce::AudioBuffer<float> buffer (1, blockSize);
    auto peak = 0.0;

    for (size_t position = 0; position < channel.size(); position += blockSize)
    {
        const auto thisBlock = (int) juce::jmin ((size_t) blockSize, channel.size() - position);
        buffer.setSize (1, thisBlock, false, false, true);

        for (int i = 0; i < thisBlock; ++i)
            buffer.setSample (0, i, channel[position + (size_t) i]);

        juce::dsp::AudioBlock<float> block (buffer);
        auto upsampled = oversampler.processSamplesUp (block);

        for (size_t i = 0; i < upsampled.getNumSamples(); ++i)
            peak = juce::jmax (peak, (double) std::abs (upsampled.getSample (0, (int) i)));
    }

    return peak;
}

void testSlidingWindows()
{
    std::printf ("\nSliding minimum and moving average\n");

    // Against brute force on random data: this is the piece everything else
    // depends on, and an off-by-one in the window would be invisible in the
    // audio and fatal to the ceiling.
    dsp::SlidingMinimum minimum;
    minimum.prepare (64);
    minimum.setWindow (17);

    juce::Random random (4321);
    std::vector<float> values;
    auto worstError = 0.0;

    for (int i = 0; i < 5000; ++i)
    {
        const auto value = random.nextFloat();
        values.push_back (value);

        const auto measured = minimum.process (value);

        auto expected = values.back();

        for (int k = 1; k < 17 && (int) values.size() - 1 - k >= 0; ++k)
            expected = juce::jmin (expected, values[values.size() - 1 - (size_t) k]);

        worstError = juce::jmax (worstError, (double) std::abs (measured - expected));
    }

    checkClose (worstError, 0.0, 1.0e-7, "sliding minimum matches a brute force scan");

    dsp::MovingAverage average;
    average.prepare (64);
    average.setLength (10);
    average.primeWith (0.0f);

    float last = 0.0f;

    for (int i = 0; i < 10; ++i)
        last = average.process (1.0f);

    checkClose (last, 1.0, 1.0e-6, "moving average reaches the input after its length");

    average.reset();
    average.setLength (8);
    average.primeWith (1.0f);
    checkClose (average.process (1.0f), 1.0, 1.0e-6, "priming stops it fading in from silence");
}

void testCeiling()
{
    std::printf ("\nThe ceiling\n");

    auto settings = defaultSettings();
    settings.inputGainDb = 18.0f;      // drive it hard
    settings.ceilingDb = -1.0f;

    const auto ceiling = juce::Decibels::decibelsToGain (-1.0);

    // Noise: continuous work for the gain path.
    juce::Random random (99);
    std::vector<std::vector<float>> noise (2, std::vector<float> (48000, 0.0f));

    for (int i = 0; i < 48000; ++i)
    {
        noise[0][(size_t) i] = random.nextFloat() * 1.6f - 0.8f;
        noise[1][(size_t) i] = random.nextFloat() * 1.6f - 0.8f;
    }

    limit::LimitEngine engine;
    engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    engine.setSettings (settings);

    const auto limited = runEngine (engine, noise);

    check (peakOf (limited) <= ceiling + 1.0e-6, "loud noise never passes the ceiling",
           toDb (peakOf (limited)), -1.0);
    check (engine.getSafetyClipCount() == 0, "and the safety clipper never fires",
           (double) engine.getSafetyClipCount(), 0.0);

    // Impulses: the hardest case for a gain path, because there is no warning
    // in the signal itself.
    std::vector<std::vector<float>> impulses (2, std::vector<float> (24000, 0.0f));

    for (int i = 1000; i < 24000; i += 3000)
    {
        impulses[0][(size_t) i] = 1.0f;
        impulses[1][(size_t) i] = -1.0f;
    }

    limit::LimitEngine second;
    second.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });

    auto impulseSettings = defaultSettings();
    impulseSettings.inputGainDb = 12.0f;
    second.setSettings (impulseSettings);

    const auto limitedImpulses = runEngine (second, impulses);

    check (peakOf (limitedImpulses) <= ceiling + 1.0e-6,
           "isolated impulses land on the ceiling, not over it",
           toDb (peakOf (limitedImpulses)), -1.0);
    check (second.getSafetyClipCount() == 0, "  again with no help from the clipper",
           (double) second.getSafetyClipCount(), 0.0);

    // And it really does reach the ceiling: a limiter that stays 3 dB under it
    // would pass the test above while being useless.
    checkClose (toDb (peakOf (limitedImpulses)), -1.0, 0.1,
                "and they reach it rather than stopping short");

    // Every style has to keep the promise, whatever its smoothing does.
    int misbehaving = 0;

    for (int i = 0; i < limit::numStyles; ++i)
    {
        auto styleSettings = defaultSettings();
        styleSettings.inputGainDb = 15.0f;
        styleSettings.style = (limit::LimitStyle) i;

        limit::LimitEngine styleEngine;
        styleEngine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
        styleEngine.setSettings (styleSettings);

        const auto styleOutput = runEngine (styleEngine, noise);

        if (peakOf (styleOutput) > ceiling + 1.0e-6 || styleEngine.getSafetyClipCount() > 0)
        {
            ++misbehaving;
            std::printf ("        %s: peak %.3f dB, %lld clipped\n",
                         limit::styleNames()[i].toRawUTF8(),
                         toDb (peakOf (styleOutput)),
                         (long long) styleEngine.getSafetyClipCount());
        }
    }

    check (misbehaving == 0, "every style keeps the ceiling on its own",
           (double) misbehaving, 0.0);
}

void testTruePeak()
{
    std::printf ("\nTrue peak\n");

    /*  A cosine at a quarter of the sample rate, offset by an eighth of a cycle:
        the samples land either side of every peak, so the sample values are
        0.707 of the real peak. This is the signal that makes the difference
        between a sample peak limiter and a true peak one visible.
    */
    const auto amplitude = 0.9;
    std::vector<std::vector<float>> signal (2, std::vector<float> (24000, 0.0f));

    for (int i = 0; i < 24000; ++i)
    {
        const auto value = (float) (amplitude * std::cos (juce::MathConstants<double>::pi * 0.5 * i
                                                          + juce::MathConstants<double>::pi * 0.25));
        signal[0][(size_t) i] = value;
        signal[1][(size_t) i] = value;
    }

    checkClose (toDb (peakOf (signal)), toDb (amplitude * std::cos (juce::MathConstants<double>::pi * 0.25)),
                0.1, "the test signal's samples sit below its real peak");

    auto settings = defaultSettings();
    settings.ceilingDb = -1.0f;
    settings.truePeak = false;

    limit::LimitEngine sampleOnly;
    sampleOnly.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    sampleOnly.setSettings (settings);
    const auto withoutTp = runEngine (sampleOnly, signal);

    settings.truePeak = true;
    limit::LimitEngine withTp;
    withTp.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    withTp.setSettings (settings);
    const auto withTpOutput = runEngine (withTp, signal);

    const auto plainTp = toDb (truePeakOf (withoutTp[0]));
    const auto guardedTp = toDb (truePeakOf (withTpOutput[0]));

    check (plainTp > -0.9, "without true peak the inter-sample peaks go over", plainTp, 0.0);
    check (guardedTp <= -0.9, "with true peak on they do not", guardedTp, -1.0);
    check (withTp.getSafetyClipCount() == 0, "  and still without the clipper",
           (double) withTp.getSafetyClipCount(), 0.0);
}

void testLatencyAndTransparency()
{
    std::printf ("\nLatency and transparency\n");

    auto settings = defaultSettings();
    settings.lookaheadMs = 5.0f;
    settings.truePeak = false;
    settings.ceilingDb = 0.0f;

    limit::LimitEngine engine;
    engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    engine.setSettings (settings);

    const auto expected = (int) std::round (0.005 * sampleRate);
    checkClose (engine.computeLatencySamples(), expected, 0.5,
                "reported latency is the lookahead");

    // A signal that never asks for limiting has to come out identical, just
    // late. This is the check that catches a gain path that is quietly always
    // a little bit on.
    std::vector<std::vector<float>> quiet (2, std::vector<float> (12000, 0.0f));
    juce::Random random (7);

    for (int i = 0; i < 12000; ++i)
    {
        const auto value = random.nextFloat() * 0.2f - 0.1f;
        quiet[0][(size_t) i] = value;
        quiet[1][(size_t) i] = value * 0.5f;
    }

    const auto output = runEngine (engine, quiet);

    auto worst = 0.0;

    for (int i = expected; i < 12000; ++i)
        worst = juce::jmax (worst, (double) std::abs (output[0][(size_t) i]
                                                      - quiet[0][(size_t) (i - expected)]));

    checkClose (worst, 0.0, 1.0e-6, "an unlimited signal comes out untouched, just delayed");
}

void testDeltaAndExtras()
{
    std::printf ("\nDelta, DC and dither\n");

    auto settings = defaultSettings();
    settings.inputGainDb = 12.0f;

    std::vector<std::vector<float>> signal (2, std::vector<float> (12000, 0.0f));
    juce::Random random (11);

    for (int i = 0; i < 12000; ++i)
    {
        const auto value = random.nextFloat() * 1.4f - 0.7f;
        signal[0][(size_t) i] = value;
        signal[1][(size_t) i] = value;
    }

    limit::LimitEngine normal;
    normal.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    normal.setSettings (settings);
    const auto limited = runEngine (normal, signal);

    settings.delta = true;
    limit::LimitEngine delta;
    delta.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    delta.setSettings (settings);
    const auto removed = runEngine (delta, signal);

    // What was kept plus what was removed is what went in.
    const auto latency = normal.getLatencySamples();
    auto worst = 0.0;

    for (int i = latency + 100; i < 12000; ++i)
    {
        const auto sum = limited[0][(size_t) i] + removed[0][(size_t) i];
        const auto original = signal[0][(size_t) (i - latency)]
                            * juce::Decibels::decibelsToGain (12.0f);
        worst = juce::jmax (worst, (double) std::abs (sum - original));
    }

    checkClose (worst, 0.0, 1.0e-5, "delta plus limited adds back up to the input");

    // DC filter.
    std::vector<std::vector<float>> offset (2, std::vector<float> (24000, 0.3f));

    auto dcSettings = defaultSettings();
    dcSettings.dcFilter = true;

    limit::LimitEngine dc;
    dc.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    dc.setSettings (dcSettings);
    const auto filtered = runEngine (dc, offset);

    auto tail = 0.0;

    for (int i = 20000; i < 24000; ++i)
        tail = juce::jmax (tail, (double) std::abs (filtered[0][(size_t) i]));

    check (tail < 0.02, "the DC filter removes a standing offset", tail, 0.0);

    // Dither: audible only as a noise floor where there was digital silence.
    auto ditherSettings = defaultSettings();
    ditherSettings.dither = limit::DitherMode::sixteenBit;

    std::vector<std::vector<float>> silence (2, std::vector<float> (4800, 0.0f));

    limit::LimitEngine dithered;
    dithered.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    dithered.setSettings (ditherSettings);
    const auto noiseFloor = runEngine (dithered, silence);

    const auto floorDb = toDb (peakOf (noiseFloor));
    check (floorDb > -110.0 && floorDb < -80.0,
           "16 bit dither leaves a noise floor where silence was", floorDb, -90.0);

    limit::LimitEngine clean;
    clean.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    clean.setSettings (defaultSettings());
    checkClose (peakOf (runEngine (clean, silence)), 0.0, 0.0,
                "with dither off, silence stays exactly silent");
}

void testLinking()
{
    std::printf ("\nChannel linking\n");

    auto run = [] (float transientLink)
    {
        auto settings = defaultSettings();
        settings.transientLink = transientLink;
        settings.releaseLink = transientLink;
        settings.ceilingDb = -6.0f;

        std::vector<std::vector<float>> signal (2, std::vector<float> (12000, 0.0f));

        for (int i = 0; i < 12000; ++i)
        {
            const auto phase = 2.0 * juce::MathConstants<double>::pi * 200.0 * i / sampleRate;
            signal[0][(size_t) i] = (float) std::sin (phase);          // full scale
            signal[1][(size_t) i] = (float) (0.1 * std::sin (phase));  // 20 dB down
        }

        limit::LimitEngine engine;
        engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
        engine.setSettings (settings);

        const auto output = runEngine (engine, signal);

        auto rightPeak = 0.0;

        for (int i = 6000; i < 12000; ++i)
            rightPeak = juce::jmax (rightPeak, (double) std::abs (output[1][(size_t) i]));

        return toDb (rightPeak) - toDb (0.1);
    };

    checkClose (run (1.0f), -6.0, 0.3, "fully linked, the quiet side takes the same reduction");
    checkClose (run (0.0f), 0.0, 0.2, "unlinked, it is left alone");
}

void testTargetAndMatching()
{
    std::printf ("\nLoudness target and matched bypass\n");

    // The targets are the whole point of the feature, so the numbers behind the
    // names are worth pinning: a "-14 Streaming" that quietly means -13 would
    // send every master out a decibel hot.
    checkClose (limit::targetLufs (limit::LoudnessTarget::club), -9.0, 1.0e-6, "Club is -9 LUFS");
    checkClose (limit::targetLufs (limit::LoudnessTarget::streaming), -14.0, 1.0e-6, "Streaming is -14");
    checkClose (limit::targetLufs (limit::LoudnessTarget::apple), -16.0, 1.0e-6, "Apple is -16");
    checkClose (limit::targetLufs (limit::LoudnessTarget::broadcast), -23.0, 1.0e-6, "Broadcast is -23");
    check (limit::targetNames().size() == limit::numTargets,
           "every target has a name", (double) limit::targetNames().size(), (double) limit::numTargets);

    /*  The matched bypass is only as honest as the average reduction it is
        built on: the dry signal gets back the input gain minus this figure. On
        steady material the average has to agree with what is actually being
        applied.
    */
    auto settings = defaultSettings();
    settings.inputGainDb = 12.0f;
    settings.ceilingDb = -1.0f;
    settings.truePeak = false;

    limit::LimitEngine engine;
    engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    engine.setSettings (settings);

    /*  A square at -6 dBFS driven 12 dB comes out asking for 7 dB back. Twelve
        seconds of it: the average runs on a 1.5 s time constant, so four
        seconds would still be half a decibel short of the answer — which is
        also why the matched bypass takes a few seconds to settle after a change,
        and why that is the right trade against a level that jumps around.
    */
    const auto amplitude = juce::Decibels::decibelsToGain (-6.0);
    std::vector<std::vector<float>> signal (2, std::vector<float> (48000 * 12, 0.0f));

    for (size_t i = 0; i < signal[0].size(); ++i)
    {
        const auto phase = 2.0 * juce::MathConstants<double>::pi * 200.0 * (double) i / sampleRate;
        const auto value = (float) (amplitude * (std::sin (phase) >= 0.0 ? 1.0 : -1.0));
        signal[0][i] = value;
        signal[1][i] = value;
    }

    runEngine (engine, signal);

    const auto expected = -6.0 + 12.0 - (-1.0);   // driven level minus the ceiling
    checkClose (engine.getAverageReductionDb(), -expected, 0.1,
                "the averaged reduction matches what is being applied");

    // And that is what the matched bypass gives back, so the two should cancel.
    const auto matchDb = settings.inputGainDb + engine.getAverageReductionDb();
    checkClose (matchDb, 12.0 - expected, 0.1,
                "so a matched bypass lands the dry signal at the limited level");
}

void testLoudnessMeter()
{
    std::printf ("\nLoudness (BS.1770)\n");

    dsp::LoudnessMeter meter;
    meter.prepare (sampleRate);

    // The K-weighting gain at 1 kHz, measured through the same filters the meter
    // uses, so the expected figure below is derived rather than copied.
    dsp::Biquad shelf, highPass;
    shelf.setCoefficients (dsp::BiquadCoefficients::highShelf (sampleRate, 1681.974450955533,
                                                               0.7071752369554196, 3.999843853973347));
    highPass.setCoefficients (dsp::BiquadCoefficients::highPass (sampleRate, 38.13547087602444,
                                                                 0.5003270373238773));

    const auto omega = 2.0 * juce::MathConstants<double>::pi * 1000.0 / sampleRate;
    double filteredSquares = 0.0;
    int counted = 0;

    for (int i = 0; i < 48000; ++i)
    {
        const auto sample = (float) std::sin (omega * i);
        const auto weighted = highPass.processSample (shelf.processSample (sample));

        if (i > 4800)
        {
            filteredSquares += (double) weighted * weighted;
            ++counted;
        }
    }

    const auto weightedMeanSquare = filteredSquares / counted;

    // Two channels of the same tone at -20 dBFS: the powers add.
    const auto amplitude = juce::Decibels::decibelsToGain (-20.0);
    const auto expectedLufs = dsp::LoudnessMeter::powerToLufs (
        2.0 * weightedMeanSquare * amplitude * amplitude);

    // Four seconds: the short term figure has nothing to say until it has three.
    constexpr int toneSamples = 48000 * 4;
    std::vector<float> left ((size_t) toneSamples), right ((size_t) toneSamples);

    for (int i = 0; i < toneSamples; ++i)
    {
        const auto value = (float) (amplitude * std::sin (omega * i));
        left[(size_t) i] = value;
        right[(size_t) i] = value;
    }

    const float* pointers[2] { left.data(), right.data() };
    meter.push (pointers, 2, toneSamples);

    checkClose (meter.getMomentaryLufs(), expectedLufs, 0.15,
                "momentary loudness matches the closed form");
    checkClose (meter.getShortTermLufs(), expectedLufs, 0.15, "short term agrees");
    checkClose (meter.getIntegratedLufs(), expectedLufs, 0.20, "integrated agrees");

    /*  Gating. Silence after the programme must stop counting almost
        immediately and then never count again, however long it goes on. The
        blocks that straddle the end of the tone are quieter than the tone but
        still above both gates, so they do pull the figure down a fraction —
        that is the standard working as specified, not a leak. What must not
        happen is the figure continuing to fall as the silence gets longer.
    */
    std::vector<float> silence (48000 * 2, 0.0f);
    const float* silentPointers[2] { silence.data(), silence.data() };

    meter.push (silentPointers, 2, (int) silence.size());
    const auto afterTwoSeconds = meter.getIntegratedLufs();

    for (int i = 0; i < 15; ++i)
        meter.push (silentPointers, 2, (int) silence.size());

    const auto afterThirtyTwoSeconds = meter.getIntegratedLufs();

    checkClose (afterTwoSeconds, expectedLufs, 0.5,
                "the blocks straddling the end cost a fraction of a LU");
    checkClose (afterThirtyTwoSeconds, afterTwoSeconds, 0.001,
                "and thirty more seconds of silence change nothing at all");

    check (meter.getMomentaryLufs() < -100.0f, "  while the momentary reading does fall",
           meter.getMomentaryLufs(), -200.0);

    // A tone 20 dB quieter should read 20 LU quieter.
    dsp::LoudnessMeter quieter;
    quieter.prepare (sampleRate);

    std::vector<float> quiet (48000);

    for (int i = 0; i < 48000; ++i)
        quiet[(size_t) i] = (float) (amplitude * 0.1 * std::sin (omega * i));

    const float* quietPointers[2] { quiet.data(), quiet.data() };
    quieter.push (quietPointers, 2, 48000);

    checkClose (quieter.getMomentaryLufs(), expectedLufs - 20.0, 0.15,
                "20 dB quieter reads 20 LU quieter");
}
} // namespace

Result runLimiterTests()
{
    std::printf ("\n\nNodo Limit verification\n=======================\n");

    testSlidingWindows();
    testCeiling();
    testTruePeak();
    testLatencyAndTransparency();
    testDeltaAndExtras();
    testLinking();
    testLoudnessMeter();
    testTargetAndMatching();

    return { checks, failures };
}
} // namespace nodo::tests
