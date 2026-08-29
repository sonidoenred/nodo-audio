/*
    Verification for the compressor.

    A compressor is harder to eyeball than a filter: there is no curve on screen
    to compare against, and "it sounds squashed" is true of both a correct one
    and a broken one. So every check here is a number with a closed form behind
    it — the static curve from the gain computer, the 63 % point of a one-pole
    for the attack and release times, the RMS of a sine — measured through the
    real engine on real buffers rather than by calling the maths twice.
*/

#include "CompTests.h"

#include <nodo_core/nodo_core.h>
#include "../plugins/nodo_comp/source/CompEngine.h"
#include "../plugins/nodo_comp/source/FactoryPresets.h"

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
constexpr int blockSize = 128;

double toDb (double linear)
{
    return 20.0 * std::log10 (juce::jmax (1.0e-12, linear));
}

comp::CompSettings defaultSettings()
{
    comp::CompSettings s;
    s.thresholdDb = -20.0f;
    s.ratio = 4.0f;
    s.kneeDb = 0.0f;
    s.attackMs = 1.0f;
    s.releaseMs = 500.0f;
    s.makeupDb = 0.0f;
    s.autoGain = false;
    s.mix = 1.0f;
    s.style = comp::CompStyle::clean;
    s.detection = comp::Detection::peak;
    s.stereoLink = 1.0f;
    s.lookaheadMs = 0.0f;
    s.sidechain = comp::SidechainSource::internal;
    s.scHighpassHz = 20.0f;
    s.scLowpassHz = 20000.0f;
    s.scListen = false;
    return s;
}

/** Runs a steady sine through the engine and returns the peak of the last
    stretch, in dB. Everything before that is settling time.
*/
double steadyOutputDb (const comp::CompSettings& settings,
                       double amplitude,
                       double frequency = 1000.0,
                       double seconds = 3.0,
                       bool square = false)
{
    comp::CompEngine engine;
    engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    engine.setSettings (settings);

    const auto totalSamples = (int) (seconds * sampleRate);
    const auto measureFrom = totalSamples - (int) (0.1 * sampleRate);
    const auto omega = 2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;

    juce::AudioBuffer<float> buffer (2, blockSize);
    double peak = 0.0;
    int position = 0;

    while (position < totalSamples)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < blockSize; ++i)
            {
                const auto phase = std::sin (omega * (position + i));
                buffer.setSample (ch, i, (float) (amplitude * (square ? (phase >= 0.0 ? 1.0 : -1.0)
                                                                      : phase)));
            }

        juce::dsp::AudioBlock<float> block (buffer);
        engine.process (block, nullptr);

        if (position >= measureFrom)
            for (int i = 0; i < blockSize; ++i)
                peak = juce::jmax (peak, (double) std::abs (buffer.getSample (0, i)));

        position += blockSize;
    }

    return toDb (peak);
}

void testStaticCurve()
{
    std::printf ("\nGain computer\n");

    // Hard knee, textbook numbers: 10 dB over a 4:1 threshold comes out 2.5 dB
    // over, so the reduction is 7.5 dB.
    checkClose (dsp::computeStaticGainDb (-10.0f, -20.0f, 4.0f, 0.0f), -7.5, 1.0e-4,
                "4:1 at 10 dB over gives 7.5 dB of reduction");

    checkClose (dsp::computeStaticGainDb (-30.0f, -20.0f, 4.0f, 0.0f), 0.0, 1.0e-6,
                "below the threshold nothing happens");

    checkClose (dsp::computeStaticGainDb (-5.0f, -20.0f, 10000.0f, 0.0f), -15.0, 1.0e-3,
                "infinite ratio pins the output to the threshold");

    checkClose (dsp::computeStaticGainDb (-10.0f, -20.0f, 1.0f, 0.0f), 0.0, 1.0e-6,
                "1:1 does nothing at all");

    // At the threshold itself a soft knee is already working, by exactly
    // slope * knee / 8.
    const auto slope = 1.0f - 1.0f / 4.0f;
    checkClose (dsp::computeStaticGainDb (-20.0f, -20.0f, 4.0f, 6.0f),
                -slope * 6.0 / 8.0, 1.0e-4,
                "soft knee at the threshold is slope*knee/8");

    // The knee has to meet the straight sections without a step.
    const auto justBelow = dsp::computeStaticGainDb (-23.01f, -20.0f, 4.0f, 6.0f);
    const auto justAbove = dsp::computeStaticGainDb (-16.99f, -20.0f, 4.0f, 6.0f);
    checkClose (justBelow, 0.0, 0.01, "knee starts flat at threshold - knee/2");
    checkClose (justAbove, -slope * 3.0, 0.02, "knee lands on the line at threshold + knee/2");
}

void testSteadyState()
{
    std::printf ("\nSteady state through the engine\n");

    auto settings = defaultSettings();

    // -10 dBFS in, 4:1 over a -20 dB threshold: -17.5 dBFS out.
    checkClose (steadyOutputDb (settings, juce::Decibels::decibelsToGain (-10.0)),
                -17.5, 0.35, "1 kHz at -10 dBFS comes out at -17.5 dBFS");

    settings.ratio = 2.0f;
    checkClose (steadyOutputDb (settings, juce::Decibels::decibelsToGain (-10.0)),
                -15.0, 0.35, "2:1 on the same signal comes out at -15 dBFS");

    settings.ratio = 1.0f;
    checkClose (steadyOutputDb (settings, juce::Decibels::decibelsToGain (-10.0)),
                -10.0, 0.05, "1:1 leaves the level alone");

    settings.ratio = 4.0f;
    checkClose (steadyOutputDb (settings, juce::Decibels::decibelsToGain (-30.0)),
                -30.0, 0.05, "a signal under the threshold passes untouched");

    // Makeup is applied after the gain element, so it adds cleanly.
    settings.makeupDb = 6.0f;
    checkClose (steadyOutputDb (settings, juce::Decibels::decibelsToGain (-10.0)),
                -11.5, 0.35, "makeup adds on top of the reduction");

    // Mix at 50 % is an average of the two paths in the linear domain.
    settings.makeupDb = 0.0f;
    settings.mix = 0.5f;
    const auto wet = juce::Decibels::decibelsToGain (-17.5);
    const auto dry = juce::Decibels::decibelsToGain (-10.0);
    checkClose (steadyOutputDb (settings, juce::Decibels::decibelsToGain (-10.0)),
                toDb (0.5 * wet + 0.5 * dry), 0.4,
                "50 % mix averages the compressed and dry paths");
}

/** Feeds a step and returns how many milliseconds the gain takes to cover
    63.2 % of the distance to its final value.
*/
double measuredTimeConstantMs (const comp::CompSettings& settings, bool attack)
{
    comp::CompEngine engine;
    engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    engine.setSettings (settings);

    const auto loud = juce::Decibels::decibelsToGain (-8.0);
    const auto quiet = juce::Decibels::decibelsToGain (-40.0);
    const auto omega = 2.0 * juce::MathConstants<double>::pi * 1000.0 / sampleRate;

    // A square wave: its rectified envelope is flat, so what is being measured
    // is the smoother alone rather than the smoother plus the shape of a sine.
    auto wave = [omega] (int index) { return std::sin (omega * index) >= 0.0 ? 1.0 : -1.0; };

    juce::AudioBuffer<float> buffer (2, blockSize);

    auto run = [&] (double amplitude, int samples, auto&& onBlock)
    {
        for (int position = 0; position < samples; position += blockSize)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i)
                    buffer.setSample (ch, i, (float) (amplitude * wave (position + i)));

            juce::dsp::AudioBlock<float> block (buffer);
            engine.process (block, nullptr);
            onBlock (position);
        }
    };

    // Settle at the starting level.
    run (attack ? quiet : loud, (int) (0.5 * sampleRate), [] (int) {});

    const auto startDb = engine.getGainReductionDb();

    // The target the gain is heading towards, measured after it has arrived.
    run (attack ? loud : quiet, (int) (2.0 * sampleRate), [] (int) {});
    const auto endDb = engine.getGainReductionDb();

    // Run it again, this time watching for the crossing.
    comp::CompEngine second;
    second.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    second.setSettings (settings);

    auto crossingSamples = -1;
    const auto threshold = startDb + 0.632f * (endDb - startDb);

    {
        comp::CompEngine& engineRef = second;
        auto localRun = [&] (double amplitude, int samples, bool watch)
        {
            for (int position = 0; position < samples; position += blockSize)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < blockSize; ++i)
                        buffer.setSample (ch, i, (float) (amplitude * wave (position + i)));

                juce::dsp::AudioBlock<float> block (buffer);
                engineRef.process (block, nullptr);

                if (watch && crossingSamples < 0)
                {
                    const auto now = engineRef.getGainReductionDb();
                    const auto passed = attack ? now <= threshold : now >= threshold;

                    if (passed)
                        crossingSamples = position + blockSize;
                }
            }
        };

        localRun (attack ? quiet : loud, (int) (0.5 * sampleRate), false);
        localRun (attack ? loud : quiet, (int) (2.0 * sampleRate), true);
    }

    return crossingSamples < 0 ? -1.0 : 1000.0 * crossingSamples / sampleRate;
}

void testTiming()
{
    std::printf ("\nAttack and release times\n");

    auto settings = defaultSettings();
    settings.attackMs = 50.0f;
    settings.releaseMs = 400.0f;

    // The block size quantises the measurement to 2.7 ms, and the detector adds
    // a fraction of a cycle, so the tolerance is generous. What is being checked
    // is that the knob means what it says, not that it is exact to the sample.
    const auto attack = measuredTimeConstantMs (settings, true);
    checkClose (attack, 50.0, 12.0, "attack reaches 63 % in about the stated time");

    const auto release = measuredTimeConstantMs (settings, false);
    checkClose (release, 400.0, 90.0, "release reaches 63 % in about the stated time");

    // Clean has no program dependence, so doubling the release doubles the time.
    settings.releaseMs = 800.0f;
    const auto longer = measuredTimeConstantMs (settings, false);
    check (longer > release * 1.5, "doubling the release roughly doubles the time",
           longer, release * 2.0);
}

void testDetection()
{
    std::printf ("\nDetection\n");

    // A sine's RMS is its peak over root two: 3.01 dB down.
    dsp::RmsFollower follower;
    follower.prepare (sampleRate);
    follower.setWindow (50.0f);

    const auto omega = 2.0 * juce::MathConstants<double>::pi * 1000.0 / sampleRate;
    float value = 0.0f;

    for (int i = 0; i < (int) sampleRate; ++i)
        value = follower.process ((float) std::sin (omega * i));

    checkClose (toDb (value), -3.01, 0.15, "RMS of a full scale sine is -3 dB");

    // Same signal, both detectors: RMS asks for 3 dB less reduction than peak.
    auto settings = defaultSettings();
    settings.attackMs = 5.0f;

    const auto peakOut = steadyOutputDb (settings, juce::Decibels::decibelsToGain (-10.0));

    settings.detection = comp::Detection::rms;
    const auto rmsOut = steadyOutputDb (settings, juce::Decibels::decibelsToGain (-10.0));

    checkClose (rmsOut - peakOut, 3.01 * (1.0 - 1.0 / 4.0), 0.5,
                "RMS detection reduces less than peak by slope * 3 dB");
}

void testStereoLink()
{
    std::printf ("\nStereo link\n");

    auto run = [] (float link)
    {
        auto settings = defaultSettings();
        settings.stereoLink = link;
        settings.attackMs = 1.0f;

        comp::CompEngine engine;
        engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
        engine.setSettings (settings);

        const auto loud = juce::Decibels::decibelsToGain (-6.0);
        const auto quiet = juce::Decibels::decibelsToGain (-40.0);
        const auto omega = 2.0 * juce::MathConstants<double>::pi * 1000.0 / sampleRate;

        juce::AudioBuffer<float> buffer (2, blockSize);
        double rightPeak = 0.0;
        const auto total = (int) sampleRate;

        for (int position = 0; position < total; position += blockSize)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto s = std::sin (omega * (position + i));
                buffer.setSample (0, i, (float) (loud * s));
                buffer.setSample (1, i, (float) (quiet * s));
            }

            juce::dsp::AudioBlock<float> block (buffer);
            engine.process (block, nullptr);

            if (position > total - (int) (0.1 * sampleRate))
                for (int i = 0; i < blockSize; ++i)
                    rightPeak = juce::jmax (rightPeak, (double) std::abs (buffer.getSample (1, i)));
        }

        return toDb (rightPeak) + 40.0;   // change applied to the quiet channel
    };

    // Fully linked, the quiet channel gets the loud channel's gain: -6 dBFS is
    // 14 dB over the threshold, so 10.5 dB of reduction.
    checkClose (run (1.0f), -10.5, 0.5, "linked, the quiet channel follows the loud one");

    // Unlinked, a channel 40 dB down is nowhere near the threshold.
    checkClose (run (0.0f), 0.0, 0.2, "unlinked, the quiet channel is left alone");
}

void testSidechain()
{
    std::printf ("\nSidechain\n");

    // A 40 Hz tone with the detector high passed at 500 Hz should barely move
    // the gain, and should move it a lot with the filter off.
    auto settings = defaultSettings();
    settings.attackMs = 5.0f;

    const auto unfiltered = steadyOutputDb (settings, juce::Decibels::decibelsToGain (-6.0), 40.0);

    settings.scHighpassHz = 500.0f;
    const auto filtered = steadyOutputDb (settings, juce::Decibels::decibelsToGain (-6.0), 40.0);

    check (filtered > unfiltered + 8.0,
           "a sidechain high pass stops low end triggering it", filtered - unfiltered, 8.0);
    checkClose (filtered, -6.0, 0.6, "with the low end filtered out it barely works");

    // External sidechain: the main signal is quiet, the sidechain is loud, and
    // the main signal should duck anyway.
    settings = defaultSettings();
    settings.sidechain = comp::SidechainSource::external;
    settings.attackMs = 1.0f;

    comp::CompEngine engine;
    engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    engine.setSettings (settings);

    const auto main = juce::Decibels::decibelsToGain (-25.0);
    const auto side = juce::Decibels::decibelsToGain (-6.0);
    const auto omega = 2.0 * juce::MathConstants<double>::pi * 1000.0 / sampleRate;

    juce::AudioBuffer<float> buffer (2, blockSize), sidechainBuffer (2, blockSize);
    double peak = 0.0;
    const auto total = (int) sampleRate;

    for (int position = 0; position < total; position += blockSize)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto s = std::sin (omega * (position + i));
                buffer.setSample (ch, i, (float) (main * s));
                sidechainBuffer.setSample (ch, i, (float) (side * s));
            }
        }

        juce::dsp::AudioBlock<float> block (buffer);
        const juce::dsp::AudioBlock<const float> scBlock (sidechainBuffer);
        engine.process (block, &scBlock);

        if (position > total - (int) (0.1 * sampleRate))
            for (int i = 0; i < blockSize; ++i)
                peak = juce::jmax (peak, (double) std::abs (buffer.getSample (0, i)));
    }

    // -6 dBFS sidechain is 14 dB over the threshold: 10.5 dB of ducking.
    checkClose (toDb (peak), -35.5, 0.5, "an external sidechain ducks the main signal");
}

void testLookahead()
{
    std::printf ("\nLookahead\n");

    auto settings = defaultSettings();
    settings.lookaheadMs = 5.0f;
    settings.ratio = 1.0f;   // transparent, so only the delay shows up

    comp::CompEngine engine;
    engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    engine.setSettings (settings);

    const auto expectedDelay = (int) std::round (0.005 * sampleRate);
    checkClose (engine.computeLatencySamples(), expectedDelay, 0.5,
                "reported latency matches the lookahead");

    // An impulse should come out exactly that many samples later, unchanged.
    juce::AudioBuffer<float> buffer (2, blockSize);
    std::vector<float> output;
    output.reserve (blockSize * 8);

    for (int block = 0; block < 8; ++block)
    {
        buffer.clear();

        if (block == 0)
            buffer.setSample (0, 10, 1.0f);

        juce::dsp::AudioBlock<float> audioBlock (buffer);
        engine.process (audioBlock, nullptr);

        for (int i = 0; i < blockSize; ++i)
            output.push_back (buffer.getSample (0, i));
    }

    auto peakIndex = 0;

    for (size_t i = 0; i < output.size(); ++i)
        if (std::abs (output[i]) > std::abs (output[(size_t) peakIndex]))
            peakIndex = (int) i;

    checkClose (peakIndex - 10, expectedDelay, 0.5, "the impulse arrives one lookahead later");
    checkClose (output[(size_t) peakIndex], 1.0, 0.02, "and arrives at full scale");
}

void testAutoGain()
{
    std::printf ("\nAuto gain\n");

    auto settings = defaultSettings();
    settings.autoGain = true;
    settings.attackMs = 5.0f;
    settings.releaseMs = 100.0f;

    // Auto gain follows the reduction with a 1.5 s time constant, so it needs a
    // few seconds of steady signal before it has caught up.
    const auto output = steadyOutputDb (settings, juce::Decibels::decibelsToGain (-10.0),
                                        1000.0, 8.0);

    checkClose (output, -10.0, 1.0, "auto gain brings a compressed tone back to size");
}

void testRange()
{
    std::printf ("\nRange\n");

    auto settings = defaultSettings();
    settings.ratio = 20.0f;          // reads inf:1, so without a range this pins
    settings.rangeDb = 6.0f;

    // 24 dB over an infinite ratio would clamp the signal to the threshold.
    // With the range at 6 dB the most it may do is 6 dB.
    checkClose (steadyOutputDb (settings, juce::Decibels::decibelsToGain (-4.0),
                                1000.0, 3.0, true),
                -10.0, 0.2, "range caps the reduction however far over it goes");

    settings.rangeDb = 60.0f;
    checkClose (steadyOutputDb (settings, juce::Decibels::decibelsToGain (-4.0),
                                1000.0, 3.0, true),
                -20.0, 0.3, "with the range open it pins to the threshold");

    // And the drawn curve has to agree with both.
    settings.rangeDb = 6.0f;
    checkClose (comp::CompEngine::staticOutputDb (-4.0f, settings), -10.0, 0.05,
                "the curve knows about the range too");
}

void testUpward()
{
    std::printf ("\nUpward compression\n");

    // Mirror image of the downward curve: 10 dB under a 4:1 threshold comes out
    // 7.5 dB higher.
    checkClose (dsp::computeUpwardGainDb (-30.0f, -20.0f, 4.0f, 0.0f), 7.5, 1.0e-4,
                "4:1 at 10 dB under gives 7.5 dB of lift");

    checkClose (dsp::computeUpwardGainDb (-10.0f, -20.0f, 4.0f, 0.0f), 0.0, 1.0e-6,
                "material above the threshold is left alone");

    auto settings = defaultSettings();
    settings.direction = comp::Direction::upward;
    settings.rangeDb = 60.0f;

    checkClose (steadyOutputDb (settings, juce::Decibels::decibelsToGain (-30.0),
                                1000.0, 3.0, true),
                -22.5, 0.3, "a quiet signal is lifted through the engine");

    checkClose (steadyOutputDb (settings, juce::Decibels::decibelsToGain (-10.0),
                                1000.0, 3.0, true),
                -10.0, 0.1, "a loud one is not touched");

    // Range matters more here than anywhere: this is what stops the noise floor
    // being amplified by forty decibels.
    settings.rangeDb = 3.0f;
    checkClose (steadyOutputDb (settings, juce::Decibels::decibelsToGain (-50.0),
                                1000.0, 3.0, true),
                -47.0, 0.2, "range bounds the lift given to near silence");

    // Attack and release must not swap over when the direction does: attack is
    // still the time it takes to start working.
    settings.rangeDb = 60.0f;
    settings.attackMs = 100.0f;
    settings.releaseMs = 5.0f;

    comp::CompEngine engine;
    engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    engine.setSettings (settings);

    juce::AudioBuffer<float> buffer (2, blockSize);
    const auto quiet = juce::Decibels::decibelsToGain (-30.0);
    const auto omega = 2.0 * juce::MathConstants<double>::pi * 1000.0 / sampleRate;

    // After 20 ms of a 100 ms attack the gain should have moved a little, not
    // all the way.
    for (int position = 0; position < (int) (0.02 * sampleRate); position += blockSize)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < blockSize; ++i)
                buffer.setSample (ch, i, (float) (quiet * (std::sin (omega * (position + i)) >= 0.0 ? 1.0 : -1.0)));

        juce::dsp::AudioBlock<float> block (buffer);
        engine.process (block, nullptr);
    }

    const auto partial = -engine.getGainReductionDb();   // reported negated in upward
    check (partial > 0.5f && partial < 6.0f,
           "a slow attack still ramps the lift in gradually", partial, 1.4);
}

void testHold()
{
    std::printf ("\nHold\n");

    auto measure = [] (float holdMs, double afterSeconds)
    {
        auto settings = defaultSettings();
        settings.attackMs = 1.0f;
        settings.releaseMs = 30.0f;
        settings.holdMs = holdMs;

        comp::CompEngine engine;
        engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
        engine.setSettings (settings);

        juce::AudioBuffer<float> buffer (2, blockSize);
        const auto loud = juce::Decibels::decibelsToGain (-8.0);
        const auto omega = 2.0 * juce::MathConstants<double>::pi * 1000.0 / sampleRate;

        auto run = [&] (double amplitude, int samples)
        {
            for (int position = 0; position < samples; position += blockSize)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < blockSize; ++i)
                    {
                        const auto square = std::sin (omega * (position + i)) >= 0.0 ? 1.0 : -1.0;
                        buffer.setSample (ch, i, (float) (amplitude * square));
                    }

                juce::dsp::AudioBlock<float> block (buffer);
                engine.process (block, nullptr);
            }
        };

        run (loud, (int) (0.5 * sampleRate));
        run (0.0, (int) (afterSeconds * sampleRate));

        return engine.getGainReductionDb();
    };

    // -8 dBFS is 12 dB over the threshold: 9 dB of reduction at 4:1.
    // 30 ms after the signal stops, a 200 ms hold should not have let go yet.
    checkClose (measure (200.0f, 0.03), -9.0, 0.3, "the gain is still held 30 ms after the signal stops");

    // Without hold, a 30 ms release has covered most of the distance by then.
    const auto released = measure (0.0f, 0.03);
    check (released > -4.0f, "without hold the same gap lets it go", released, -1.0);

    // And once the hold expires it releases normally.
    const auto afterHold = measure (50.0f, 0.20);
    check (afterHold > -1.0f, "after the hold expires it releases as usual", afterHold, 0.0);
}

void testAutoControls()
{
    std::printf ("\nAuto threshold and auto release\n");

    // The point of auto threshold: the same setting does the same amount of work
    // on material 20 dB apart.
    auto reduction = [] (bool automatic, double inputDb)
    {
        auto settings = defaultSettings();
        settings.autoThreshold = automatic;
        settings.attackMs = 5.0f;
        settings.releaseMs = 200.0f;

        return steadyOutputDb (settings, juce::Decibels::decibelsToGain (inputDb),
                               1000.0, 6.0, true) - inputDb;
    };

    const auto fixedLoud = reduction (false, -10.0);
    const auto fixedQuiet = reduction (false, -30.0);

    check (std::abs (fixedLoud - fixedQuiet) > 5.0,
           "with a fixed threshold, 20 dB quieter means far less work",
           std::abs (fixedLoud - fixedQuiet), 5.0);

    const auto autoLoud = reduction (true, -10.0);
    const auto autoQuiet = reduction (true, -30.0);

    checkClose (autoLoud, autoQuiet, 1.0,
                "with auto threshold both get the same treatment");

    // Auto release: deeper reduction has to take longer to let go than shallow
    // reduction, which is the whole idea.
    auto releaseTime = [] (double inputDb)
    {
        auto settings = defaultSettings();
        settings.autoRelease = true;
        settings.attackMs = 1.0f;
        settings.releaseMs = 100.0f;

        comp::CompEngine engine;
        engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
        engine.setSettings (settings);

        juce::AudioBuffer<float> buffer (2, blockSize);
        const auto omega = 2.0 * juce::MathConstants<double>::pi * 1000.0 / sampleRate;
        const auto amplitude = juce::Decibels::decibelsToGain (inputDb);

        auto run = [&] (double level, int samples, bool watch, float target)
        {
            for (int position = 0; position < samples; position += blockSize)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < blockSize; ++i)
                    {
                        const auto square = std::sin (omega * (position + i)) >= 0.0 ? 1.0 : -1.0;
                        buffer.setSample (ch, i, (float) (level * square));
                    }

                juce::dsp::AudioBlock<float> block (buffer);
                engine.process (block, nullptr);

                if (watch && engine.getGainReductionDb() >= target)
                    return 1000.0 * (position + blockSize) / sampleRate;
            }

            return -1.0;
        };

        run (amplitude, (int) (1.0 * sampleRate), false, 0.0f);
        const auto settled = engine.getGainReductionDb();

        // Time to give back half of whatever it was doing.
        return run (0.0, (int) (3.0 * sampleRate), true, settled * 0.5f);
    };

    const auto shallow = releaseTime (-16.0);
    const auto deep = releaseTime (-2.0);

    check (deep > shallow * 1.3, "auto release lets go slower after a deeper reduction",
           deep, shallow * 1.3);
}

void testStyles()
{
    std::printf ("\nStyles\n");

    auto settings = defaultSettings();
    settings.kneeDb = 6.0f;
    settings.attackMs = 10.0f;

    const auto amplitude = juce::Decibels::decibelsToGain (-10.0);

    const auto clean = steadyOutputDb (settings, amplitude, 1000.0, 3.0);

    settings.style = comp::CompStyle::opto;
    const auto opto = steadyOutputDb (settings, amplitude, 1000.0, 3.0);

    // A feedback detector cannot clamp as hard as a feed forward one: it only
    // ever sees what it has already let through.
    check (opto > clean, "the feedback styles reduce less than clean", opto, clean);

    settings.style = comp::CompStyle::punch;
    const auto punch = steadyOutputDb (settings, amplitude, 1000.0, 3.0);

    // Punch halves the knee, so at 10 dB over the threshold it is on the
    // straight section either way and lands within a whisker of clean.
    checkClose (punch, clean, 0.5, "punch matches clean once past the knee");

    // Every style has to leave a signal under the threshold alone.
    int misbehaving = 0;

    for (int i = 0; i < comp::numStyles; ++i)
    {
        settings.style = (comp::CompStyle) i;
        const auto quiet = steadyOutputDb (settings, juce::Decibels::decibelsToGain (-45.0),
                                           1000.0, 1.5);

        if (std::abs (quiet + 45.0) > 0.5)
        {
            ++misbehaving;
            std::printf ("        %s touches a signal 25 dB under the threshold (%.2f dB)\n",
                         comp::styleNames()[i].toRawUTF8(), quiet + 45.0);
        }
    }

    check (misbehaving == 0, "no style touches a signal well under the threshold",
           (double) misbehaving, 0.0);
}

void testStability()
{
    std::printf ("\nStability\n");

    auto settings = defaultSettings();
    settings.style = comp::CompStyle::opto;   // the feedback path is the risky one
    settings.ratio = 20.0f;
    settings.attackMs = 0.05f;
    settings.releaseMs = 5.0f;
    settings.lookaheadMs = 12.0f;

    comp::CompEngine engine;
    engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    engine.setSettings (settings);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::Random random (20260815);
    auto worst = 0.0f;

    // Loud noise, then silence: the first finds instability, the second finds
    // denormals and anything that fails to decay.
    for (int block = 0; block < 400; ++block)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < blockSize; ++i)
                buffer.setSample (ch, i, block < 200 ? random.nextFloat() * 2.0f - 1.0f : 0.0f);

        juce::dsp::AudioBlock<float> block2 (buffer);
        engine.process (block2, nullptr);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < blockSize; ++i)
                worst = juce::jmax (worst, std::abs (buffer.getSample (ch, i)));
    }

    check (std::isfinite (worst) && worst <= 2.0f, "hammering it stays finite and bounded",
           (double) worst, 1.0);

    auto tail = 0.0f;

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < blockSize; ++i)
            tail = juce::jmax (tail, std::abs (buffer.getSample (ch, i)));

    check (tail == 0.0f, "silence in, silence out", (double) tail, 0.0);
}

class HarnessProcessor : public juce::AudioProcessor
{
public:
    HarnessProcessor()
        : apvts (*this, nullptr, "NodoComp", comp::createParameterLayout()) {}

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    const juce::String getName() const override { return "Harness"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    juce::AudioProcessorValueTreeState apvts;
};

void testFactoryPresets()
{
    std::printf ("\nFactory presets\n");

    HarnessProcessor harness;
    const auto presets = comp::buildFactoryPresets();

    check (presets.size() >= 20, "there are enough presets to be useful",
           (double) presets.size(), 20.0);

    /*  Mojibake canary. juce::String reads a plain const char* as ASCII, so a
        preset written in Spanish arrives on screen as "Sala pequeÃ±a" - the
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


    int missingIds = 0, outOfRange = 0;

    for (const auto& preset : presets)
    {
        for (const auto& [id, value] : preset.assignments)
        {
            auto* parameter = harness.apvts.getParameter (id);

            if (parameter == nullptr)
            {
                ++missingIds;
                std::printf ("        unknown parameter: %s\n", id.toRawUTF8());
                continue;
            }

            const auto restored = parameter->convertFrom0to1 (parameter->convertTo0to1 (value));

            if (std::abs (restored - value) > juce::jmax (0.02f, std::abs (value) * 0.002f))
            {
                ++outOfRange;
                std::printf ("        %s: %.3f does not survive the range (got %.3f)\n",
                             id.toRawUTF8(), value, restored);
            }
        }
    }

    check (missingIds == 0, "every preset writes to a parameter that exists",
           (double) missingIds, 0.0);
    check (outOfRange == 0, "every preset value is inside its parameter's range",
           (double) outOfRange, 0.0);

    // Descriptions are what the tooltip shows; an empty one is a preset nobody
    // can tell apart from the one above it.
    int missingDescriptions = 0;

    for (const auto& preset : presets)
        if (preset.description.isEmpty() || preset.category.isEmpty())
            ++missingDescriptions;

    check (missingDescriptions == 0, "every preset has a category and a description",
           (double) missingDescriptions, 0.0);
}

void testCurveMatchesEngine()
{
    std::printf ("\nCurve against measurement\n");

    // The transfer curve drawn on screen has to be the same function the audio
    // goes through. This is the compressor's version of the EQ's "what you see
    // is what you hear" check.
    //
    // Measured on a square wave, whose rectified envelope is constant. On a
    // sine the gain ripples inside every cycle — the detector sees the peak,
    // then the level collapses towards zero and the release starts letting go
    // before the next peak pulls it back down. That ripple is real compressor
    // behaviour, not an error in the curve, and it grows with the depth of the
    // reduction; the check below measures it rather than pretending it is zero.
    auto settings = defaultSettings();
    settings.kneeDb = 8.0f;
    settings.attackMs = 2.0f;
    settings.releaseMs = 200.0f;

    int mismatches = 0;
    double worst = 0.0;

    for (auto inputDb : { -40.0, -30.0, -24.0, -20.0, -16.0, -12.0, -6.0, -2.0 })
    {
        const auto drawn = comp::CompEngine::staticOutputDb ((float) inputDb, settings);
        const auto measured = steadyOutputDb (settings, juce::Decibels::decibelsToGain (inputDb),
                                              1000.0, 3.0, true);
        const auto error = std::abs (drawn - measured);

        worst = juce::jmax (worst, error);

        if (error > 0.15)
        {
            ++mismatches;
            std::printf ("        %.0f dB in: curve says %.2f, engine gives %.2f\n",
                         inputDb, drawn, measured);
        }
    }

    check (mismatches == 0, "the drawn curve matches the measured output", worst, 0.0);

    // And the size of the ripple on a sine, pinned so a future change to the
    // detector cannot quietly make the compressor sloppier.
    const auto onSine = steadyOutputDb (settings, juce::Decibels::decibelsToGain (-2.0),
                                        1000.0, 3.0, false);
    const auto ripple = onSine - comp::CompEngine::staticOutputDb (-2.0f, settings);

    check (ripple > 0.0 && ripple < 1.0,
           "peak detection on a sine rides above the curve, but not by much", ripple, 0.5);
}
} // namespace

Result runCompressorTests()
{
    std::printf ("\n\nNodo Comp verification\n======================\n");

    testStaticCurve();
    testSteadyState();
    testTiming();
    testDetection();
    testStereoLink();
    testSidechain();
    testLookahead();
    testAutoGain();
    testRange();
    testUpward();
    testHold();
    testAutoControls();
    testStyles();
    testStability();
    testFactoryPresets();
    testCurveMatchesEngine();

    return { checks, failures };
}
} // namespace nodo::tests
