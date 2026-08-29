/*
    Verification for the delay.

    The spine of this file is one idea: the picture the plugin draws of where
    every repeat lands is computed by buildTapPattern(), and the audio is
    produced by an entirely separate piece of code. If an impulse through the
    engine does not land exactly where the drawing says it will, at exactly the
    level the drawing says, then one of the two is lying and it does not much
    matter which.
*/

#include "DelayTests.h"

#include <nodo_core/nodo_core.h>
#include "../plugins/nodo_delay/source/DelayEngine.h"
#include "../plugins/nodo_delay/source/TapPattern.h"
#include "../plugins/nodo_delay/source/FactoryPresets.h"

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

using Signal = std::vector<std::vector<float>>;

delay::DelaySettings defaultSettings()
{
    delay::DelaySettings s;
    s.timeLeftMs = 100.0f;
    s.timeRightMs = 150.0f;
    s.sync = false;
    s.linkTimes = false;
    s.feedback = 0.0f;
    s.cross = 0.0f;
    s.invertFeedback = false;
    s.freeze = false;
    s.highpassHz = 20.0f;
    s.lowpassHz = 20000.0f;
    s.drive = 0.0f;
    s.loFi = 0.0f;
    s.modRateHz = 0.5f;
    s.modDepth = 0.0f;
    s.routing = delay::Routing::stereo;
    s.timeMode = delay::TimeMode::fade;
    s.panLeft = -1.0f;
    s.panRight = 1.0f;
    s.width = 1.0f;
    s.mix = 1.0f;                 // pure wet, so the output *is* the repeats
    s.bpm = 120.0;
    return s;
}

Signal makeSilence (int samples)
{
    return Signal (2, std::vector<float> ((size_t) samples, 0.0f));
}

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

Signal runEngine (delay::DelayEngine& engine, const Signal& input)
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

/** Every local maximum of |x| above a floor, as (index, signed value). */
std::vector<std::pair<int, float>> findImpulses (const std::vector<float>& channel, float floorLevel)
{
    std::vector<std::pair<int, float>> found;

    for (size_t i = 1; i + 1 < channel.size(); ++i)
    {
        const auto magnitude = std::abs (channel[i]);

        if (magnitude > floorLevel
            && magnitude >= std::abs (channel[i - 1])
            && magnitude > std::abs (channel[i + 1]))
            found.emplace_back ((int) i, channel[i]);
    }

    return found;
}

/** Magnitude of one frequency in a window, by Goertzel. Cheaper than an FFT
    and exact at the frequency asked for, which is what these tests want.
*/
double goertzel (const std::vector<float>& channel, int from, int count, double frequency)
{
    if (from < 0 || from + count > (int) channel.size() || count <= 0)
        return 0.0;

    const auto omega = 2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;
    const auto coeff = 2.0 * std::cos (omega);

    auto s1 = 0.0, s2 = 0.0;

    for (int i = 0; i < count; ++i)
    {
        const auto s0 = (double) channel[(size_t) (from + i)] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }

    const auto real = s1 - s2 * std::cos (omega);
    const auto imaginary = s2 * std::sin (omega);

    return 2.0 * std::sqrt (real * real + imaginary * imaginary) / (double) count;
}

/** Frequency estimated from zero crossings. Crude, and exactly right for the
    job here: telling whether a steady tone has been shifted in pitch.
*/
double crossingFrequency (const std::vector<float>& channel, int from, int count)
{
    auto crossings = 0;

    for (int i = from + 1; i < from + count && i < (int) channel.size(); ++i)
        if ((channel[(size_t) i] >= 0.0f) != (channel[(size_t) (i - 1)] >= 0.0f))
            ++crossings;

    return (double) crossings * sampleRate / (2.0 * (double) count);
}

/** Frequency of a steady periodic signal, measured between its first and last
    zero crossing rather than by counting them over a fixed window.

    Counting misses whichever crossing lands past the end of the window, which
    is a bias of up to one crossing however long the window is — over four
    seconds of a 4 Hz sine it is still three percent, and it looks exactly like
    an oscillator running slightly slow.
*/
double steadyFrequency (const std::vector<float>& channel)
{
    auto first = -1, last = -1, intervals = 0;

    for (size_t i = 1; i < channel.size(); ++i)
    {
        if ((channel[i] >= 0.0f) == (channel[i - 1] >= 0.0f))
            continue;

        if (first < 0)
            first = (int) i;
        else
            ++intervals;

        last = (int) i;
    }

    if (intervals < 1 || last <= first)
        return 0.0;

    return (double) intervals * sampleRate / (2.0 * (double) (last - first));
}

void testInterpolation()
{
    std::printf ("\nThe delay line itself\n");

    dsp::FractionalDelay line;
    line.prepare (2048);

    // A whole number of samples has to be exact, or nothing built on top can be.
    std::vector<float> source (1024, 0.0f);
    juce::Random random (11);

    for (auto& value : source)
        value = random.nextFloat() * 2.0f - 1.0f;

    auto worstInteger = 0.0;

    for (size_t i = 0; i < source.size(); ++i)
    {
        line.write (source[i]);
        const auto delayed = line.read (100.0f);
        line.advance();

        if (i >= 100)
            worstInteger = juce::jmax (worstInteger,
                                       std::abs ((double) delayed - (double) source[i - 100]));
    }

    checkClose (worstInteger, 0.0, 1.0e-7, "a whole number of samples is exact");

    /*  And a fractional one is close. Catmull-Rom rather than linear because a
        linear interpolator is a low pass whose corner depends on the fractional
        part, so a modulated delay built on it gets audibly duller and brighter
        as the wobble goes past .5.
    */
    dsp::FractionalDelay smooth;
    smooth.prepare (2048);

    const auto frequency = 1000.0;
    const auto omega = 2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;
    const auto delaySamples = 100.5f;

    auto worstFraction = 0.0;

    for (int i = 0; i < 2000; ++i)
    {
        smooth.write ((float) std::sin (omega * i));
        const auto delayed = smooth.read (delaySamples);
        smooth.advance();

        if (i >= 400)
        {
            const auto expected = std::sin (omega * ((double) i - delaySamples));
            worstFraction = juce::jmax (worstFraction, std::abs ((double) delayed - expected));
        }
    }

    check (worstFraction < 0.002, "and a fractional one is within a fifth of a percent",
           worstFraction, 0.002);

    // Constants have to survive interpolation exactly, or a modulated delay
    // would ripple on any signal with a DC component.
    dsp::FractionalDelay flat;
    flat.prepare (512);

    for (int i = 0; i < 400; ++i)
    {
        flat.write (0.5f);
        flat.advance();
    }

    auto worstDc = 0.0;

    for (auto d = 50.0f; d < 60.0f; d += 0.13f)
        worstDc = juce::jmax (worstDc, std::abs ((double) flat.read (d) - 0.5));

    checkClose (worstDc, 0.0, 1.0e-6, "and a constant comes back as a constant");
}

void testTiming()
{
    std::printf ("\nWhere the repeats land\n");

    auto settings = defaultSettings();
    settings.timeLeftMs = 100.0f;
    settings.timeRightMs = 150.0f;
    settings.feedback = 0.0f;

    auto impulse = makeSilence (48000);
    impulse[0][0] = 0.5f;
    impulse[1][0] = 0.5f;

    delay::DelayEngine engine;
    engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    engine.setSettings (settings);

    const auto output = runEngine (engine, impulse);

    const auto left = findImpulses (output[0], 0.05f);
    const auto right = findImpulses (output[1], 0.05f);

    check (left.size() == 1, "one repeat on the left", (double) left.size(), 1.0);
    check (right.size() == 1, "one on the right", (double) right.size(), 1.0);

    if (! left.empty())
        checkClose (left.front().first, 0.100 * sampleRate, 0.5,
                    "the left one exactly 100 ms late");

    if (! right.empty())
        checkClose (right.front().first, 0.150 * sampleRate, 0.5,
                    "and the right one exactly 150 ms late");

    if (! left.empty())
        checkClose (left.front().second, 0.5, 1.0e-6, "at the level it went in at");

    // A hard panned line puts nothing on the other side.
    auto crossTalk = 0.0;

    for (size_t i = 0; i < output[1].size(); ++i)
        if (std::abs ((int) i - (int) (0.100 * sampleRate)) < 20)
            crossTalk = juce::jmax (crossTalk, (double) std::abs (output[1][i]));

    checkClose (crossTalk, 0.0, 1.0e-6, "and nothing at all on the other side");
}

void testPatternMatchesAudio()
{
    std::printf ("\nThe drawing against the audio\n");

    /*  The test this whole file is built around. buildTapPattern() is what the
        interface draws; the engine is what you hear. They share no code.
    */
    auto settings = defaultSettings();
    settings.timeLeftMs = 120.0f;
    settings.timeRightMs = 200.0f;
    settings.feedback = 0.55f;
    settings.cross = 0.4f;

    const auto amplitude = 0.4f;

    auto impulse = makeSilence (48000 * 2);
    impulse[0][0] = amplitude;
    impulse[1][0] = amplitude;

    delay::DelayEngine engine;
    engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    engine.setSettings (settings);

    const auto output = runEngine (engine, impulse);
    /*  A much finer pattern than the display asks for. The drawing prunes
        quiet paths and caps how many repeats it bothers with, which is right
        for a picture and wrong for a comparison: a repeat that is really the
        sum of several routes through the network would be predicted short by
        exactly the paths that were pruned. Dropping the floor two orders of
        magnitude removes the pruning as a variable and leaves only the question
        being asked.
    */
    const auto predicted = buildTapPattern (settings, 1600.0f, 300, 3.0e-4f);

    const auto measured = findImpulses (output[0], amplitude * 0.004f);

    std::printf ("        %d taps predicted on the left, %d measured\n",
                 (int) std::count_if (predicted.begin(), predicted.end(),
                                      [amplitude] (const delay::Tap& t)
                                      { return std::abs (t.left) > 0.004f; }),
                 (int) measured.size());

    auto worstTime = 0.0;
    auto worstLevel = 0.0;
    auto matched = 0;

    for (const auto& tap : predicted)
    {
        const auto expectedLevel = tap.left * amplitude;

        if (std::abs (expectedLevel) < amplitude * 0.01f)
            continue;

        const auto expectedIndex = (int) std::round (tap.timeMs * 0.001 * sampleRate);

        // Find the measured impulse nearest that position.
        auto best = -1;
        auto bestDistance = 1 << 30;

        for (const auto& [index, value] : measured)
        {
            const auto distance = std::abs (index - expectedIndex);

            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = index;
            }
        }

        if (best < 0 || bestDistance > 8)
            continue;

        ++matched;
        worstTime = juce::jmax (worstTime, (double) bestDistance);

        const auto measuredLevel = output[0][(size_t) best];
        worstLevel = juce::jmax (worstLevel, std::abs ((double) measuredLevel - expectedLevel));
    }

    check (matched >= 6, "enough repeats to be worth comparing", (double) matched, 6.0);
    checkClose (worstTime, 0.0, 0.5, "every predicted repeat lands where it was drawn");
    check (worstLevel < amplitude * 0.002, "and at the level it was drawn at",
           worstLevel, amplitude * 0.002);
}

void testFeedbackAndRouting()
{
    std::printf ("\nFeedback and routing\n");

    // Plain feedback: each repeat is the last one times the knob.
    auto settings = defaultSettings();
    settings.timeLeftMs = 100.0f;
    settings.timeRightMs = 100.0f;
    settings.feedback = 0.5f;

    auto impulse = makeSilence (48000);
    impulse[0][0] = 0.4f;
    impulse[1][0] = 0.4f;

    delay::DelayEngine engine;
    engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    engine.setSettings (settings);

    const auto output = runEngine (engine, impulse);
    const auto repeats = findImpulses (output[0], 0.001f);

    check (repeats.size() >= 6, "the repeats keep coming", (double) repeats.size(), 6.0);

    auto worstRatio = 0.0;

    for (size_t i = 1; i < repeats.size() && i < 7; ++i)
    {
        const auto ratio = repeats[i].second / repeats[i - 1].second;
        worstRatio = juce::jmax (worstRatio, std::abs ((double) ratio - 0.5));
    }

    checkClose (worstRatio, 0.0, 0.002, "each one exactly half the last");

    // Ping-pong: the first repeat is on one side and the second on the other,
    // which is the whole point and is a routing rather than an amount.
    auto ping = defaultSettings();
    ping.routing = delay::Routing::pingPong;
    ping.timeLeftMs = 100.0f;
    ping.timeRightMs = 100.0f;
    ping.feedback = 0.7f;

    delay::DelayEngine bouncer;
    bouncer.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    bouncer.setSettings (ping);

    const auto bounced = runEngine (bouncer, impulse);

    const auto atFirst = (int) std::round (0.100 * sampleRate);
    const auto atSecond = (int) std::round (0.200 * sampleRate);

    check (std::abs (bounced[0][(size_t) atFirst]) > 0.3,
           "ping-pong puts the first repeat on the left",
           std::abs (bounced[0][(size_t) atFirst]), 0.3);
    checkClose (bounced[1][(size_t) atFirst], 0.0, 1.0e-6, "and nothing on the right");
    check (std::abs (bounced[1][(size_t) atSecond]) > 0.2,
           "the second one on the right", std::abs (bounced[1][(size_t) atSecond]), 0.2);
    checkClose (bounced[0][(size_t) atSecond], 0.0, 1.0e-6, "and nothing on the left");

    // Inverted feedback flips the sign of every generation.
    auto inverted = settings;
    inverted.invertFeedback = true;

    delay::DelayEngine flipper;
    flipper.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    flipper.setSettings (inverted);

    const auto flipped = runEngine (flipper, impulse);
    const auto flippedRepeats = findImpulses (flipped[0], 0.001f);

    auto alternates = flippedRepeats.size() >= 4;

    for (size_t i = 1; i < flippedRepeats.size() && i < 5; ++i)
        if (flippedRepeats[i].second * flippedRepeats[i - 1].second > 0.0f)
            alternates = false;

    check (alternates, "inverted feedback alternates the sign of each repeat", 1.0, 1.0);
}

void testSync()
{
    std::printf ("\nTempo sync\n");

    auto settings = defaultSettings();
    settings.sync = true;
    settings.bpm = 120.0;

    settings.noteLeft = delay::NoteValue::quarter;
    checkClose (delay::resolvedTimeMs (settings, 0), 500.0, 0.01,
                "a quarter at 120 is 500 ms");

    settings.noteLeft = delay::NoteValue::eighthDotted;
    checkClose (delay::resolvedTimeMs (settings, 0), 375.0, 0.01,
                "a dotted eighth is 375");

    settings.noteLeft = delay::NoteValue::eighthTriplet;
    checkClose (delay::resolvedTimeMs (settings, 0), 1000.0 / 6.0, 0.01,
                "an eighth triplet is a third of a quarter");

    settings.bpm = 90.0;
    settings.noteLeft = delay::NoteValue::quarter;
    checkClose (delay::resolvedTimeMs (settings, 0), 60000.0 / 90.0, 0.01,
                "and it follows the tempo");

    // Link makes the right line copy the left, synced or not.
    settings.linkTimes = true;
    settings.noteRight = delay::NoteValue::whole;
    checkClose (delay::resolvedTimeMs (settings, 1), delay::resolvedTimeMs (settings, 0), 0.01,
                "linked, the right line copies the left");

    // And the engine really runs at it.
    auto engineSettings = defaultSettings();
    engineSettings.sync = true;
    engineSettings.bpm = 120.0;
    engineSettings.noteLeft = delay::NoteValue::eighth;
    engineSettings.linkTimes = true;
    engineSettings.feedback = 0.0f;

    auto impulse = makeSilence (48000);
    impulse[0][0] = 0.5f;

    delay::DelayEngine engine;
    engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    engine.setSettings (engineSettings);

    const auto found = findImpulses (runEngine (engine, impulse)[0], 0.05f);

    check (! found.empty() && std::abs (found.front().first - 0.250 * sampleRate) < 1.0,
           "the engine delays by the synced time",
           found.empty() ? -1.0 : (double) found.front().first, 0.250 * sampleRate);
}

void testTapeAndFade()
{
    std::printf ("\nTape against fade\n");

    /*  The difference between the two modes, as a measurement rather than as a
        claim: with a steady tone going through, moving the delay time bends the
        pitch in tape mode and does not in fade mode.

        The tone is measured by counting zero crossings, which is crude and
        exactly right for the question — has this tone been shifted or not.
    */
    constexpr int total = 48000 * 2;
    constexpr int changeAt = 24000;
    constexpr double toneHz = 1000.0;

    auto probe = [&] (delay::TimeMode mode)
    {
        auto settings = defaultSettings();
        settings.timeMode = mode;
        settings.timeLeftMs = 200.0f;
        settings.timeRightMs = 200.0f;
        settings.feedback = 0.0f;
        settings.mix = 1.0f;

        delay::DelayEngine engine;
        engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
        engine.setSettings (settings);

        const auto tone = makeSine (toneHz, 0.4, total);
        std::vector<std::vector<float>> output (2, std::vector<float> ((size_t) total, 0.0f));

        juce::AudioBuffer<float> buffer (2, blockSize);

        for (int position = 0; position < total; position += blockSize)
        {
            const auto thisBlock = juce::jmin (blockSize, total - position);

            // Halve the delay time part way through.
            if (position >= changeAt)
            {
                settings.timeLeftMs = 100.0f;
                settings.timeRightMs = 100.0f;
                engine.setSettings (settings);
            }

            buffer.setSize (2, thisBlock, false, false, true);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < thisBlock; ++i)
                    buffer.setSample (ch, i, tone[(size_t) ch][(size_t) (position + i)]);

            juce::dsp::AudioBlock<float> block (buffer);
            engine.process (block);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < thisBlock; ++i)
                    output[(size_t) ch][(size_t) (position + i)] = buffer.getSample (ch, i);
        }

        return output;
    };

    const auto taped = probe (delay::TimeMode::tape);
    const auto faded = probe (delay::TimeMode::fade);

    // Just after the change, while the read head is still travelling.
    const auto window = 2400;
    const auto tapeDuring = crossingFrequency (taped[0], changeAt + 400, window);
    const auto fadeDuring = crossingFrequency (faded[0], changeAt + 400, window);

    std::printf ("        during the move: tape %.0f Hz, fade %.0f Hz (from %.0f)\n",
                 tapeDuring, fadeDuring, toneHz);

    check (tapeDuring > toneHz * 1.15, "tape bends the pitch while the head travels",
           tapeDuring, toneHz * 1.15);
    checkClose (fadeDuring, toneHz, 40.0, "fade does not");

    // And both settle back to the original pitch once the move is over.
    const auto tapeAfter = crossingFrequency (taped[0], total - 6000, 4800);
    checkClose (tapeAfter, toneHz, 20.0, "and tape settles back where it started");
}

void testFreezeAndSafety()
{
    std::printf ("\nFreeze and the loop's safety net\n");

    /*  Freeze holds what is in the buffer and lets nothing new in. Measured as
        two windows a delay apart being the same, plus a loud signal arriving
        after the freeze and not appearing at the output.
    */
    constexpr int total = 48000 * 3;
    constexpr int freezeAt = 48000;

    auto settings = defaultSettings();
    settings.timeLeftMs = 250.0f;
    settings.timeRightMs = 250.0f;
    settings.feedback = 0.4f;
    settings.mix = 1.0f;

    auto input = makeSine (300.0, 0.35, total);

    // Silence, then a loud tone, both after the freeze.
    for (int i = freezeAt; i < total; ++i)
    {
        const auto loud = i > freezeAt + 24000 ? 0.9 : 0.0;
        const auto value = (float) (loud * std::sin (2.0 * juce::MathConstants<double>::pi
                                                      * 3000.0 * (double) i / sampleRate));
        input[0][(size_t) i] = value;
        input[1][(size_t) i] = value;
    }

    delay::DelayEngine engine;
    engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    engine.setSettings (settings);

    std::vector<std::vector<float>> output (2, std::vector<float> ((size_t) total, 0.0f));
    juce::AudioBuffer<float> buffer (2, blockSize);

    for (int position = 0; position < total; position += blockSize)
    {
        const auto thisBlock = juce::jmin (blockSize, total - position);

        if (position >= freezeAt && ! settings.freeze)
        {
            settings.freeze = true;
            engine.setSettings (settings);
        }

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

    auto rms = [&output] (int from, int count)
    {
        auto sum = 0.0;

        for (int i = from; i < from + count; ++i)
            sum += (double) output[0][(size_t) i] * output[0][(size_t) i];

        return std::sqrt (sum / (double) count);
    };

    const auto early = rms (total - 24000, 4800);
    const auto late  = rms (total - 4800, 4800);

    check (early > 0.01, "the frozen loop is still going", early, 0.01);
    check (std::abs (late - early) / juce::jmax (1.0e-9, early) < 0.02,
           "and it is not decaying", std::abs (late - early) / juce::jmax (1.0e-9, early), 0.02);

    // The 3 kHz tone arriving after the freeze must not be in there.
    const auto leaked = goertzel (output[0], total - 12000, 9600, 3000.0);
    const auto held = goertzel (output[0], total - 12000, 9600, 300.0);

    check (leaked < held * 0.02, "and nothing new got into it", leaked, held * 0.02);

    // --- the safety net ----------------------------------------------------
    /*  Feedback past unity is allowed on purpose. What must not happen is the
        loop running away: a soft clipper inside it means a runaway lands on a
        steady tone instead of on infinity.
    */
    auto runaway = defaultSettings();
    runaway.feedback = 1.1f;
    runaway.timeLeftMs = 50.0f;
    runaway.timeRightMs = 50.0f;
    runaway.mix = 1.0f;

    delay::DelayEngine loop;
    loop.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    loop.setSettings (runaway);

    auto hot = makeSine (400.0, 0.8, 48000 * 6);

    for (int i = 4800; i < (int) hot[0].size(); ++i)
        hot[0][(size_t) i] = hot[1][(size_t) i] = 0.0f;

    const auto sustained = runEngine (loop, hot);

    auto peak = 0.0;
    auto finite = true;

    for (const auto& sample : sustained[0])
    {
        peak = juce::jmax (peak, (double) std::abs (sample));

        if (! std::isfinite (sample))
            finite = false;
    }

    check (finite, "at 110 % feedback nothing goes non-finite", 1.0, 1.0);
    check (peak < 1.2, "and the loop settles instead of running away", peak, 1.2);
}

void testCharacter()
{
    std::printf ("\nDrive, lo-fi and the loop filters\n");

    /*  Where the drive stage's output is normalised, measured rather than
        assumed. It is referenced to -12 dBFS, roughly where a delay loop sits,
        because the two obvious alternatives are both wrong: normalising at zero
        turns the knob into a volume control pointing downwards, and normalising
        at full scale lifts small signals by the whole drive amount, which
        inside a feedback loop is a runaway.

        The closed form says the small-signal lift is exactly the reference
        level divided by the clipper's value there, which at the plugin's
        maximum drive of 4x works out at 1.5.
    */
    /*  Measured peak to peak, not sample by sample. ADAA reports the *average*
        of the shaping function across the segment between two samples, so
        dividing one output sample by one input sample gives a ratio that blows
        up near a zero crossing — where the input is nearly nothing and the
        average across the segment is not. Comparing peaks asks the question
        that was meant.
    */
    auto smallSignalGain = [] (float drive)
    {
        dsp::SoftSaturator saturator;
        auto inputPeak = 0.0, outputPeak = 0.0;

        for (int i = 0; i < 4000; ++i)
        {
            const auto x = (float) (0.0005 * std::sin (2.0 * juce::MathConstants<double>::pi
                                                        * 500.0 * (double) i / sampleRate));
            const auto y = saturator.process (x, drive);

            if (i > 400)
            {
                inputPeak = juce::jmax (inputPeak, (double) std::abs (x));
                outputPeak = juce::jmax (outputPeak, (double) std::abs (y));
            }
        }

        return outputPeak / juce::jmax (1.0e-12, inputPeak);
    };

    // The stage's total small-signal gain is the drive times the compensation,
    // and the compensation is a closed form: the reference level divided by the
    // clipper's value there.
    checkClose (smallSignalGain (1.0f), (double) dsp::SoftSaturator::makeupFor (1.0f), 0.01,
                "undriven, the shaper is within a fifth of a dB of a wire");
    checkClose (smallSignalGain (4.0f), 4.0 * (double) dsp::SoftSaturator::makeupFor (4.0f), 0.02,
                "at full drive the small-signal lift is the closed form");
    checkClose (4.0 * (double) dsp::SoftSaturator::makeupFor (4.0f), 1.5, 0.001,
                "which works out at exactly 1.5");

    // Loud material is compressed rather than lifted, which is why a saturating
    // loop settles instead of climbing.
    dsp::SoftSaturator loud;
    auto loudPeak = 0.0;

    for (int i = 0; i < 2000; ++i)
    {
        const auto x = (float) (0.8 * std::sin (2.0 * juce::MathConstants<double>::pi
                                                 * 500.0 * (double) i / sampleRate));
        const auto y = loud.process (x, 4.0f);

        if (i > 200)
            loudPeak = juce::jmax (loudPeak, (double) std::abs (y));
    }

    check (loudPeak < 0.8, "and loud material is squashed, not lifted", loudPeak, 0.8);

    /*  Antialiasing, measured. A cubic clipper on a 9 kHz sine produces a third
        harmonic at 27 kHz, which at a 48 kHz sample rate has nowhere to go but
        back down to 21 kHz. ADAA averages the shaping function across the
        segment between two samples, and that averaging happens before the
        folding rather than after it — which is the only place it can help.
    */
    constexpr double toneHz = 9000.0;
    constexpr double aliasHz = 3.0 * toneHz - sampleRate;    // 21 kHz

    std::vector<float> naive (24000, 0.0f), antialiased (24000, 0.0f);
    dsp::SoftSaturator adaa;

    for (size_t i = 0; i < naive.size(); ++i)
    {
        const auto x = (float) (0.7 * std::sin (2.0 * juce::MathConstants<double>::pi
                                                 * toneHz * (double) i / sampleRate));
        naive[i] = dsp::cubicClip (x * 4.0f) * dsp::SoftSaturator::makeupFor (4.0f);
        antialiased[i] = adaa.process (x, 4.0f);
    }

    const auto naiveAlias = goertzel (naive, 4800, 16384, aliasHz);
    const auto adaaAlias = goertzel (antialiased, 4800, 16384, aliasHz);

    std::printf ("        the folded third harmonic: naive %.5f, ADAA %.5f\n",
                 naiveAlias, adaaAlias);

    check (adaaAlias < naiveAlias * 0.7, "ADAA folds back noticeably less than the plain shaper",
           adaaAlias / juce::jmax (1.0e-9, naiveAlias), 0.7);

    // Lo-fi at zero has to be bit for bit transparent, or the knob is a tax.
    dsp::BitCrusher crusher;
    crusher.prepare (sampleRate);

    auto worstCrush = 0.0;
    juce::Random random (5);

    for (int i = 0; i < 4000; ++i)
    {
        const auto x = random.nextFloat() * 2.0f - 1.0f;
        worstCrush = juce::jmax (worstCrush, std::abs ((double) crusher.process (x, 0.0f) - (double) x));
    }

    checkClose (worstCrush, 0.0, 0.0, "lo-fi at zero is bit for bit the input");

    // And at the top it is quantising: a smooth ramp comes out as steps.
    dsp::BitCrusher hard;
    hard.prepare (sampleRate);

    std::vector<float> stepped;

    for (int i = 0; i < 2000; ++i)
        stepped.push_back (hard.process ((float) i / 2000.0f * 0.9f, 1.0f));

    std::vector<float> distinct = stepped;
    std::sort (distinct.begin(), distinct.end());
    distinct.erase (std::unique (distinct.begin(), distinct.end()), distinct.end());

    check (distinct.size() < 40, "and at the top it comes out in steps",
           (double) distinct.size(), 40.0);

    /*  The loop filters compound, which is the whole reason they are inside the
        loop and the reason the display draws the curve once per generation. A
        low pass that takes 6 dB off the first repeat takes 12 off the second.
    */
    auto settings = defaultSettings();
    settings.timeLeftMs = 100.0f;
    settings.timeRightMs = 100.0f;
    settings.feedback = 1.0f;         // so level changes are the filter and nothing else
    settings.lowpassHz = 1000.0f;
    settings.mix = 1.0f;

    auto burst = makeSilence (48000);

    for (int i = 0; i < 2400; ++i)
    {
        const auto value = (float) (0.3 * std::sin (2.0 * juce::MathConstants<double>::pi
                                                     * 4000.0 * (double) i / sampleRate));
        burst[0][(size_t) i] = value;
        burst[1][(size_t) i] = value;
    }

    delay::DelayEngine engine;
    engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    engine.setSettings (settings);

    const auto output = runEngine (engine, burst);

    const auto step = (int) std::round (0.100 * sampleRate);
    const auto first  = goertzel (output[0], step, 2048, 4000.0);
    const auto second = goertzel (output[0], step * 2, 2048, 4000.0);
    const auto third  = goertzel (output[0], step * 3, 2048, 4000.0);

    const auto firstDrop = 20.0 * std::log10 (juce::jmax (1.0e-9, second / juce::jmax (1.0e-9, first)));
    const auto secondDrop = 20.0 * std::log10 (juce::jmax (1.0e-9, third / juce::jmax (1.0e-9, second)));

    std::printf ("        4 kHz through a 1 kHz loop filter: %.2f dB then %.2f dB\n",
                 firstDrop, secondDrop);

    check (firstDrop < -15.0, "the loop filter takes a real bite out of each repeat",
           firstDrop, -15.0);
    checkClose (secondDrop, firstDrop, 1.0, "and it takes the same bite again on the next");
}

void testMixAndWidth()
{
    std::printf ("\nMix, pan and width\n");

    // At zero mix the plugin is a wire.
    auto settings = defaultSettings();
    settings.mix = 0.0f;
    settings.feedback = 0.6f;

    juce::Random random (29);
    auto noise = makeSilence (24000);

    for (size_t i = 0; i < noise[0].size(); ++i)
    {
        noise[0][i] = random.nextFloat() * 0.6f - 0.3f;
        noise[1][i] = random.nextFloat() * 0.6f - 0.3f;
    }

    delay::DelayEngine engine;
    engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    engine.setSettings (settings);

    const auto through = runEngine (engine, noise);

    auto worst = 0.0;

    for (int ch = 0; ch < 2; ++ch)
        for (size_t i = 0; i < noise[0].size(); ++i)
            worst = juce::jmax (worst, std::abs ((double) through[(size_t) ch][i]
                                                 - (double) noise[(size_t) ch][i]));

    checkClose (worst, 0.0, 1.0e-6, "at zero mix it is a wire");

    // Width at zero makes the wet mono: both sides identical.
    auto narrow = defaultSettings();
    narrow.width = 0.0f;
    narrow.feedback = 0.3f;
    narrow.timeLeftMs = 120.0f;
    narrow.timeRightMs = 190.0f;

    delay::DelayEngine mono;
    mono.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    mono.setSettings (narrow);

    auto impulse = makeSilence (24000);
    impulse[0][0] = 0.5f;
    impulse[1][0] = 0.5f;

    const auto collapsed = runEngine (mono, impulse);

    auto widest = 0.0;

    for (size_t i = 0; i < collapsed[0].size(); ++i)
        widest = juce::jmax (widest, std::abs ((double) collapsed[0][i] - (double) collapsed[1][i]));

    checkClose (widest, 0.0, 1.0e-6, "width at zero collapses the wet to mono");

    // Constant power panning: a line in the centre is not louder than one
    // panned hard.
    float centreL = 0.0f, centreR = 0.0f, hardL = 0.0f, hardR = 0.0f;
    delay::panGains (0.0f, centreL, centreR);
    delay::panGains (-1.0f, hardL, hardR);

    checkClose (centreL * centreL + centreR * centreR, hardL * hardL + hardR * hardR, 1.0e-6,
                "panning holds the power constant across the picture");
}

void testMultiTapIsInertWhenOff()
{
    std::printf ("\nWith multi-tap off, none of it exists\n");

    /*  The most important test in this file and the least interesting to read.
        Phase two added eighty parameters to a plugin that already worked, and
        the property that has to survive all of them is that a session saved
        before any of it existed opens and sounds the same.

        So: take a settings block, fill every new field with something
        deliberately disruptive, leave multi-tap off and every matrix amount at
        zero, and require the output to be identical sample for sample.
    */
    auto plain = defaultSettings();
    plain.timeLeftMs = 130.0f;
    plain.timeRightMs = 210.0f;
    plain.feedback = 0.5f;
    plain.cross = 0.3f;
    plain.mix = 0.7f;
    plain.drive = 0.3f;
    plain.lowpassHz = 4000.0f;

    auto loaded = plain;
    loaded.multiTap = false;
    loaded.division = 13;
    loaded.tapSpread = 0.2f;

    for (int i = 0; i < delay::numTaps; ++i)
        loaded.taps[(size_t) i] = { true, 0.9f - 0.05f * (float) i, (i % 2 == 0 ? -0.9f : 0.9f) };

    loaded.lfos[0] = { 3.0f, false, delay::NoteValue::eighth, dsp::Lfo::Shape::square, 0.25f };
    loaded.lfos[1] = { 0.7f, true, delay::NoteValue::half, dsp::Lfo::Shape::randomStep, 0.5f };
    loaded.envelopeAttackMs = 3.0f;
    loaded.envelopeReleaseMs = 400.0f;

    // Sources and targets set, amounts at zero: the slots exist but say nothing.
    loaded.mod[0] = { delay::ModSource::lfo1, delay::ModTarget::time, 0.0f };
    loaded.mod[1] = { delay::ModSource::lfo2, delay::ModTarget::feedback, 0.0f };
    loaded.mod[2] = { delay::ModSource::envelope, delay::ModTarget::mix, 0.0f };

    juce::Random random (91);
    auto material = makeSilence (48000);

    for (size_t i = 0; i < material[0].size(); ++i)
    {
        material[0][i] = random.nextFloat() * 0.6f - 0.3f;
        material[1][i] = random.nextFloat() * 0.6f - 0.3f;
    }

    auto run = [&material] (const delay::DelaySettings& settings)
    {
        delay::DelayEngine engine;
        engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
        engine.setSettings (settings);
        return runEngine (engine, material);
    };

    const auto before = run (plain);
    const auto after = run (loaded);

    auto worst = 0.0;

    for (int ch = 0; ch < 2; ++ch)
        for (size_t i = 0; i < material[0].size(); ++i)
            worst = juce::jmax (worst, std::abs ((double) before[(size_t) ch][i]
                                                 - (double) after[(size_t) ch][i]));

    checkClose (worst, 0.0, 0.0, "sixteen taps and two LFOs later, not one sample moved");
}

void testMultiTap()
{
    std::printf ("\nMulti-tap\n");

    /*  At a division of one the pattern is a single step landing on the delay
        time itself, which is where the loop closes — so it has to be the plain
        delay it was before. The input goes in on the left only so the right
        line contributes nothing and the two paths can be compared directly.
    */
    auto off = defaultSettings();
    off.timeLeftMs = 100.0f;
    off.feedback = 0.5f;
    off.panLeft = -1.0f;

    auto on = off;
    on.multiTap = true;
    on.division = 1;
    on.taps[0] = { true, 1.0f, -1.0f };

    auto impulse = makeSilence (48000);
    impulse[0][0] = 0.5f;

    auto run = [&impulse] (const delay::DelaySettings& settings)
    {
        delay::DelayEngine engine;
        engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
        engine.setSettings (settings);
        return runEngine (engine, impulse);
    };

    const auto plain = run (off);
    const auto single = run (on);

    auto worst = 0.0;

    for (size_t i = 0; i < impulse[0].size(); ++i)
        worst = juce::jmax (worst, std::abs ((double) plain[0][i] - (double) single[0][i]));

    checkClose (worst, 0.0, 1.0e-7, "one step is the plain delay, to the last bit");

    /*  The normalisation, measured. Sixteen taps cannot be twenty-four decibels
        louder than one, and the choice made here is to divide by the square
        root of the division rather than by how many steps happen to be on —
        so that toggling a step thins the pattern instead of turning the rest
        up. With only the last step on, that factor is the whole difference.
    */
    auto quartered = on;
    quartered.division = 4;

    for (auto& tap : quartered.taps)
        tap.on = false;

    quartered.taps[3] = { true, 1.0f, -1.0f };

    const auto scaled = run (quartered);
    const auto at100ms = (int) std::round (0.100 * sampleRate);

    checkClose (scaled[0][(size_t) at100ms], plain[0][(size_t) at100ms] * 0.5, 1.0e-6,
                "four steps with one on is exactly half the level of one step");

    // And the steps land where they should: quarters of the delay time.
    auto spaced = on;
    spaced.division = 4;
    spaced.feedback = 0.0f;

    for (int i = 0; i < 4; ++i)
        spaced.taps[(size_t) i] = { true, 1.0f, -1.0f };

    const auto pattern = run (spaced);
    const auto found = findImpulses (pattern[0], 0.05f);

    check (found.size() == 4, "four steps make four repeats", (double) found.size(), 4.0);

    auto worstPosition = 0.0;

    for (size_t i = 0; i < found.size() && i < 4; ++i)
        worstPosition = juce::jmax (worstPosition,
                                    std::abs ((double) found[i].first
                                              - 0.025 * sampleRate * (double) (i + 1)));

    checkClose (worstPosition, 0.0, 0.5, "at exact quarters of the delay time");
}

void testMultiTapPatternMatchesAudio()
{
    std::printf ("\nThe drawing against the audio, with a pattern\n");

    auto settings = defaultSettings();
    settings.timeLeftMs = 320.0f;
    settings.timeRightMs = 250.0f;
    settings.feedback = 0.5f;
    settings.cross = 0.0f;
    settings.multiTap = true;
    settings.division = 8;
    settings.tapSpread = 1.0f;

    const bool on[8] { true, false, true, true, false, true, false, true };

    for (int i = 0; i < 8; ++i)
        settings.taps[(size_t) i] = { on[i], 0.9f - 0.06f * (float) i,
                                      (i % 2 == 0 ? -0.8f : 0.5f) };

    const auto amplitude = 0.4f;

    /*  An impulse on both inputs, which is what the drawing assumes. Feeding
        one side only looked tidier and was wrong: where two routes arrive at
        the same instant the drawing adds them, and comparing that against audio
        in which one of the two routes is silent is comparing two different
        questions.
    */
    auto impulse = makeSilence (48000 * 2);
    impulse[0][0] = amplitude;
    impulse[1][0] = amplitude;

    delay::DelayEngine engine;
    engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    engine.setSettings (settings);

    const auto output = runEngine (engine, impulse);
    const auto predicted = buildTapPattern (settings, 1600.0f, 400, 2.0e-4f);
    const auto measured = findImpulses (output[0], amplitude * 0.002f);

    auto worstTime = 0.0, worstLevel = 0.0;
    auto matched = 0;

    for (const auto& tap : predicted)
    {
        const auto expectedLevel = tap.left * amplitude;

        if (std::abs (expectedLevel) < amplitude * 0.01f)
            continue;

        const auto expectedIndex = (int) std::round (tap.timeMs * 0.001 * sampleRate);

        auto best = -1;
        auto bestDistance = 1 << 30;

        for (const auto& [index, value] : measured)
            if (std::abs (index - expectedIndex) < bestDistance)
            {
                bestDistance = std::abs (index - expectedIndex);
                best = index;
            }

        if (best < 0 || bestDistance > 8)
            continue;

        ++matched;
        worstTime = juce::jmax (worstTime, (double) bestDistance);
        worstLevel = juce::jmax (worstLevel,
                                 std::abs ((double) output[0][(size_t) best] - expectedLevel));
    }

    check (matched >= 10, "enough repeats to be worth comparing", (double) matched, 10.0);
    checkClose (worstTime, 0.0, 0.5, "every step of every pass lands where it was drawn");
    check (worstLevel < amplitude * 0.004, "and at the level it was drawn at",
           worstLevel, amplitude * 0.004);
}

void testModulation()
{
    std::printf ("\nModulación\n");

    // --- the LFO on its own ------------------------------------------------
    for (int shapeIndex = 0; shapeIndex < dsp::Lfo::numShapes; ++shapeIndex)
    {
        dsp::Lfo lfo;
        lfo.prepare (sampleRate);
        lfo.setShape ((dsp::Lfo::Shape) shapeIndex);
        lfo.setRateHz (5.0f);

        auto low = 10.0f, high = -10.0f;

        for (int i = 0; i < 48000; ++i)
        {
            const auto value = lfo.process();
            low = juce::jmin (low, value);
            high = juce::jmax (high, value);
        }

        if (low < -1.0001f || high > 1.0001f)
        {
            check (false, "every shape stays inside -1 to 1", juce::jmax (-low, high), 1.0);
            return;
        }
    }

    check (true, "every shape stays inside -1 to 1", 1.0, 1.0);

    // The rate means what it says: count the zero crossings of a sine.
    {
        dsp::Lfo lfo;
        lfo.prepare (sampleRate);
        lfo.setRateHz (4.0f);

        /*  Four seconds rather than one. Counting zero crossings misses the
            one that lands on the last sample, and over a single second of a
            4 Hz sine that is one crossing in eight — a twelve percent error
            that has nothing to do with the oscillator.
        */
        std::vector<float> trace ((size_t) sampleRate * 4, 0.0f);

        for (auto& value : trace)
            value = lfo.process();

        checkClose (steadyFrequency (trace), 4.0, 0.01, "and the rate is the rate");
    }

    /*  The random shapes have to be deterministic, or a session does not
        recall: the same settings on two instances have to give the same sound.
    */
    {
        dsp::Lfo a, b;
        a.prepare (sampleRate);
        b.prepare (sampleRate);
        a.setShape (dsp::Lfo::Shape::randomStep);
        b.setShape (dsp::Lfo::Shape::randomStep);
        a.setRateHz (7.0f);
        b.setRateHz (7.0f);

        auto worst = 0.0;

        for (int i = 0; i < 24000; ++i)
            worst = juce::jmax (worst, std::abs ((double) a.process() - (double) b.process()));

        checkClose (worst, 0.0, 0.0, "and the random shapes are the same random every time");
    }

    // --- a slot pointed at the mix -----------------------------------------
    /*  A square LFO on the mix is the easiest modulation to measure: the output
        alternates between fully wet and fully dry, and the two halves can be
        read off separately.
    */
    constexpr int total = 48000 * 2;

    auto settings = defaultSettings();
    settings.timeLeftMs = 100.0f;
    settings.timeRightMs = 100.0f;
    settings.feedback = 0.0f;
    settings.mix = 0.5f;
    settings.lfos[0] = { 1.0f, false, delay::NoteValue::quarter, dsp::Lfo::Shape::square, 0.0f };
    settings.mod[0] = { delay::ModSource::lfo1, delay::ModTarget::mix, 1.0f };

    delay::DelayEngine engine;
    engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    engine.setSettings (settings);

    /*  997 Hz rather than 1000. A 100 ms delay of a 1 kHz tone is exactly one
        hundred cycles, so the delayed copy and the dry one are the same signal
        and the mix control provably does nothing — which the first version of
        this test duly proved.
    */
    auto tone = makeSine (997.0, 0.4, total);
    const auto output = runEngine (engine, tone);

    // First half second of each LFO cycle: mix pushed to 1, so the output is the
    // delayed tone only. Second half: mix pushed to 0, the dry tone only.
    // Either way it is a 1 kHz tone, so measure the *level*, which differs
    // because wet and dry are the same amplitude — instead compare against a
    // run with no modulation at all.
    auto unmodulated = settings;
    unmodulated.mod[0].amount = 0.0f;

    delay::DelayEngine reference;
    reference.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    reference.setSettings (unmodulated);

    const auto flat = runEngine (reference, tone);

    auto difference = 0.0;

    for (size_t i = 24000; i < (size_t) total; ++i)
        difference = juce::jmax (difference, std::abs ((double) output[0][i] - (double) flat[0][i]));

    check (difference > 0.05, "a slot pointed at the mix moves the mix", difference, 0.05);

    // --- and time modulation bends the pitch, like the tape mode does ------
    auto timeMod = defaultSettings();
    timeMod.timeLeftMs = 200.0f;
    timeMod.timeRightMs = 200.0f;
    timeMod.feedback = 0.0f;
    timeMod.mix = 1.0f;
    timeMod.lfos[0] = { 0.5f, false, delay::NoteValue::quarter, dsp::Lfo::Shape::sine, 0.0f };
    timeMod.mod[0] = { delay::ModSource::lfo1, delay::ModTarget::time, 0.8f };

    delay::DelayEngine bender;
    bender.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    bender.setSettings (timeMod);

    const auto bent = runEngine (bender, makeSine (1000.0, 0.4, 48000 * 5));

    /*  Sampled across a whole LFO period, not part of one. Half a cycle of a
        sine has a derivative of one sign throughout, so the read head is
        travelling one way the whole time and the pitch only ever goes up —
        which is what the first version of this measured, and it looked like a
        bug in the modulation rather than a window that was too short.
    */
    auto lowest = 1.0e9, highest = 0.0;

    for (int window = 0; window < 20; ++window)
    {
        const auto from = 48000 + window * 4800;
        const auto hz = crossingFrequency (bent[0], from, 4800);
        lowest = juce::jmin (lowest, hz);
        highest = juce::jmax (highest, hz);
    }

    std::printf ("        the modulated tone ranges from %.0f to %.0f Hz\n", lowest, highest);

    check (lowest < 940.0 && highest > 1060.0,
           "time modulation bends the pitch both ways", highest - lowest, 120.0);
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
    juce::AudioProcessorValueTreeState state (holder, nullptr, "NodoDelay",
                                              delay::createParameterLayout());

    const auto presets = delay::buildFactoryPresets();

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


    // Every preset, run through the engine, has to stay finite and bounded.
    auto worstPeak = 0.0;
    auto allFinite = true;

    for (const auto& preset : presets)
    {
        for (const auto& [id, value] : preset.assignments)
            if (auto* parameter = state.getParameter (id))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));

        auto settings = delay::readSettings (state);
        settings.bpm = 128.0;

        delay::DelayEngine engine;
        engine.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
        engine.setSettings (settings);

        juce::Random random (77);
        auto material = makeSilence (48000 * 2);

        for (int i = 0; i < 24000; ++i)
        {
            const auto value = random.nextFloat() * 0.6f - 0.3f;
            material[0][(size_t) i] = value;
            material[1][(size_t) i] = value * 0.8f;
        }

        const auto output = runEngine (engine, material);

        for (int ch = 0; ch < 2; ++ch)
            for (const auto& sample : output[(size_t) ch])
            {
                worstPeak = juce::jmax (worstPeak, (double) std::abs (sample));

                if (! std::isfinite (sample))
                    allFinite = false;
            }
    }

    check (allFinite, "no preset produces anything non-finite", 1.0, 1.0);
    check (worstPeak < 1.5, "and none of them runs away", worstPeak, 1.5);
}
} // namespace

Result runDelayTests()
{
    std::printf ("\n\nNodo Delay verification\n=======================\n");

    testInterpolation();
    testTiming();
    testPatternMatchesAudio();
    testFeedbackAndRouting();
    testSync();
    testTapeAndFade();
    testFreezeAndSafety();
    testCharacter();
    testMixAndWidth();
    testMultiTapIsInertWhenOff();
    testMultiTap();
    testMultiTapPatternMatchesAudio();
    testModulation();
    testFactoryPresets();

    return { checks, failures };
}
} // namespace nodo::tests
