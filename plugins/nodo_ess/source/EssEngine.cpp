#include "EssEngine.h"

namespace nodo::ess
{
namespace
{
    constexpr float maximumLookaheadMs = 15.0f;
    constexpr float butterworthQ = 0.70710678118654752f;

    // How fast the adaptive threshold follows the material: quick enough to
    // track a take, far too slow to follow a syllable.
    constexpr float adaptiveRiseSeconds = 0.40f;
    constexpr float adaptiveFallSeconds = 1.50f;

    /** In adaptive mode the threshold knob stops being a level and becomes a
        distance above the band's own running level. The offset puts the default
        setting a few decibels above it, which is where an S sits.
    */
    constexpr float adaptiveReferenceDb = 30.0f;

    inline float toDecibels (float linear) noexcept
    {
        return 20.0f * std::log10 (juce::jmax (1.0e-6f, linear));
    }

    inline float fromDecibels (float db) noexcept
    {
        return std::pow (10.0f, db * 0.05f);
    }
}

void EssEngine::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;
    maxBlockSize = (int) juce::jmax (1u, spec.maximumBlockSize);

    delayLength = (int) std::ceil (maximumLookaheadMs * 0.001 * sampleRate) + maxBlockSize + 8;

    for (auto& buffer : lowDelay)  buffer.assign ((size_t) delayLength, 0.0f);
    for (auto& buffer : highDelay) buffer.assign ((size_t) delayLength, 0.0f);

    for (auto& smoother : smoothers)
        smoother.prepare (sampleRate);

    slowRiseCoeff = std::exp (-1.0f / (adaptiveRiseSeconds * (float) sampleRate));
    slowFallCoeff = std::exp (-1.0f / (adaptiveFallSeconds * (float) sampleRate));

    reset();
}

void EssEngine::reset()
{
    for (auto& buffer : lowDelay)  std::fill (buffer.begin(), buffer.end(), 0.0f);
    for (auto& buffer : highDelay) std::fill (buffer.begin(), buffer.end(), 0.0f);

    delayWritePos = 0;

    for (auto& half : lowBand)  half.reset();
    for (auto& half : highBand) half.reset();
    for (auto& filter : detectorTop) filter.reset();
    for (auto& smoother : smoothers) smoother.reset();

    slowLevelDb.fill (-100.0f);
    settingsValid = false;

    reductionDb.store (0.0f, std::memory_order_relaxed);
    detectorDb.store (-100.0f, std::memory_order_relaxed);
}

int EssEngine::computeLatencySamples() const noexcept
{
    return (int) std::round (pending.lookaheadMs * 0.001 * sampleRate);
}

void EssEngine::applyPendingSettings()
{
    settings = pending;

    const auto frequency = juce::jlimit (20.0, sampleRate * 0.45, (double) settings.frequencyHz);

    const auto lowCoefficients  = dsp::BiquadCoefficients::lowPass  (sampleRate, frequency, butterworthQ);
    const auto highCoefficients = dsp::BiquadCoefficients::highPass (sampleRate, frequency, butterworthQ);

    // Sibilance is a band, not everything above a corner: without a top, a
    // cymbal or the air of a bright room drags the detector along with the S.
    detectorTopActive = settings.detectorTopHz < 19500.0f;

    const auto topCoefficients = dsp::BiquadCoefficients::lowPass (
        sampleRate,
        juce::jlimit (20.0, sampleRate * 0.45, (double) settings.detectorTopHz),
        butterworthQ);

    for (int ch = 0; ch < 2; ++ch)
    {
        lowBand[(size_t) ch].setCoefficients (lowCoefficients);
        highBand[(size_t) ch].setCoefficients (highCoefficients);
        detectorTop[(size_t) ch].setCoefficients (topCoefficients);

        smoothers[(size_t) ch].setTimes (settings.attackMs, settings.releaseMs);
        smoothers[(size_t) ch].setProgramDependence (0.0f);
        smoothers[(size_t) ch].setDualStage (0.0f);
        smoothers[(size_t) ch].setHold (0.0f);
    }

    lookaheadSamples = juce::jlimit (0, delayLength - maxBlockSize - 2,
                                     (int) std::round (settings.lookaheadMs * 0.001 * sampleRate));

    settingsValid = true;
}

void EssEngine::process (juce::dsp::AudioBlock<float>& block)
{
    applyPendingSettings();

    const auto numSamples = (int) block.getNumSamples();
    const auto channels = (int) juce::jmin ((size_t) 2, block.getNumChannels());

    if (numSamples <= 0 || channels <= 0)
        return;

    const auto range = juce::jmax (0.0f, settings.rangeDb);
    const auto knee = juce::jmax (0.0f, settings.kneeDb);
    const auto link = juce::jlimit (0.0f, 1.0f, settings.stereoLink);
    const auto split = settings.mode == Mode::split;
    const auto adaptive = settings.detection == Detection::adaptive;

    /*  Mid/side is done by rotating at the door and rotating back at the end,
        so everything in between is unchanged: the crossover runs on both
        components rather than on left and right. Both still get the same all
        pass treatment, which is what keeps the sum transparent — filtering only
        the component being de-essed would leave the other one arriving with a
        different phase and comb the recombination.
    */
    const auto midSide = settings.channel != Channel::stereo && channels > 1;
    const auto workedOn = settings.channel == Channel::side ? 1 : 0;

    std::array<float*, 2> audio { nullptr, nullptr };

    for (int ch = 0; ch < channels; ++ch)
        audio[(size_t) ch] = block.getChannelPointer ((size_t) ch);

    auto worstReduction = 0.0f;
    auto lastDetector = -100.0f;
    auto lastThreshold = settings.thresholdDb;
    auto lastReference = settings.thresholdDb;

    for (int i = 0; i < numSamples; ++i)
    {
        std::array<float, 2> low { 0.0f, 0.0f }, high { 0.0f, 0.0f }, detector { 0.0f, 0.0f };

        // What the two working "channels" are: left and right, or mid and side.
        std::array<float, 2> source { 0.0f, 0.0f };

        for (int ch = 0; ch < channels; ++ch)
            source[(size_t) ch] = audio[(size_t) ch][i];

        if (midSide)
        {
            const auto mid  = (source[0] + source[1]) * 0.5f;
            const auto side = (source[0] - source[1]) * 0.5f;
            source[0] = mid;
            source[1] = side;
        }

        for (int ch = 0; ch < channels; ++ch)
        {
            low[(size_t) ch]  = lowBand[(size_t) ch].process (source[(size_t) ch]);
            high[(size_t) ch] = highBand[(size_t) ch].process (source[(size_t) ch]);

            // The detector reads the band undelayed, which is what lookahead
            // buys: the gain can be moving before the sibilant arrives. Its own
            // low pass narrows the watch to the sibilant region and never
            // touches the audio.
            const auto watched = detectorTopActive
                               ? detectorTop[(size_t) ch].processSample (high[(size_t) ch])
                               : high[(size_t) ch];

            detector[(size_t) ch] = std::abs (watched);
        }

        if (channels == 1)
        {
            low[1] = low[0];
            high[1] = high[0];
            detector[1] = detector[0];
        }

        // Linking is about two sides of one image, so it means nothing once the
        // two working channels are mid and side: those are different signals,
        // not two halves of the same one.
        if (link > 0.0f && ! midSide)
        {
            const auto loudest = juce::jmax (detector[0], detector[1]);

            for (auto& value : detector)
                value += link * (loudest - value);
        }

        std::array<float, 2> gain { 1.0f, 1.0f };

        for (int ch = 0; ch < channels; ++ch)
        {
            const auto levelDb = toDecibels (detector[(size_t) ch]);

            auto& slow = slowLevelDb[(size_t) ch];
            const auto coeff = levelDb > slow ? slowRiseCoeff : slowFallCoeff;
            slow = levelDb + coeff * (slow - levelDb);

            if (! std::isfinite (slow))
                slow = -100.0f;

            const auto threshold = adaptive
                                 ? juce::jlimit (-90.0f, 0.0f,
                                                 slow + settings.thresholdDb + adaptiveReferenceDb)
                                 : settings.thresholdDb;

            /*  An infinite ratio: the band is brought back down to the
                threshold, and the range says how far that is allowed to go. That
                is what a de-esser does — it is not shaping dynamics, it is
                taking a specific sound out of the way.
            */
            const auto wanted = juce::jmax (-range,
                                            dsp::computeStaticGainDb (levelDb, threshold, 10000.0f, knee));

            const auto smoothed = smoothers[(size_t) ch].process (wanted);
            gain[(size_t) ch] = fromDecibels (smoothed);

            // In mid/side the other component is not being de-essed, so neither
            // its reduction nor its level belongs on the meters.
            const auto reported = midSide ? ch == workedOn : ch == 0;

            if (smoothed < worstReduction && (! midSide || ch == workedOn))
                worstReduction = smoothed;

            if (reported)
            {
                lastDetector = levelDb;
                lastThreshold = threshold;
                lastReference = slow + adaptiveReferenceDb;
            }
        }

        if (channels == 1)
            gain[1] = gain[0];

        // In mid/side only the chosen component is touched; the other one goes
        // through the same delay and the same all pass, untouched.
        if (midSide)
            gain[(size_t) (1 - workedOn)] = 1.0f;

        std::array<float, 2> processedOut { 0.0f, 0.0f }, dryOut { 0.0f, 0.0f },
                             bandOut { 0.0f, 0.0f };

        for (int ch = 0; ch < channels; ++ch)
        {
            auto& lowBuffer = lowDelay[(size_t) ch];
            auto& highBuffer = highDelay[(size_t) ch];

            lowBuffer[(size_t) delayWritePos] = low[(size_t) ch];
            highBuffer[(size_t) delayWritePos] = high[(size_t) ch];

            auto readPos = delayWritePos - lookaheadSamples;

            if (readPos < 0)
                readPos += delayLength;

            const auto delayedLow = lowBuffer[(size_t) readPos];
            const auto delayedHigh = highBuffer[(size_t) readPos];

            // Fourth order Linkwitz-Riley: the two halves sum back to the input
            // with a flat magnitude, so with no reduction this is transparent.
            const auto dry = delayedLow + delayedHigh;

            const auto processed = split
                                 ? delayedLow + delayedHigh * gain[(size_t) ch]
                                 : dry * gain[(size_t) ch];

            processedOut[(size_t) ch] = processed;
            dryOut[(size_t) ch]       = dry;
            bandOut[(size_t) ch]      = delayedHigh;
        }

        /*  Back out of mid/side. The monitor choice is made on the working
            components and then rotated, not the other way round: "listen to the
            difference" has to mean the difference in the same domain the gain
            was applied in, or the null is not a null.
        */
        if (midSide)
        {
            const auto rotate = [] (std::array<float, 2>& v)
            {
                const auto left  = v[0] + v[1];
                const auto right = v[0] - v[1];
                v[0] = left;
                v[1] = right;
            };

            rotate (processedOut);
            rotate (dryOut);
            rotate (bandOut);
        }

        for (int ch = 0; ch < channels; ++ch)
        {
            switch (settings.monitor)
            {
                case Monitor::band:
                    audio[(size_t) ch][i] = bandOut[(size_t) ch];
                    break;

                case Monitor::difference:
                    audio[(size_t) ch][i] = dryOut[(size_t) ch] - processedOut[(size_t) ch];
                    break;

                case Monitor::normal:
                default:
                    audio[(size_t) ch][i] = processedOut[(size_t) ch];
                    break;
            }
        }

        if (++delayWritePos >= delayLength)
            delayWritePos = 0;
    }

    for (int ch = 0; ch < 2; ++ch)
    {
        lowBand[(size_t) ch].snapToZero();
        highBand[(size_t) ch].snapToZero();
        detectorTop[(size_t) ch].snapToZero();
    }

    reductionDb.store (worstReduction, std::memory_order_relaxed);
    detectorDb.store (lastDetector, std::memory_order_relaxed);
    effectiveThresholdDb.store (lastThreshold, std::memory_order_relaxed);
    adaptiveReference.store (lastReference, std::memory_order_relaxed);
}
} // namespace nodo::ess
