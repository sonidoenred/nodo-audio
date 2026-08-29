#include "DelayEngine.h"
#include "TapPattern.h"

namespace nodo::delay
{
namespace
{
    constexpr float maximumDelayMs = 5000.0f;

    /** How long the read head takes to reach a new distance in tape mode.

        This number is the whole character of the mode. Too short and moving the
        knob is a click; too long and the delay feels like it is arguing with
        you. A fifth of a second is about what a tape machine's capstan takes to
        settle, and it puts the pitch bend in the range where it reads as an
        effect rather than as a fault.
    */
    constexpr float tapeGlideSeconds = 0.20f;

    /** The crossfade in fade mode. Long enough that neither end clicks, short
        enough that a moving automation lane does not smear.
    */
    constexpr float fadeSeconds = 0.030f;

    /** The most the modulation moves the read head at full depth. Scaled down
        for short delays further on: eight milliseconds of wobble on a twenty
        millisecond delay is not wow, it is a broken machine.
    */
    constexpr float maximumWowMs = 8.0f;

    inline float equalPower (float a, float b, float position) noexcept
    {
        const auto angle = juce::jlimit (0.0f, 1.0f, position) * juce::MathConstants<float>::halfPi;
        return a * std::cos (angle) + b * std::sin (angle);
    }
}

void DelayEngine::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;

    // Room for the longest delay plus the deepest modulation, plus a margin so
    // the interpolator never reaches past the end of the buffer.
    maxDelaySamples = (int) std::ceil ((maximumDelayMs + maximumWowMs + 4.0f) * 0.001 * sampleRate);

    for (auto& line : lines)
    {
        line.buffer.prepare (maxDelaySamples);
        line.crusher.prepare (sampleRate);
        line.currentSamples = (float) (0.375 * sampleRate);
        line.previousSamples = line.currentSamples;
    }

    mixAmount.reset (sampleRate, 0.02);
    feedbackAmount.reset (sampleRate, 0.05);
    crossAmount.reset (sampleRate, 0.05);
    widthAmount.reset (sampleRate, 0.02);

    for (auto& lfo : lfos)
        lfo.prepare (sampleRate);

    envelope.prepare (sampleRate);

    fadeIncrement = 1.0f / juce::jmax (1.0f, fadeSeconds * (float) sampleRate);
    timeGlideCoeff = std::exp (-1.0f / (tapeGlideSeconds * (float) sampleRate));
    levelCoeff = std::exp (-1.0f / (0.15f * (float) sampleRate));

    reset();
}

void DelayEngine::reset()
{
    for (auto& line : lines)
        line.reset();

    settingsValid = false;

    for (auto& lfo : lfos)
        lfo.reset();

    envelope.reset();

    for (auto& value : loopLevel)
        value.store (0.0f, std::memory_order_relaxed);

    for (auto& value : sourceValue)
        value.store (0.0f, std::memory_order_relaxed);
}

void DelayEngine::applyPendingSettings()
{
    settings = pending;

    filtersActive = loopFiltersActive (settings);

    if (filtersActive)
    {
        const auto hp = dsp::BiquadCoefficients::highPass (
            sampleRate, juce::jlimit (20.0, sampleRate * 0.45, (double) settings.highpassHz), 0.707);
        const auto lp = dsp::BiquadCoefficients::lowPass (
            sampleRate, juce::jlimit (20.0, sampleRate * 0.45, (double) settings.lowpassHz), 0.707);

        for (auto& line : lines)
        {
            line.highpass.setCoefficients (hp);
            line.lowpass.setCoefficients (lp);
        }
    }

    const auto pingPong = settings.routing == Routing::pingPong;

    const auto mixTarget = juce::jlimit (0.0f, 1.0f, settings.mix);
    const auto feedbackTarget = settings.freeze ? 1.0f : juce::jlimit (0.0f, 1.1f, settings.feedback);
    const auto crossTarget = pingPong ? 1.0f : juce::jlimit (0.0f, 1.0f, settings.cross);
    const auto widthTarget = juce::jlimit (0.0f, 2.0f, settings.width);

    // As everywhere else in the suite: a SmoothedValue starts at zero, so
    // ramping on the very first block would fade the plugin in from silence
    // every time the transport starts.
    if (settingsValid)
    {
        mixAmount.setTargetValue (mixTarget);
        feedbackAmount.setTargetValue (feedbackTarget);
        crossAmount.setTargetValue (crossTarget);
        widthAmount.setTargetValue (widthTarget);
    }
    else
    {
        mixAmount.setCurrentAndTargetValue (mixTarget);
        feedbackAmount.setCurrentAndTargetValue (feedbackTarget);
        crossAmount.setCurrentAndTargetValue (crossTarget);
        widthAmount.setCurrentAndTargetValue (widthTarget);

        for (int i = 0; i < 2; ++i)
        {
            const auto target = (float) (resolvedTimeMs (settings, i) * 0.001 * sampleRate);
            lines[(size_t) i].currentSamples = target;
            lines[(size_t) i].previousSamples = target;
        }
    }

    for (int i = 0; i < numLfos; ++i)
    {
        auto& lfo = lfos[(size_t) i];

        lfo.setRateHz (resolvedLfoRateHz (settings, i));
        lfo.setShape (settings.lfos[(size_t) i].shape);
        lfo.setPhaseOffset (settings.lfos[(size_t) i].phase);
    }

    envelope.setTimes (settings.envelopeAttackMs, settings.envelopeReleaseMs);

    modActive = modulationActive (settings);

    settingsValid = true;
}

void DelayEngine::process (juce::dsp::AudioBlock<float>& block)
{
    applyPendingSettings();

    const auto numSamples = (int) block.getNumSamples();
    const auto channels = (int) juce::jmin ((size_t) 2, block.getNumChannels());

    if (numSamples <= 0 || channels <= 0)
        return;

    std::array<float*, 2> audio { nullptr, nullptr };

    for (int ch = 0; ch < channels; ++ch)
        audio[(size_t) ch] = block.getChannelPointer ((size_t) ch);

    const auto mono = channels < 2;

    std::array<float, 2> targetSamples {};
    std::array<float, 2> maxWowSamples {};

    for (int i = 0; i < 2; ++i)
    {
        const auto ms = resolvedTimeMs (settings, i);
        targetSamples[(size_t) i] = (float) (ms * 0.001 * sampleRate);

        // Proportional for short delays, absolute for long ones.
        const auto wowMs = juce::jmin (maximumWowMs, ms * 0.25f);
        maxWowSamples[(size_t) i] = (float) (wowMs * 0.001 * sampleRate);
    }

    const auto tape = settings.timeMode == TimeMode::tape;
    const auto driveAmount = juce::jlimit (0.0f, 1.0f, settings.drive);
    const auto loFiAmount = juce::jlimit (0.0f, 1.0f, settings.loFi);
    const auto invert = settings.invertFeedback ? -1.0f : 1.0f;
    const auto freeze = settings.freeze;
    const auto modDepth = juce::jlimit (0.0f, 1.0f, settings.modDepth);
    const auto lfoIncrement = (float) (juce::jlimit (0.01f, 20.0f, settings.modRateHz) / sampleRate);

    float panGain[2][2];
    panGains (settings.panLeft, panGain[0][0], panGain[0][1]);
    panGains (settings.panRight, panGain[1][0], panGain[1][1]);

    // ---- multi-tap ---------------------------------------------------------
    const auto multiTap = settings.multiTap;
    const auto steps = activeSteps (settings);

    /*  Normalised by the division rather than by how many steps happen to be
        switched on. Both choices stop sixteen taps being twenty-four decibels
        louder than one, but only this one leaves the other taps alone when you
        toggle a step: turning one off should thin the pattern out, not turn
        everything else up.
    */
    const auto tapNormalise = 1.0f / std::sqrt ((float) juce::jmax (1, steps));

    // ---- modulation --------------------------------------------------------
    std::array<bool, numModTargets> targetUsed {};

    if (modActive)
        for (const auto& slot : settings.mod)
            if (slot.source != ModSource::none && slot.target != ModTarget::none)
                targetUsed[(size_t) slot.target] = true;

    /*  Filter cutoffs get recomputed on a control block rather than per sample.
        A biquad's coefficients cost four transcendentals to work out and the
        loop has four of them; every thirty-two samples is a third of a
        millisecond, which is far below anything that could be heard as a step
        and a fortieth of the arithmetic.
    */
    const auto filterModulated = targetUsed[(size_t) ModTarget::loopHighpass]
                              || targetUsed[(size_t) ModTarget::loopLowpass];
    constexpr int controlBlock = 32;
    auto controlCounter = 0;

    std::array<float, numModTargets> modAcc {};

    // What the sources were doing on the last sample of the block, for the
    // display. Read once per repaint; there is no point publishing more often
    // than the screen can show.
    std::array<float, 3> lastSources { 0.0f, 0.0f, 0.0f };

    for (int n = 0; n < numSamples; ++n)
    {
        const auto dryL = audio[0][n];
        const auto dryR = mono ? dryL : audio[1][n];

        /*  The modulators run whether or not anything is listening, so the
            display has something to show and so a slot switched on mid-phrase
            does not start from a jump. Only the accumulation is skipped, which
            leaves every target exactly at its knob value.
        */
        const float sources[4] { 0.0f,
                                 lfos[0].process(),
                                 lfos[1].process(),
                                 envelope.process ((dryL + dryR) * 0.5f) };

        lastSources[0] = sources[1];
        lastSources[1] = sources[2];
        lastSources[2] = sources[3];

        if (modActive)
        {
            modAcc.fill (0.0f);

            for (const auto& slot : settings.mod)
                if (slot.source != ModSource::none && slot.target != ModTarget::none)
                    modAcc[(size_t) slot.target] += sources[(size_t) slot.source] * slot.amount;
        }

        if (filterModulated && --controlCounter <= 0)
        {
            controlCounter = controlBlock;

            // Two octaves either way at full depth, which is enough to sweep a
            // loop filter across the range where it matters and not so much
            // that a small amount does nothing.
            const auto hp = juce::jlimit (20.0, sampleRate * 0.45,
                                          (double) settings.highpassHz
                                              * std::pow (2.0, 2.0 * modAcc[(size_t) ModTarget::loopHighpass]));
            const auto lp = juce::jlimit (20.0, sampleRate * 0.45,
                                          (double) settings.lowpassHz
                                              * std::pow (2.0, 2.0 * modAcc[(size_t) ModTarget::loopLowpass]));

            const auto hpCoefficients = dsp::BiquadCoefficients::highPass (sampleRate, hp, 0.707);
            const auto lpCoefficients = dsp::BiquadCoefficients::lowPass (sampleRate, lp, 0.707);

            for (auto& line : lines)
            {
                line.highpass.setCoefficients (hpCoefficients);
                line.lowpass.setCoefficients (lpCoefficients);
            }
        }

        const auto driveNow = juce::jlimit (0.0f, 1.0f,
                                            driveAmount + modAcc[(size_t) ModTarget::drive]);
        const auto loFiNow = juce::jlimit (0.0f, 1.0f,
                                           loFiAmount + modAcc[(size_t) ModTarget::loFi]);
        const auto driveGain = 1.0f + driveNow * 3.0f;   // up to +12 dB into the shaper

        std::array<float, 2> processed {}, rawRead {};

        /*  The pattern's steps are read here, alongside the loop's own read,
            and not further down where they are used. By the time the output is
            assembled the write head has already advanced, and reading then puts
            every step one sample early — which is exactly what it did until a
            test compared the steps against the quarters of the delay time they
            are supposed to land on.
        */
        std::array<std::array<float, numTaps>, 2> tapValues {};

        for (int i = 0; i < 2; ++i)
        {
            auto& line = lines[(size_t) i];
            const auto target = targetSamples[(size_t) i];

            /*  Two entirely different answers to "the time changed", and the
                switch between them is the whole tape-versus-digital question.
            */
            if (tape)
            {
                // The read head slides. Because it is moving through the buffer
                // at a rate other than one sample per sample, what comes out is
                // pitch shifted for as long as the move lasts — which is not a
                // trick added on top, it is what the geometry does.
                line.currentSamples = target + timeGlideCoeff * (line.currentSamples - target);
                line.fadePosition = 1.0f;
            }
            else
            {
                if (line.fadePosition >= 1.0f)
                {
                    if (std::abs (target - line.currentSamples) > 0.5f)
                    {
                        line.previousSamples = line.currentSamples;
                        line.currentSamples = target;
                        line.fadePosition = 0.0f;
                    }
                }
                else
                {
                    line.fadePosition = juce::jmin (1.0f, line.fadePosition + fadeIncrement);
                }
            }

            // Wow and flutter. Two sines at unrelated rates rather than one:
            // a single sine is heard as a vibrato, and tape is not that regular.
            line.lfoPhase += lfoIncrement;

            if (line.lfoPhase >= 1.0f)
                line.lfoPhase -= std::floor (line.lfoPhase);

            const auto twoPi = juce::MathConstants<float>::twoPi;
            const auto wobble = 0.72f * std::sin (twoPi * line.lfoPhase)
                              + 0.28f * std::sin (twoPi * line.lfoPhase * 0.27f + 1.1f);

            /*  Two things move the read head off its nominal distance, and
                they are added rather than chosen between: the wow, which is
                part of the machine, and whatever the matrix is pointing at the
                time, which is the user's idea. The matrix works in proportion
                to the delay rather than in milliseconds, so the same setting is
                a flutter on a slapback and a dive on a four second wash.
            */
            const auto matrixTime = modAcc[(size_t) ModTarget::time]
                                  + modAcc[(size_t) (i == 0 ? ModTarget::timeLeft
                                                            : ModTarget::timeRight)];

            const auto offset = modDepth * maxWowSamples[(size_t) i] * wobble
                              + matrixTime * 0.3f * line.currentSamples;

            const auto raw = line.fadePosition >= 1.0f
                           ? line.buffer.read (line.currentSamples + offset)
                           : equalPower (line.buffer.read (line.previousSamples + offset),
                                         line.buffer.read (line.currentSamples + offset),
                                         line.fadePosition);

            const auto readBase = line.currentSamples + offset;

            if (multiTap)
                for (int step = 1; step < steps; ++step)
                    if (settings.taps[(size_t) (step - 1)].on)
                        tapValues[(size_t) i][(size_t) (step - 1)] =
                            line.buffer.read (readBase * (float) step / (float) steps);

            /*  The loop's tone, applied on the way out so that what you hear
                and what goes round again are the same signal — each repeat is
                one generation further gone rather than all of them being
                processed once.

                Except while frozen: then what is written back is the untouched
                buffer contents, so you can sweep the filters over a held loop
                without eroding it a little more on every pass.
            */
            auto shaped = raw;

            if (filtersActive)
            {
                shaped = line.highpass.processSample (shaped);
                shaped = line.lowpass.processSample (shaped);
            }

            if (driveNow > 0.0f)
                shaped = line.saturator.process (shaped, driveGain);

            shaped = line.crusher.process (shaped, loFiNow);

            processed[(size_t) i] = shaped;
            rawRead[(size_t) i] = raw;

            const auto magnitude = std::abs (raw);
            line.level = magnitude + levelCoeff * (line.level - magnitude);
        }

        const auto feedback = juce::jlimit (0.0f, 1.2f,
                                            feedbackAmount.getNextValue()
                                                + modAcc[(size_t) ModTarget::feedback] * 0.5f);
        const auto cross = juce::jlimit (0.0f, 1.0f,
                                         crossAmount.getNextValue()
                                             + modAcc[(size_t) ModTarget::cross] * 0.5f);

        // What goes into each line: the input, according to the routing, plus
        // the feedback network.
        std::array<float, 2> injection {};

        switch (settings.routing)
        {
            case Routing::pingPong:
                injection[0] = (dryL + dryR) * 0.5f;
                injection[1] = 0.0f;
                break;

            case Routing::mono:
                injection[0] = injection[1] = (dryL + dryR) * 0.5f;
                break;

            case Routing::stereo:
            default:
                injection[0] = dryL;
                injection[1] = dryR;
                break;
        }

        for (int i = 0; i < 2; ++i)
        {
            auto& line = lines[(size_t) i];
            const auto other = 1 - i;

            if (freeze)
            {
                // Exactly what came out goes back in, with no safety clipper in
                // the way: the loop is at unity by definition, and squashing it
                // would make a held chord quietly change colour.
                line.buffer.write (rawRead[(size_t) i]);
            }
            else
            {
                const auto source = injection[(size_t) i]
                                  + feedback * invert
                                      * ((1.0f - cross) * processed[(size_t) i]
                                         + cross * processed[(size_t) other]);

                line.buffer.write (dsp::loopSafety (source));
            }

            line.buffer.advance();
            juce::ignoreUnused (other);
        }

        // Where each line sits in the picture, then the width of the result.
        auto wetL = 0.0f, wetR = 0.0f;

        if (multiTap)
        {
            /*  Every step of the pattern is a window into the same buffer. The
                last one lands on the delay time itself, which is where the loop
                closes, so it is the signal that has already been through the
                filters and the saturation; the earlier ones have not been round
                yet and are read raw. That is not an inconsistency, it is where
                the tone stage physically sits — a tap before it has not passed
                through it.
            */
            const auto spread = juce::jlimit (0.0f, 1.5f,
                                              settings.tapSpread
                                                  + modAcc[(size_t) ModTarget::tapSpread]);
            const auto levelScale = juce::jlimit (0.0f, 2.0f,
                                                  1.0f + modAcc[(size_t) ModTarget::tapLevel]);

            for (int i = 0; i < 2; ++i)
            {
                for (int step = 1; step <= steps; ++step)
                {
                    const auto& tap = settings.taps[(size_t) (step - 1)];

                    if (! tap.on || tap.level <= 0.0f)
                        continue;

                    const auto value = step == steps
                                     ? processed[(size_t) i]
                                     : tapValues[(size_t) i][(size_t) (step - 1)];

                    float left = 0.0f, right = 0.0f;
                    panGains (juce::jlimit (-1.0f, 1.0f, tap.pan * spread), left, right);

                    const auto gain = tap.level * levelScale * tapNormalise;

                    wetL += value * gain * left;
                    wetR += value * gain * right;
                }
            }
        }
        else
        {
            wetL = processed[0] * panGain[0][0] + processed[1] * panGain[1][0];
            wetR = processed[0] * panGain[0][1] + processed[1] * panGain[1][1];
        }

        const auto width = juce::jlimit (0.0f, 2.5f,
                                         widthAmount.getNextValue()
                                             + modAcc[(size_t) ModTarget::width] * 0.5f);
        const auto mid = (wetL + wetR) * 0.5f;
        const auto side = (wetL - wetR) * 0.5f * width;

        wetL = mid + side;
        wetR = mid - side;

        const auto wet = juce::jlimit (0.0f, 1.0f,
                                       mixAmount.getNextValue()
                                           + modAcc[(size_t) ModTarget::mix] * 0.5f);
        const auto dry = 1.0f - wet;

        audio[0][n] = dryL * dry + wetL * wet;

        if (! mono)
            audio[1][n] = dryR * dry + wetR * wet;
    }

    for (int i = 0; i < 2; ++i)
    {
        auto& line = lines[(size_t) i];

        line.highpass.snapToZero();
        line.lowpass.snapToZero();

        currentTimeMs[(size_t) i].store ((float) (line.currentSamples / sampleRate * 1000.0),
                                         std::memory_order_relaxed);
        loopLevel[(size_t) i].store (line.level, std::memory_order_relaxed);
    }

    for (int i = 0; i < 3; ++i)
        sourceValue[(size_t) i].store (lastSources[(size_t) i], std::memory_order_relaxed);
}
} // namespace nodo::delay
