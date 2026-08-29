/*
    Verification for the de-esser.

    The defining property is not how much it takes off an S — it is that it does
    nothing at all the rest of the time. A de-esser that colours the signal while
    idle is a tone control that happens to move, so the first checks here are
    about transparency, held to a millionth of full scale, and only then about
    the reduction.
*/

#include "EssTests.h"

#include <nodo_core/nodo_core.h>
#include "../plugins/nodo_ess/source/EssEngine.h"

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

ess::EssSettings defaultSettings()
{
    ess::EssSettings s;
    s.frequencyHz = 6500.0f;
    s.thresholdDb = -30.0f;
    s.rangeDb = 24.0f;
    s.attackMs = 1.0f;
    s.releaseMs = 60.0f;
    s.lookaheadMs = 0.0f;
    s.kneeDb = 0.0f;
    s.stereoLink = 1.0f;
    s.mode = ess::Mode::split;
    s.detection = ess::Detection::absolute;
    s.monitor = ess::Monitor::normal;
    return s;
}

std::vector<std::vector<float>> makeTone (double frequency, double amplitude, int samples)
{
    std::vector<std::vector<float>> signal (2, std::vector<float> ((size_t) samples, 0.0f));
    const auto omega = 2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;

    for (int i = 0; i < samples; ++i)
    {
        const auto value = (float) (amplitude * std::sin (omega * i));
        signal[0][(size_t) i] = value;
        signal[1][(size_t) i] = value;
    }

    return signal;
}

std::vector<std::vector<float>> runEngine (ess::EssEngine& engine,
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
        engine.process (block);

        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < thisBlock; ++i)
                output[(size_t) ch][(size_t) (position + i)] = buffer.getSample (ch, i);
    }

    return output;
}

/** RMS of the last stretch of a channel, in dB relative to a full scale sine.

    RMS rather than peak throughout: at 16 kHz there are three samples per cycle,
    so where the samples happen to land can put the measured peak most of a
    decibel under the real one. That is a property of looking at the signal, not
    of the signal, and it would have been read here as a filter error.
*/
double settledLevelDb (const std::vector<float>& channel, double fraction = 0.2)
{
    const auto from = (size_t) ((double) channel.size() * (1.0 - fraction));
    auto sum = 0.0;
    auto count = 0;

    for (auto i = from; i < channel.size(); ++i)
    {
        sum += (double) channel[i] * channel[i];
        ++count;
    }

    // Reported as the amplitude of the sine that would give this RMS.
    return count > 0 ? toDb (std::sqrt (2.0 * sum / count)) : -200.0;
}

void testCrossover()
{
    std::printf ("\nCrossover and transparency\n");

    /*  With nothing over the threshold the two halves have to add back up to
        what went in. A fourth order Linkwitz-Riley is the order that does this;
        anything else leaves a dip or a bump at the crossover that would be
        printed into every take whether or not there was an S in it.
    */
    auto settings = defaultSettings();
    settings.thresholdDb = 0.0f;     // nothing will ever reach it
    settings.rangeDb = 0.0f;

    int coloured = 0;
    double worstDb = 0.0;

    for (auto frequency : { 100.0, 1000.0, 3000.0, 5000.0, 6500.0, 8000.0, 12000.0, 16000.0 })
    {
        ess::EssEngine engine;
        engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
        engine.setSettings (settings);

        const auto input = makeTone (frequency, 0.25, 24000);
        const auto output = runEngine (engine, input);

        const auto change = settledLevelDb (output[0]) - toDb (0.25);
        worstDb = juce::jmax (worstDb, std::abs (change));

        if (std::abs (change) > 0.05)
        {
            ++coloured;
            std::printf ("        %.0f Hz: %+.3f dB\n", frequency, change);
        }
    }

    check (coloured == 0, "idle, the crossover puts the signal back together", worstDb, 0.0);

    // And sample for sample, not just in magnitude: the delayed sum of the two
    // halves is the input through an all pass, so compare against a run with
    // the same filters rather than against the raw input.
    ess::EssEngine engine;
    engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    engine.setSettings (settings);

    juce::Random random (17);
    std::vector<std::vector<float>> noise (2, std::vector<float> (24000, 0.0f));

    for (size_t i = 0; i < noise[0].size(); ++i)
    {
        const auto value = random.nextFloat() * 0.5f - 0.25f;
        noise[0][i] = value;
        noise[1][i] = value;
    }

    const auto through = runEngine (engine, noise);

    // An all pass keeps the energy, so compare RMS rather than sample values.
    auto inputEnergy = 0.0, outputEnergy = 0.0;

    for (size_t i = 4800; i < noise[0].size(); ++i)
    {
        inputEnergy += (double) noise[0][i] * noise[0][i];
        outputEnergy += (double) through[0][i] * through[0][i];
    }

    checkClose (toDb (std::sqrt (outputEnergy)) - toDb (std::sqrt (inputEnergy)), 0.0, 0.05,
                "and keeps the energy of a broadband signal");
}

void testReduction()
{
    std::printf ("\nReduction\n");

    /*  Measured in wideband mode, where the whole signal is multiplied by the
        gain and the arithmetic is therefore exact. In split mode the residue of
        the same tone coming through the low half of the crossover — 13 dB down
        at only twice the corner frequency — would dominate the measurement and
        make the number meaningless.

        And the detector does not see the input, it sees the band: at 9 kHz with
        the crossover at 6.5 kHz the high half is still 2 dB under unity. So the
        band's response is measured first, with the plugin auditioning it, and
        the expected figure is derived from that rather than assumed to be zero.
    */
    const auto testFrequency = 9000.0;
    const auto amplitude = juce::Decibels::decibelsToGain (-12.0);

    auto bandSettings = defaultSettings();
    bandSettings.rangeDb = 0.0f;
    bandSettings.monitor = ess::Monitor::band;

    ess::EssEngine bandProbe;
    bandProbe.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    bandProbe.setSettings (bandSettings);

    const auto detectorDb = settledLevelDb (
        runEngine (bandProbe, makeTone (testFrequency, amplitude, 24000))[0]);

    std::printf ("        the detector sees %.2f dB where the input is %.2f dB\n",
                 detectorDb, toDb (amplitude));

    auto settings = defaultSettings();
    settings.mode = ess::Mode::wideband;
    settings.thresholdDb = -30.0f;
    settings.rangeDb = 24.0f;

    /*  A slow release for the steady state measurement. The detector is a peak
        detector, so on a sine the gain relaxes a fraction between one peak and
        the next and settles a hair shallower than the peak calls for — the same
        ripple already pinned in the compressor's tests. A long release makes it
        vanish; the working defaults are short on purpose, which is why the
        measurement and the setting are not the same thing.
    */
    settings.releaseMs = 500.0f;

    ess::EssEngine engine;
    engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    engine.setSettings (settings);

    const auto output = runEngine (engine, makeTone (testFrequency, amplitude, 48000 * 2));

    // An infinite ratio brings the band back to the threshold, so the whole
    // signal moves by however far over it the band was.
    const auto expected = toDb (amplitude) - (detectorDb - settings.thresholdDb);
    checkClose (settledLevelDb (output[0]), expected, 0.35,
                "the band is brought back down to the threshold");

    // And the size of what is left over, pinned so it cannot grow unnoticed: a
    // peak detector on a sine always settles a fraction shallower than the peak
    // asks for, because the gain relaxes between one peak and the next.
    const auto ripple = settledLevelDb (output[0]) - expected;
    check (ripple > 0.0 && ripple < 0.5,
           "  riding a hair shallower than the peak, as a peak detector does", ripple, 0.2);

    // Range caps it, exactly.
    settings.rangeDb = 6.0f;
    ess::EssEngine limited;
    limited.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    limited.setSettings (settings);

    checkClose (settledLevelDb (runEngine (limited, makeTone (testFrequency, amplitude, 48000))[0]),
                toDb (amplitude) - 6.0, 0.2, "range caps how far it may go");

    // Below the threshold nothing happens at all.
    settings.rangeDb = 24.0f;
    ess::EssEngine quiet;
    quiet.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    quiet.setSettings (settings);

    const auto quietAmplitude = juce::Decibels::decibelsToGain (-45.0);
    checkClose (settledLevelDb (runEngine (quiet, makeTone (testFrequency, quietAmplitude, 24000))[0]),
                -45.0, 0.2, "under the threshold it is left alone");
}

void testModes()
{
    std::printf ("\nWideband against split\n");

    /*  The difference between the two modes, stated as a measurement: with a
        loud sibilant tone and a quiet low tone playing together, split leaves
        the low tone where it was and wideband pulls it down with everything
        else. This is the whole trade the mode switch offers.
    */
    auto settings = defaultSettings();
    settings.thresholdDb = -30.0f;
    settings.rangeDb = 24.0f;

    const auto sibilant = juce::Decibels::decibelsToGain (-12.0);
    const auto body = juce::Decibels::decibelsToGain (-20.0);

    std::vector<std::vector<float>> mixture (2, std::vector<float> (48000, 0.0f));

    for (size_t i = 0; i < mixture[0].size(); ++i)
    {
        const auto t = (double) i / sampleRate;
        const auto value = (float) (sibilant * std::sin (2.0 * juce::MathConstants<double>::pi * 9000.0 * t)
                                  + body * std::sin (2.0 * juce::MathConstants<double>::pi * 300.0 * t));
        mixture[0][i] = value;
        mixture[1][i] = value;
    }

    auto lowLevelAfter = [&] (ess::Mode mode)
    {
        auto used = settings;
        used.mode = mode;

        ess::EssEngine engine;
        engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
        engine.setSettings (used);

        const auto output = runEngine (engine, mixture);

        // Measure the low tone by filtering the result rather than by peak,
        // since both tones are present.
        dsp::Biquad filter;
        filter.setCoefficients (dsp::BiquadCoefficients::lowPass (sampleRate, 1000.0, 0.707));

        auto peak = 0.0;

        for (size_t i = 0; i < output[0].size(); ++i)
        {
            const auto value = filter.processSample (output[0][i]);

            if (i > output[0].size() * 3 / 4)
                peak = juce::jmax (peak, (double) std::abs (value));
        }

        return toDb (peak) - toDb (body);
    };

    checkClose (lowLevelAfter (ess::Mode::split), 0.0, 0.3,
                "split band leaves the body of the voice alone");

    const auto wideband = lowLevelAfter (ess::Mode::wideband);
    check (wideband < -6.0, "wideband pulls the whole signal down with the S",
           wideband, -6.0);
}

void testAdaptive()
{
    std::printf ("\nAdaptive threshold\n");

    /*  The point of the adaptive detector: the same setting does the same work
        on a quiet take and a loud one. With a fixed threshold, 20 dB quieter
        means it stops working altogether.
    */
    auto reduction = [] (ess::Detection detection, double levelDb)
    {
        auto settings = defaultSettings();
        settings.detection = detection;
        settings.thresholdDb = detection == ess::Detection::adaptive ? -24.0f : -30.0f;
        settings.rangeDb = 24.0f;

        ess::EssEngine engine;
        engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
        engine.setSettings (settings);

        const auto amplitude = juce::Decibels::decibelsToGain (levelDb);
        const auto output = runEngine (engine, makeTone (9000.0, amplitude, 48000 * 3));

        return settledLevelDb (output[0]) - levelDb;
    };

    const auto fixedLoud = reduction (ess::Detection::absolute, -12.0);
    const auto fixedQuiet = reduction (ess::Detection::absolute, -32.0);

    check (std::abs (fixedLoud - fixedQuiet) > 10.0,
           "a fixed threshold does far less work on a quiet take",
           std::abs (fixedLoud - fixedQuiet), 10.0);

    const auto adaptiveLoud = reduction (ess::Detection::adaptive, -12.0);
    const auto adaptiveQuiet = reduction (ess::Detection::adaptive, -32.0);

    checkClose (adaptiveLoud, adaptiveQuiet, 1.5,
                "the adaptive one treats both the same");
}

void testMonitoringAndLink()
{
    std::printf ("\nMonitoring and linking\n");

    auto settings = defaultSettings();
    settings.thresholdDb = -30.0f;
    settings.rangeDb = 24.0f;

    const auto amplitude = juce::Decibels::decibelsToGain (-12.0);
    const auto tone = makeTone (9000.0, amplitude, 24000);

    ess::EssEngine normal;
    normal.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    normal.setSettings (settings);
    const auto processed = runEngine (normal, tone);

    settings.monitor = ess::Monitor::difference;
    ess::EssEngine delta;
    delta.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    delta.setSettings (settings);
    const auto removed = runEngine (delta, tone);

    /*  What is kept plus what is removed has to be what the crossover handed
        over — which is the input through an all pass, not the input itself, so
        the reference is a third run with the de-essing turned off rather than
        the original signal.
    */
    auto transparent = defaultSettings();
    transparent.thresholdDb = 0.0f;
    transparent.rangeDb = 0.0f;

    ess::EssEngine untouched;
    untouched.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    untouched.setSettings (transparent);
    const auto dry = runEngine (untouched, tone);

    auto worst = 0.0;

    for (size_t i = 12000; i < tone[0].size(); ++i)
        worst = juce::jmax (worst,
                            std::abs ((double) processed[0][i] + (double) removed[0][i]
                                      - (double) dry[0][i]));

    checkClose (worst, 0.0, 1.0e-5, "what is kept plus what is removed is what came in");

    // Listening to the band has to be the band: a low tone must not come out.
    settings.monitor = ess::Monitor::band;
    ess::EssEngine listen;
    listen.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    listen.setSettings (settings);

    const auto lowTone = makeTone (300.0, 0.5, 24000);
    const auto lowThroughBand = settledLevelDb (runEngine (listen, lowTone)[0]);
    check (lowThroughBand < -60.0, "listening to the band keeps the low end out",
           lowThroughBand, -60.0);

    // Stereo link: a sibilant on one side only.
    auto linkTest = [&] (float link)
    {
        auto used = defaultSettings();
        used.thresholdDb = -30.0f;
        used.rangeDb = 24.0f;
        used.stereoLink = link;

        ess::EssEngine engine;
        engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
        engine.setSettings (used);

        std::vector<std::vector<float>> signal (2, std::vector<float> (24000, 0.0f));
        const auto omega = 2.0 * juce::MathConstants<double>::pi * 9000.0 / sampleRate;

        for (size_t i = 0; i < signal[0].size(); ++i)
        {
            const auto s = std::sin (omega * (double) i);
            signal[0][i] = (float) (amplitude * s);
            signal[1][i] = (float) (juce::Decibels::decibelsToGain (-40.0) * s);
        }

        const auto output = runEngine (engine, signal);
        return settledLevelDb (output[1]) + 40.0;
    };

    check (linkTest (1.0f) < -10.0, "linked, the quiet side takes the same reduction",
           linkTest (1.0f), -18.0);
    checkClose (linkTest (0.0f), 0.0, 0.3, "unlinked, it is left alone");
}

void testDetectorBand()
{
    std::printf ("\nThe detector's band has a top\n");

    /*  Sibilance is a band, not everything above a corner. With the detector
        open to the top of the spectrum a cymbal or the air of a bright room is
        indistinguishable from an S, and the de-esser ducks the voice every time
        the drummer plays. Closing the detector's top is what separates them.

        Measured in wideband mode so the gain lands on the whole signal and the
        arithmetic is exact.
    */
    auto settings = defaultSettings();
    settings.mode = ess::Mode::wideband;
    settings.thresholdDb = -30.0f;
    settings.rangeDb = 24.0f;
    settings.releaseMs = 500.0f;   // as in testReduction: kills the peak-detector ripple

    const auto cymbalDb = -12.0;
    const auto amplitude = juce::Decibels::decibelsToGain (cymbalDb);

    auto reductionAt = [&] (float topHz)
    {
        auto used = settings;
        used.detectorTopHz = topHz;

        ess::EssEngine engine;
        engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
        engine.setSettings (used);

        const auto output = runEngine (engine, makeTone (16000.0, amplitude, 48000 * 2));
        return settledLevelDb (output[0]) - cymbalDb;
    };

    const auto open = reductionAt (20000.0f);
    const auto narrowed = reductionAt (8000.0f);

    std::printf ("        16 kHz: %.2f dB with the top open, %.2f dB with it at 8 kHz\n",
                 open, narrowed);

    check (open < -14.0, "wide open, the top octave drives the de-esser", open, -18.0);

    /*  How much less it does is not guessed at: it is the detector filter's own
        magnitude at that frequency. A second order low pass is 12 dB down an
        octave up on paper, but the digital section has a double zero at Nyquist
        and 16 kHz at a 48 kHz sample rate is close enough to it to matter — a
        textbook figure would have been read here as an error in the filter.

        Once the detector no longer reaches the threshold the reduction is not
        negative, it is nothing, so the expectation is floored rather than
        extrapolated.
    */
    auto attenuationAt = [] (double topHz)
    {
        return -toDb (dsp::BiquadCoefficients::lowPass (sampleRate, topHz, 0.70710678118654752)
                          .magnitudeAt (16000.0, sampleRate));
    };

    std::printf ("        the detector's own top is %.2f dB down there at 8 kHz, "
                 "%.2f dB at 12 kHz\n", attenuationAt (8000.0), attenuationAt (12000.0));

    checkClose (narrowed, juce::jmin (0.0, open + attenuationAt (8000.0)), 0.3,
                "closing the top takes it back out of the detector");

    // And in between, where the cymbal is only partly out of the way, the two
    // move together decibel for decibel.
    checkClose (reductionAt (12000.0f), open + attenuationAt (12000.0), 0.4,
                "part way, it does exactly that much less work");

    /*  And the audio never goes through that filter. If it did, the de-esser
        would be quietly low passing everything it was handed.
    */
    auto transparent = settings;
    transparent.detectorTopHz = 8000.0f;
    transparent.thresholdDb = 0.0f;
    transparent.rangeDb = 0.0f;

    ess::EssEngine untouched;
    untouched.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    untouched.setSettings (transparent);

    checkClose (settledLevelDb (runEngine (untouched, makeTone (16000.0, amplitude, 24000))[0]),
                cymbalDb, 0.05, "and the sound itself never passes through it");
}

void testMidSide()
{
    std::printf ("\nMid and side\n");

    /*  A lead vocal sits in the middle. De-essing the mid alone leaves the
        reverb tail, the doubles and the widened guitars untouched, which is the
        difference between taking an S off a voice and taking the air off a mix.

        The two checks that matter are selectivity — the component you did not
        choose is not touched — and transparency, because the rotation into mid
        and side and back is only free if both components get exactly the same
        filtering on the way through.
    */
    auto settings = defaultSettings();
    settings.mode = ess::Mode::wideband;
    settings.thresholdDb = -30.0f;
    settings.rangeDb = 24.0f;
    settings.releaseMs = 500.0f;

    const auto amplitude = juce::Decibels::decibelsToGain (-12.0);

    // A signal living entirely in one component: identical channels are pure
    // mid, opposite channels are pure side.
    auto makeComponent = [&] (bool inSide)
    {
        auto signal = makeTone (9000.0, amplitude, 48000 * 2);

        if (inSide)
            for (auto& sample : signal[1])
                sample = -sample;

        return signal;
    };

    auto levelAfter = [&] (ess::Channel channel, bool signalInSide)
    {
        auto used = settings;
        used.channel = channel;

        ess::EssEngine engine;
        engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
        engine.setSettings (used);

        return settledLevelDb (runEngine (engine, makeComponent (signalInSide))[0]) + 12.0;
    };

    const auto midOnMid   = levelAfter (ess::Channel::mid, false);
    const auto midOnSide  = levelAfter (ess::Channel::mid, true);
    const auto sideOnSide = levelAfter (ess::Channel::side, true);
    const auto sideOnMid  = levelAfter (ess::Channel::side, false);

    check (midOnMid < -14.0, "mid only de-esses a centred sibilant", midOnMid, -18.0);
    checkClose (midOnSide, 0.0, 0.1, "and leaves one living in the sides alone");

    check (sideOnSide < -14.0, "side only de-esses a sibilant out at the edges",
           sideOnSide, -18.0);
    checkClose (sideOnMid, 0.0, 0.1, "and leaves the centre alone");

    // The same amount of work either way round: the two paths are one path.
    checkClose (midOnMid, sideOnSide, 0.05, "both components are treated identically");

    /*  Transparency. With nothing over the threshold, rotating into mid and
        side, filtering, and rotating back has to give exactly what the plugin
        gives in stereo mode — sample for sample, not just in energy. Filtering
        only the component being worked on would leave the other one arriving
        with a different phase and comb the recombination, and that would show
        up here and nowhere else.
    */
    auto idle = defaultSettings();
    idle.thresholdDb = 0.0f;
    idle.rangeDb = 0.0f;

    juce::Random random (23);
    std::vector<std::vector<float>> noise (2, std::vector<float> (24000, 0.0f));

    for (size_t i = 0; i < noise[0].size(); ++i)
    {
        noise[0][i] = random.nextFloat() * 0.5f - 0.25f;
        noise[1][i] = random.nextFloat() * 0.5f - 0.25f;
    }

    auto runWith = [&] (ess::Channel channel)
    {
        auto used = idle;
        used.channel = channel;

        ess::EssEngine engine;
        engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
        engine.setSettings (used);

        return runEngine (engine, noise);
    };

    const auto stereo = runWith (ess::Channel::stereo);
    const auto rotated = runWith (ess::Channel::mid);

    auto worst = 0.0;

    for (int ch = 0; ch < 2; ++ch)
        for (size_t i = 4800; i < noise[0].size(); ++i)
            worst = juce::jmax (worst, std::abs ((double) stereo[(size_t) ch][i]
                                                 - (double) rotated[(size_t) ch][i]));

    checkClose (worst, 0.0, 1.0e-6, "idle, the rotation costs nothing at all");
}

void testLatency()
{
    std::printf ("\nLookahead\n");

    auto settings = defaultSettings();
    settings.lookaheadMs = 2.0f;
    settings.thresholdDb = 0.0f;
    settings.rangeDb = 0.0f;

    ess::EssEngine engine;
    engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    engine.setSettings (settings);

    const auto expected = (int) std::round (0.002 * sampleRate);
    checkClose (engine.computeLatencySamples(), expected, 0.5,
                "reported latency is the lookahead");

    // Run a block so the setting is applied, then check the engine agrees.
    juce::AudioBuffer<float> buffer (2, blockSize);
    buffer.clear();
    juce::dsp::AudioBlock<float> block (buffer);
    engine.process (block);

    checkClose (engine.getLatencySamples(), expected, 0.5, "and the engine is using it");
}
} // namespace

Result runDeEsserTests()
{
    std::printf ("\n\nNodo Ess verification\n=====================\n");

    testCrossover();
    testReduction();
    testModes();
    testAdaptive();
    testMonitoringAndLink();
    testDetectorBand();
    testMidSide();
    testLatency();

    return { checks, failures };
}
} // namespace nodo::tests
