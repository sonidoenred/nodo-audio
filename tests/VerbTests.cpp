/*
    Verification for the reverb.

    My own note on this project said the reverb would be the last one because
    its quality can only be judged by ear, and that is still true of whether it
    sounds good. It is not true of everything.

    What can be measured, and what this file measures, is the decay time: the
    plugin draws a curve of seconds against frequency, and a decay time is a
    slope in decibels per second that can be fitted out of the output. If the
    two agree, then the one thing the interface promises is a thing the plugin
    does. That does not make it a good reverb. It makes it an honest one, and
    the rest is Pau's job.

    The method is the standard T30: measure the level decaying, fit a straight
    line between 5 and 35 dB below the start, extrapolate to 60. Starting at
    -5 dB avoids the first few milliseconds, where the direct sound and the
    early part of the build-up have not settled into an exponential yet.
*/

#include "VerbTests.h"

#include <nodo_core/nodo_core.h>
#include "../plugins/nodo_verb/source/VerbEngine.h"
#include "../plugins/nodo_verb/source/FactoryPresets.h"

#include <algorithm>
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
        std::printf ("  ok    %-56s  %8.3f\n", description.toRawUTF8(), actual);
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

using Signal = std::vector<std::vector<float>>;

verb::VerbSettings defaultSettings()
{
    verb::VerbSettings s;
    s.mix = 1.0f;                  // pure wet: the output is the reverb
    s.predelayMs = 0.0f;
    s.size = 1.0f;
    s.decay.midSeconds = 2.0f;
    s.decay.lowMultiplier = 1.0f;
    s.decay.lowFrequency = 250.0f;
    s.decay.highMultiplier = 1.0f;
    s.decay.highFrequency = 4000.0f;
    s.diffusion = 0.7f;
    s.motion = 0.0f;               // still, so a measurement is repeatable
    s.width = 1.0f;
    s.freeze = false;
    s.preLowCutHz = 20.0f;         // nothing filtered: the decay is the subject
    s.preHighCutHz = 20000.0f;
    s.postLowGain = 0.0f;
    s.postMidGain = 0.0f;
    s.postHighGain = 0.0f;
    s.duckAmount = 0.0f;
    return s;
}

Signal makeSilence (int samples)
{
    return Signal (2, std::vector<float> ((size_t) samples, 0.0f));
}

Signal runEngine (verb::VerbEngine& engine, const Signal& input)
{
    const auto total = (int) input[0].size();
    Signal output (2, std::vector<float> ((size_t) total, 0.0f));

    juce::AudioBuffer<float> buffer (2, blockSize);

    for (int position = 0; position < total; position += blockSize)
    {
        const auto thisBlock = juce::jmin (blockSize, total - position);
        buffer.setSize (2, thisBlock, false, false, true);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < thisBlock; ++i)
                buffer.setSample (ch, i, input[(size_t) ch][(size_t) (position + i)]);

        juce::dsp::AudioBlock<float> block (buffer);
        engine.process (block);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < thisBlock; ++i)
                output[(size_t) ch][(size_t) (position + i)] = buffer.getSample (ch, i);
    }

    return output;
}

/** A burst of noise, which excites every line of the network far more evenly
    than a single impulse does and makes the decay easier to fit.
*/
Signal makeBurst (int totalSamples, int burstSamples, int seed = 3, float amplitude = 0.3f)
{
    auto signal = makeSilence (totalSamples);
    juce::Random random (seed);

    for (int i = 0; i < juce::jmin (burstSamples, totalSamples); ++i)
    {
        signal[0][(size_t) i] = (random.nextFloat() * 2.0f - 1.0f) * amplitude;
        signal[1][(size_t) i] = (random.nextFloat() * 2.0f - 1.0f) * amplitude;
    }

    return signal;
}

/** Fourth order band pass, as two cascaded biquads. One is too gentle to keep a
    neighbouring band's decay out of the measurement.
*/
std::vector<float> bandPass (const std::vector<float>& channel, double centreHz)
{
    dsp::Biquad a, b;
    const auto coefficients = dsp::BiquadCoefficients::bandPass (sampleRate, centreHz, 1.4);
    a.setCoefficients (coefficients);
    b.setCoefficients (coefficients);

    std::vector<float> filtered (channel.size(), 0.0f);

    for (size_t i = 0; i < channel.size(); ++i)
        filtered[i] = b.processSample (a.processSample (channel[i]));

    return filtered;
}

/** T30: fit the decay between 5 and 35 dB below the start and extrapolate to
    60. Returns 0 if the tail never falls far enough to fit.
*/
double measureT60 (const std::vector<float>& channel, double centreHz, int from)
{
    const auto filtered = bandPass (channel, centreHz);

    // Windowed energy, in dB, on a 20 ms grid.
    const auto window = (int) (0.02 * sampleRate);
    std::vector<double> levels;
    std::vector<double> times;

    for (int start = from; start + window < (int) filtered.size(); start += window)
    {
        auto sum = 0.0;

        for (int i = start; i < start + window; ++i)
            sum += (double) filtered[(size_t) i] * filtered[(size_t) i];

        levels.push_back (10.0 * std::log10 (juce::jmax (1.0e-20, sum / window)));
        times.push_back ((double) (start - from) / sampleRate);
    }

    if (levels.size() < 8)
        return 0.0;

    const auto peak = *std::max_element (levels.begin(), levels.end());

    // A least squares fit over the window between -5 and -35 dB.
    auto n = 0.0, sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;

    for (size_t i = 0; i < levels.size(); ++i)
    {
        const auto relative = levels[i] - peak;

        if (relative > -5.0 || relative < -35.0)
            continue;

        n += 1.0;
        sx += times[i];
        sy += levels[i];
        sxx += times[i] * times[i];
        sxy += times[i] * levels[i];
    }

    if (n < 4.0)
        return 0.0;

    const auto denominator = n * sxx - sx * sx;

    if (std::abs (denominator) < 1.0e-12)
        return 0.0;

    const auto slope = (n * sxy - sx * sy) / denominator;   // dB per second

    return slope < -0.5 ? -60.0 / slope : 0.0;
}

void testDecayCurveDesign()
{
    std::printf ("\nThe decay curve, before any audio\n");

    /*  The design first. Every line's filter is supposed to attenuate by
        -60 L / (T fs) decibels per pass, where T is the decay time at that
        frequency. If that is wrong then the measurements below are measuring
        the wrong thing accurately.
    */
    verb::DecayCurve curve;
    curve.midSeconds = 2.0f;
    curve.lowMultiplier = 2.0f;
    curve.lowFrequency = 200.0f;
    curve.highMultiplier = 0.5f;
    curve.highFrequency = 5000.0f;

    checkClose (curve.secondsAt (1000.0f, sampleRate), 2.0, 0.15,
                "in the middle the curve is the decay knob");

    checkClose (curve.secondsAt (25.0f, sampleRate), 4.0, 0.2,
                "at the bottom it is the knob times the low multiplier");

    checkClose (curve.secondsAt (18000.0f, sampleRate), 1.0, 0.06,
                "and at the top times the high one");

    // The per-line filter has to agree with the curve it was designed from.
    const auto length = 1493;
    const auto damping = verb::designLineDamping (curve, length, sampleRate);

    for (auto hz : { 40.0f, 300.0f, 1000.0f, 3000.0f, 12000.0f })
    {
        const auto perPassDb = damping.responseDb (hz, sampleRate);
        const auto impliedT60 = -60.0 * length / (perPassDb * sampleRate);
        const auto drawn = curve.secondsAt (hz, sampleRate);

        if (std::abs (impliedT60 - drawn) > drawn * 0.06)
        {
            check (false, "the filter agrees with the curve at every frequency",
                   impliedT60, drawn);
            return;
        }
    }

    check (true, "the filter agrees with the curve at every frequency", 1.0, 1.0);

    // And every line, however long, ends up implying the same decay time —
    // which is the point of dividing by the length in the first place.
    auto worstSpread = 0.0;

    for (int i = 0; i < verb::numLines; ++i)
    {
        const auto lines = verb::lineLengthSamples (i, 1.0f, sampleRate);
        const auto d = verb::designLineDamping (curve, lines, sampleRate);
        const auto implied = -60.0 * lines / (d.responseDb (1000.0f, sampleRate) * sampleRate);

        worstSpread = juce::jmax (worstSpread, std::abs (implied - 2.0));
    }

    checkClose (worstSpread, 0.0, 0.02, "and all eight lines imply the same decay time");
}

void testFlatDecay()
{
    std::printf ("\nThe decay time, measured out of the audio\n");

    for (auto wanted : { 0.8f, 2.0f, 5.0f })
    {
        auto settings = defaultSettings();
        settings.decay.midSeconds = wanted;

        verb::VerbEngine engine;
        engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
        engine.setSettings (settings);

        const auto total = (int) (sampleRate * (double) wanted * 2.5);
        const auto output = runEngine (engine, makeBurst (total, 2400));

        const auto measured = measureT60 (output[0], 1000.0, 4800);

        std::printf ("        asked for %.2f s, measured %.2f s\n", wanted, measured);

        if (std::abs (measured - (double) wanted) > (double) wanted * 0.12)
        {
            check (false, "the measured decay is the decay that was asked for",
                   measured, (double) wanted);
            return;
        }
    }

    check (true, "the measured decay is the decay that was asked for", 1.0, 1.0);
}

void testDecayPerBand()
{
    std::printf ("\nA different decay time at each end\n");

    /*  The whole reason this plugin is built the way it is. The bottom is asked
        to hang on twice as long as the middle and the top to disappear twice as
        fast, and all three are measured separately out of one tail.
    */
    auto settings = defaultSettings();
    settings.decay.midSeconds = 2.0f;
    settings.decay.lowMultiplier = 2.0f;
    settings.decay.lowFrequency = 200.0f;
    settings.decay.highMultiplier = 0.5f;
    settings.decay.highFrequency = 5000.0f;

    verb::VerbEngine engine;
    engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    engine.setSettings (settings);

    const auto output = runEngine (engine, makeBurst ((int) (sampleRate * 12.0), 4800));

    struct Band { double hz; const char* name; };
    const Band bands[3] { { 80.0, "80 Hz" }, { 1000.0, "1 kHz" }, { 9000.0, "9 kHz" } };

    auto worstError = 0.0;

    for (const auto& band : bands)
    {
        const auto measured = measureT60 (output[0], band.hz, 9600);
        const auto drawn = (double) settings.decay.secondsAt ((float) band.hz, sampleRate);
        const auto error = std::abs (measured - drawn) / juce::jmax (0.05, drawn);

        std::printf ("        %-6s  drawn %.2f s, measured %.2f s  (%+.1f %%)\n",
                     band.name, drawn, measured, 100.0 * (measured - drawn) / juce::jmax (0.05, drawn));

        worstError = juce::jmax (worstError, error);
    }

    check (worstError < 0.15, "every band decays at the time the display draws",
           worstError * 100.0, 15.0);

    // And they are genuinely different, not three measurements of one number.
    const auto low = measureT60 (output[0], 80.0, 9600);
    const auto high = measureT60 (output[0], 9000.0, 9600);

    check (low > high * 2.5, "the bottom really does outlast the top", low / juce::jmax (0.01, high), 2.5);
}

void testStability()
{
    std::printf ("\nStability\n");

    /*  A feedback delay network with an orthogonal matrix and a per-line gain
        below one is provably stable, but the design is what decides the gain
        and the design is code. A sweep over the parameter space is the cheap
        way to find out whether any corner of it produces a gain above one.
    */
    juce::Random random (17);
    auto worstPeak = 0.0;
    auto allFinite = true;
    auto allDecay = true;

    for (int trial = 0; trial < 40; ++trial)
    {
        auto settings = defaultSettings();

        settings.decay.midSeconds = 0.1f + random.nextFloat() * 19.9f;
        settings.decay.lowMultiplier = 0.12f + random.nextFloat() * 1.88f;
        settings.decay.highMultiplier = 0.12f + random.nextFloat() * 1.88f;
        settings.decay.lowFrequency = 40.0f + random.nextFloat() * 1400.0f;
        settings.decay.highFrequency = 800.0f + random.nextFloat() * 15000.0f;
        settings.size = 0.25f + random.nextFloat() * 2.75f;
        settings.diffusion = random.nextFloat();
        settings.motion = random.nextFloat();

        verb::VerbEngine engine;
        engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
        engine.setSettings (settings);

        const auto total = (int) (sampleRate * 4.0);
        const auto output = runEngine (engine, makeBurst (total, 4800, trial + 1));

        auto peak = 0.0, tailEnergy = 0.0, midEnergy = 0.0;

        for (int ch = 0; ch < 2; ++ch)
            for (size_t i = 0; i < output[0].size(); ++i)
            {
                const auto value = (double) output[(size_t) ch][i];

                if (! std::isfinite (value))
                    allFinite = false;

                peak = juce::jmax (peak, std::abs (value));

                if (i > output[0].size() - 4800)
                    tailEnergy += value * value;
                else if (i > 24000 && i < 28800)
                    midEnergy += value * value;
            }

        worstPeak = juce::jmax (worstPeak, peak);

        // The end of a four second run must be quieter than the middle of it,
        // whatever the decay time: a network that is growing is a broken one.
        if (tailEnergy > midEnergy)
            allDecay = false;
    }

    check (allFinite, "forty random settings, nothing non-finite", 1.0, 1.0);
    check (allDecay, "and every one of them is decaying, not growing", 1.0, 1.0);
    check (worstPeak < 40.0, "and none of them builds up unreasonably", worstPeak, 40.0);
}

void testPredelayAndFreeze()
{
    std::printf ("\nPredelay and freeze\n");

    // Predelay: nothing at all comes out before it.
    auto settings = defaultSettings();
    settings.predelayMs = 50.0f;
    settings.diffusion = 0.0f;

    verb::VerbEngine engine;
    engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    engine.setSettings (settings);

    auto impulse = makeSilence (48000);
    impulse[0][0] = 0.5f;
    impulse[1][0] = 0.5f;

    const auto output = runEngine (engine, impulse);
    const auto predelaySamples = (int) (0.050 * sampleRate);

    auto before = 0.0;

    for (int i = 0; i < predelaySamples - 4; ++i)
        before = juce::jmax (before, (double) std::abs (output[0][(size_t) i]));

    checkClose (before, 0.0, 1.0e-7, "nothing comes out before the predelay is up");

    auto after = 0.0;

    for (int i = predelaySamples; i < predelaySamples + 4800; ++i)
        after = juce::jmax (after, (double) std::abs (output[0][(size_t) i]));

    check (after > 0.01, "and it does after", after, 0.01);

    // ---- freeze ------------------------------------------------------------
    /*  Every line hands back exactly what it was given, so a frozen tail has to
        hold for ever — not nearly for ever. Measured as the level five seconds
        in against the level fifteen seconds in.
    */
    constexpr int total = (int) (48000 * 20);
    constexpr int freezeAt = 48000 * 2;

    auto frozen = defaultSettings();
    frozen.decay.midSeconds = 3.0f;

    verb::VerbEngine holder;
    holder.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    holder.setSettings (frozen);

    auto material = makeBurst (total, 24000);
    Signal held (2, std::vector<float> ((size_t) total, 0.0f));
    juce::AudioBuffer<float> buffer (2, blockSize);

    for (int position = 0; position < total; position += blockSize)
    {
        const auto thisBlock = juce::jmin (blockSize, total - position);

        if (position >= freezeAt && ! frozen.freeze)
        {
            frozen.freeze = true;
            holder.setSettings (frozen);
        }

        buffer.setSize (2, thisBlock, false, false, true);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < thisBlock; ++i)
                buffer.setSample (ch, i, material[(size_t) ch][(size_t) (position + i)]);

        juce::dsp::AudioBlock<float> block (buffer);
        holder.process (block);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < thisBlock; ++i)
                held[(size_t) ch][(size_t) (position + i)] = buffer.getSample (ch, i);
    }

    auto rms = [&held] (int from, int count)
    {
        auto sum = 0.0;

        for (int i = from; i < from + count; ++i)
            sum += (double) held[0][(size_t) i] * held[0][(size_t) i];

        return std::sqrt (sum / count);
    };

    const auto early = rms (48000 * 5, 48000);
    const auto late  = rms (48000 * 18, 48000);

    check (early > 1.0e-4, "a frozen tail is still there", early, 1.0e-4);

    const auto drift = 20.0 * std::log10 (juce::jmax (1.0e-12, late / juce::jmax (1.0e-12, early)));
    std::printf ("        thirteen seconds later it has moved %+.2f dB\n", drift);

    checkClose (drift, 0.0, 0.5, "and thirteen seconds later it is the same level");
}

void testDuckingAndMix()
{
    std::printf ("\nDucking, mix and width\n");

    // Mix at zero is a wire.
    auto settings = defaultSettings();
    settings.mix = 0.0f;

    verb::VerbEngine engine;
    engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    engine.setSettings (settings);

    const auto material = makeBurst (24000, 24000);
    const auto through = runEngine (engine, material);

    auto worst = 0.0;

    for (int ch = 0; ch < 2; ++ch)
        for (size_t i = 0; i < material[0].size(); ++i)
            worst = juce::jmax (worst, std::abs ((double) through[(size_t) ch][i]
                                                 - (double) material[(size_t) ch][i]));

    checkClose (worst, 0.0, 1.0e-6, "at zero mix it is a wire");

    /*  Ducking: a burst, then silence. While the burst is playing the tail is
        pushed down; once it stops the tail comes back. Measured as the level
        just after the burst against a run with no ducking at all.
    */
    auto ducked = defaultSettings();
    ducked.decay.midSeconds = 3.0f;
    ducked.duckAmount = 0.9f;
    ducked.duckAttackMs = 5.0f;
    ducked.duckReleaseMs = 400.0f;

    auto plain = ducked;
    plain.duckAmount = 0.0f;

    /*  Loud on purpose. The ducking depth is not the amount alone: the amount
        scales a detector that reads 1 only at or above -6 dBFS, so a quiet
        source ducks less. The first version of this test fed the same -10 dBFS
        burst as the decay tests and expected the full depth; it measured
        -11.8 dB instead of -20 and the expectation was the thing that was
        wrong. Fed a source that reaches the top of the detector's range, the
        depth is exactly what the amount promises, which is the number worth
        checking.
    */
    auto run = [] (const verb::VerbSettings& s)
    {
        verb::VerbEngine e;
        e.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
        e.setSettings (s);
        return runEngine (e, makeBurst (48000 * 4, 48000, 3, 1.0f));
    };

    const auto withDuck = run (ducked);
    const auto withoutDuck = run (plain);

    auto rms = [] (const Signal& signal, int from, int count)
    {
        auto sum = 0.0;

        for (int i = from; i < from + count; ++i)
            sum += (double) signal[0][(size_t) i] * signal[0][(size_t) i];

        return std::sqrt (sum / count);
    };

    // During the burst, the ducked one is much quieter.
    const auto duringDucked = rms (withDuck, 24000, 12000);
    const auto duringPlain = rms (withoutDuck, 24000, 12000);
    const auto duringDb = 20.0 * std::log10 (juce::jmax (1.0e-9, duringDucked / duringPlain));

    // 1 - amount is the gain the tail is held at while the detector is full on.
    const auto promisedDb = 20.0 * std::log10 (1.0 - (double) ducked.duckAmount);

    checkClose (duringDb, promisedDb, 1.0, "while the source plays, the tail is pushed down by the amount asked for");

    // Well after it, they have converged again.
    const auto afterDucked = rms (withDuck, 48000 * 3, 24000);
    const auto afterPlain = rms (withoutDuck, 48000 * 3, 24000);
    const auto afterDb = 20.0 * std::log10 (juce::jmax (1.0e-9, afterDucked / afterPlain));

    std::printf ("        during the source %+.1f dB, well after it %+.1f dB\n", duringDb, afterDb);

    checkClose (afterDb, 0.0, 1.0, "and in the gaps it comes back");

    // Width at zero makes the wet mono.
    auto narrow = defaultSettings();
    narrow.width = 0.0f;

    verb::VerbEngine mono;
    mono.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    mono.setSettings (narrow);

    const auto collapsed = runEngine (mono, makeBurst (48000, 4800));

    auto widest = 0.0;

    for (size_t i = 0; i < collapsed[0].size(); ++i)
        widest = juce::jmax (widest, std::abs ((double) collapsed[0][i] - (double) collapsed[1][i]));

    checkClose (widest, 0.0, 1.0e-6, "width at zero collapses the wet to mono");

    // And at one the two sides are genuinely different, or it is a mono reverb
    // with extra steps.
    verb::VerbEngine wide;
    wide.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    wide.setSettings (defaultSettings());

    const auto stereo = runEngine (wide, makeBurst (48000, 4800));

    auto correlation = 0.0, energyL = 0.0, energyR = 0.0;

    for (size_t i = 9600; i < stereo[0].size(); ++i)
    {
        correlation += (double) stereo[0][i] * stereo[1][i];
        energyL += (double) stereo[0][i] * stereo[0][i];
        energyR += (double) stereo[1][i] * stereo[1][i];
    }

    const auto normalised = correlation / std::sqrt (juce::jmax (1.0e-12, energyL * energyR));

    std::printf ("        the two sides correlate %.3f\n", normalised);

    check (std::abs (normalised) < 0.35, "and the two sides are not the same signal",
           std::abs (normalised), 0.35);
}

void testFactoryPresets()
{
    std::printf ("\nFactory presets\n");

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
    juce::AudioProcessorValueTreeState state (holder, nullptr, "NodoVerb",
                                              verb::createParameterLayout());

    const auto presets = verb::buildFactoryPresets();

    check (presets.size() >= 8, "there are enough of them to be useful",
           (double) presets.size(), 8.0);

    auto unknownIds = 0, outOfRange = 0, missingText = 0;

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

            const auto normalised = parameter->convertTo0to1 (value);

            if (normalised < -1.0e-4f || normalised > 1.0f + 1.0e-4f)
            {
                ++outOfRange;
                std::printf ("        out of range: %s = %.3f\n", id.toRawUTF8(), value);
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

Result runVerbTests()
{
    std::printf ("\n\nNodo Verb verification\n======================\n");

    testDecayCurveDesign();
    testFlatDecay();
    testDecayPerBand();
    testStability();
    testPredelayAndFreeze();
    testDuckingAndMix();
    testFactoryPresets();

    return { checks, failures };
}
} // namespace nodo::tests
