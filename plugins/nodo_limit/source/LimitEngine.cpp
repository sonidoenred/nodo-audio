#include "LimitEngine.h"

namespace nodo::limit
{
namespace
{
    constexpr float maximumLookaheadMs = 20.0f;
    constexpr int truePeakFactorLog2 = 2;    // 4x, as BS.1770 measures it

    inline float toDecibels (float linear) noexcept
    {
        return 20.0f * std::log10 (juce::jmax (1.0e-7f, linear));
    }

    inline float fromDecibels (float db) noexcept
    {
        return std::pow (10.0f, db * 0.05f);
    }
}

void LimitEngine::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;
    maxBlockSize = (int) juce::jmax (1u, spec.maximumBlockSize);

    const auto maxLookahead = (int) std::ceil (maximumLookaheadMs * 0.001 * sampleRate);

    truePeakOversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) 2, truePeakFactorLog2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);

    truePeakOversampler->initProcessing ((size_t) maxBlockSize);
    truePeakOversampler->reset();

    truePeakLatency = (int) std::ceil (truePeakOversampler->getLatencyInSamples());

    delayLength = maxLookahead + truePeakLatency + maxBlockSize + 8;

    for (auto& buffer : delayBuffer)
        buffer.assign ((size_t) delayLength, 0.0f);

    for (auto& hold : minimumHold)
        hold.prepare (maxLookahead + 2);

    for (auto& average : smoother1) average.prepare (maxLookahead + 2);
    for (auto& average : smoother2) average.prepare (maxLookahead + 2);

    // A gentle high pass, well below anything musical: this is for removing an
    // offset that eats headroom, not for shaping the bottom end.
    const auto dc = dsp::BiquadCoefficients::highPass (sampleRate, 12.0, 0.707);

    for (auto& filter : dcBlocker)
        filter.setCoefficients (dc);

    detectorBuffer.setSize (2, maxBlockSize, false, false, true);
    peakBuffer.setSize (2, maxBlockSize, false, false, true);

    inputGain.reset (sampleRate, 0.02);
    averagingCoeff = std::exp (-1.0f / (1.5f * (float) sampleRate));

    reset();
}

void LimitEngine::reset()
{
    for (auto& buffer : delayBuffer)
        std::fill (buffer.begin(), buffer.end(), 0.0f);

    delayWritePos = 0;

    for (auto& hold : minimumHold) hold.reset();
    for (auto& filter : dcBlocker) filter.reset();

    for (auto& average : smoother1) { average.reset(); average.primeWith (1.0f); }
    for (auto& average : smoother2) { average.reset(); average.primeWith (1.0f); }

    releaseEnvelope.fill (1.0f);
    slowEnvelope.fill (1.0f);

    if (truePeakOversampler != nullptr)
        truePeakOversampler->reset();

    settingsValid = false;

    runningReduction = 0.0f;
    reductionDb.store (0.0f, std::memory_order_relaxed);
    averageReductionDb.store (0.0f, std::memory_order_relaxed);
    peakDb.store (-200.0f, std::memory_order_relaxed);
    clipCount.store (0, std::memory_order_relaxed);
}

int LimitEngine::computeLatencySamples() const noexcept
{
    const auto lookahead = (int) std::round (pending.lookaheadMs * 0.001 * sampleRate);

    return lookahead + (pending.truePeak ? truePeakLatency : 0);
}

void LimitEngine::applyPendingSettings()
{
    const auto traits = styleTraits (pending.style);

    settings = pending;

    lookaheadSamples = juce::jlimit (1, delayLength - maxBlockSize - 4,
                                     (int) std::round (settings.lookaheadMs * 0.001 * sampleRate));

    latencySamples = lookaheadSamples + (settings.truePeak ? truePeakLatency : 0);

    // The window the gain is allowed to fall over. Shorter than the lookahead
    // means the gain is fully down before the peak arrives, with a sharper
    // corner on the way in: that is what the punchier styles trade.
    const auto smoothing = juce::jmax (2, (int) std::round (traits.smoothingFraction
                                                            * (float) lookaheadSamples));
    const auto half = juce::jmax (1, smoothing / 2);

    for (int ch = 0; ch < 2; ++ch)
    {
        minimumHold[(size_t) ch].setWindow (lookaheadSamples + 1);
        smoother1[(size_t) ch].setLength (half);
        smoother2[(size_t) ch].setLength (half);
    }

    const auto releaseMs = juce::jmax (1.0f, settings.releaseMs * traits.releaseScale);
    releaseCoeff = std::exp (-1.0f / (releaseMs * 0.001f * (float) sampleRate));
    slowCoeff = std::exp (-1.0f / (releaseMs * 4.0f * 0.001f * (float) sampleRate));

    const auto gainTarget = fromDecibels (settings.inputGainDb);

    if (settingsValid)
        inputGain.setTargetValue (gainTarget);
    else
        inputGain.setCurrentAndTargetValue (gainTarget);

    settingsValid = true;
}

void LimitEngine::measureTruePeaks (int numSamples, int channels)
{
    // Four times oversampled magnitude, which is how a converter's
    // reconstruction filter will see it and how BS.1770 defines true peak. The
    // oversampler's own latency is added to the audio delay, so the peak the
    // gain path sees still lines up with the sample it belongs to.
    juce::dsp::AudioBlock<float> detectorBlock (detectorBuffer);
    auto upsampled = truePeakOversampler->processSamplesUp (
        detectorBlock.getSubBlock (0, (size_t) numSamples));

    const auto factor = 1 << truePeakFactorLog2;

    for (int ch = 0; ch < channels; ++ch)
    {
        const auto* source = upsampled.getChannelPointer ((size_t) ch);
        auto* destination = peakBuffer.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            auto peak = 0.0f;

            for (int k = 0; k < factor; ++k)
                peak = juce::jmax (peak, std::abs (source[i * factor + k]));

            destination[i] = peak;
        }
    }
}

void LimitEngine::process (juce::dsp::AudioBlock<float>& block,
                           const juce::dsp::AudioBlock<const float>* sidechain)
{
    applyPendingSettings();

    const auto numSamples = (int) block.getNumSamples();
    const auto channels = (int) juce::jmin ((size_t) 2, block.getNumChannels());

    if (numSamples <= 0 || channels <= 0)
        return;

    const auto traits = styleTraits (settings.style);
    const auto ceiling = fromDecibels (settings.ceilingDb);

    /*  The gain is computed against a ceiling a hair under the real one. The
        arithmetic is exact in principle — the gain applied to a sample is never
        larger than the gain that sample demanded — but "never larger" in float
        means "never larger by more than an ulp", and an ulp over the ceiling is
        still over the ceiling. A millionth of a decibel of margin makes the
        promise hold in floating point, and keeps the safety clipper at zero
        where it belongs.
    */
    const auto ceilingForGain = ceiling * (1.0f - 1.0e-6f);
    const auto transientLink = juce::jlimit (0.0f, 1.0f, settings.transientLink);
    const auto releaseLink = juce::jlimit (0.0f, 1.0f, settings.releaseLink);

    const auto haveExternal = sidechain != nullptr
                           && settings.externalTrigger
                           && sidechain->getNumChannels() > 0
                           && sidechain->getNumSamples() >= (size_t) numSamples;

    std::array<float*, 2> audio { nullptr, nullptr };

    for (int ch = 0; ch < channels; ++ch)
        audio[(size_t) ch] = block.getChannelPointer ((size_t) ch);

    // ---- input gain and DC ------------------------------------------------
    for (int i = 0; i < numSamples; ++i)
    {
        const auto gain = inputGain.getNextValue();

        for (int ch = 0; ch < channels; ++ch)
        {
            auto sample = audio[(size_t) ch][i] * gain;

            if (settings.dcFilter)
                sample = dcBlocker[(size_t) ch].processSample (sample);

            audio[(size_t) ch][i] = sample;
        }
    }

    if (settings.dcFilter)
        for (auto& filter : dcBlocker)
            filter.snapToZero();

    // ---- what the gain is computed from -----------------------------------
    detectorBuffer.setSize (2, juce::jmax (numSamples, 1), false, false, true);
    peakBuffer.setSize (2, juce::jmax (numSamples, 1), false, false, true);

    for (int ch = 0; ch < 2; ++ch)
    {
        const auto sourceChannel = juce::jmin (ch, channels - 1);

        if (haveExternal)
        {
            const auto scChannel = juce::jmin ((size_t) sourceChannel,
                                               sidechain->getNumChannels() - 1);
            const auto* source = sidechain->getChannelPointer (scChannel);

            for (int i = 0; i < numSamples; ++i)
                detectorBuffer.setSample (ch, i, source[i]);
        }
        else
        {
            const auto* source = audio[(size_t) sourceChannel];

            for (int i = 0; i < numSamples; ++i)
                detectorBuffer.setSample (ch, i, source[i]);
        }
    }

    if (settings.truePeak && truePeakOversampler != nullptr)
    {
        measureTruePeaks (numSamples, 2);
    }
    else
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* source = detectorBuffer.getReadPointer (ch);
            auto* destination = peakBuffer.getWritePointer (ch);

            for (int i = 0; i < numSamples; ++i)
                destination[i] = std::abs (source[i]);
        }
    }

    // ---- the gain path ----------------------------------------------------
    auto deepest = 0.0f;
    auto loudestOut = 0.0f;
    juce::int64 clipped = 0;

    for (int i = 0; i < numSamples; ++i)
    {
        std::array<float, 2> gain { 1.0f, 1.0f };

        // Required gain, per channel, then linked. Linking the transient path
        // is what keeps a peak on one side from pulling the image over.
        std::array<float, 2> required { 1.0f, 1.0f };

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto peak = peakBuffer.getSample (ch, i);
            required[(size_t) ch] = peak > ceilingForGain ? ceilingForGain / peak : 1.0f;
        }

        if (transientLink > 0.0f)
        {
            const auto lowest = juce::jmin (required[0], required[1]);

            for (auto& value : required)
                value += transientLink * (lowest - value);
        }

        for (int ch = 0; ch < channels; ++ch)
        {
            // Sliding minimum: the gain the next lookahead window is going to
            // demand, available now.
            const auto held = minimumHold[(size_t) ch].process (required[(size_t) ch]);

            // Release: instant on the way down, exponential on the way up.
            auto& fast = releaseEnvelope[(size_t) ch];
            auto& slow = slowEnvelope[(size_t) ch];

            auto coeff = releaseCoeff;

            if (traits.programDepend > 0.0f)
            {
                // Deeper limiting lets go more slowly, which is what stops a
                // heavily driven master from breathing on every bar.
                const auto depth = juce::jlimit (0.0f, 12.0f, -toDecibels (fast));
                const auto stretch = 1.0f + traits.programDepend * depth / 6.0f;
                coeff = std::exp (std::log (juce::jmax (1.0e-6f, releaseCoeff)) / stretch);
            }

            fast = juce::jmin (held, 1.0f - (1.0f - fast) * coeff);
            slow = juce::jmin (held, 1.0f - (1.0f - slow) * slowCoeff);

            const auto released = traits.dualStage > 0.0f
                                ? fast + traits.dualStage * (juce::jmin (fast, slow) - fast)
                                : fast;

            // Two boxcars turn the staircase into a ramp with no corner at
            // either end. This is the only part of the chain that can raise the
            // gain above what the peak allows, so the minimum is applied again
            // afterwards.
            const auto smoothed = smoother2[(size_t) ch].process (
                                      smoother1[(size_t) ch].process (released));

            gain[(size_t) ch] = juce::jmin (smoothed, held);
        }

        if (channels == 1)
            gain[1] = gain[0];

        if (releaseLink > 0.0f)
        {
            const auto lowest = juce::jmin (gain[0], gain[1]);

            for (auto& value : gain)
                value += releaseLink * (lowest - value);
        }

        // ---- apply --------------------------------------------------------
        for (int ch = 0; ch < channels; ++ch)
        {
            auto& buffer = delayBuffer[(size_t) ch];

            buffer[(size_t) delayWritePos] = audio[(size_t) ch][i];

            auto readPos = delayWritePos - latencySamples;

            if (readPos < 0)
                readPos += delayLength;

            const auto delayed = buffer[(size_t) readPos];
            auto out = delayed * gain[(size_t) ch];

            // Dither, when the output is going to a fixed point file. TPDF: two
            // uniform draws, which gives a triangular distribution and a noise
            // floor that does not modulate with the signal.
            if (settings.dither != DitherMode::off)
            {
                const auto bits = settings.dither == DitherMode::sixteenBit ? 16 : 24;
                const auto step = std::pow (2.0f, -(float) (bits - 1));

                out += (ditherNoise.nextFloat() - ditherNoise.nextFloat()) * step;
            }

            // Safety. Nothing above should ever let a sample past the ceiling;
            // this is here so that "should" is not the last word, and the count
            // is exposed so the tests can insist it stays at zero.
            if (out > ceiling)       { out = ceiling;  ++clipped; }
            else if (out < -ceiling) { out = -ceiling; ++clipped; }

            if (settings.delta)
                out = delayed - out;

            audio[(size_t) ch][i] = out;
            loudestOut = juce::jmax (loudestOut, std::abs (out));
        }

        const auto worst = juce::jmin (gain[0], gain[1]);
        const auto worstDb = worst < 1.0f ? toDecibels (worst) : 0.0f;

        if (worst < 1.0f)
            deepest = juce::jmin (deepest, worstDb);

        runningReduction = worstDb + averagingCoeff * (runningReduction - worstDb);

        if (++delayWritePos >= delayLength)
            delayWritePos = 0;
    }

    reductionDb.store (deepest, std::memory_order_relaxed);
    averageReductionDb.store (runningReduction, std::memory_order_relaxed);

    const auto outDb = toDecibels (loudestOut);

    if (outDb > peakDb.load (std::memory_order_relaxed))
        peakDb.store (outDb, std::memory_order_relaxed);

    if (clipped > 0)
        clipCount.store (clipCount.load (std::memory_order_relaxed) + clipped,
                         std::memory_order_relaxed);
}
} // namespace nodo::limit
