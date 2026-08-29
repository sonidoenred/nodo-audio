#include "CompEngine.h"

namespace nodo::comp
{
namespace
{
    constexpr float minimumLevel = 1.0e-6f;      // -120 dB, the detector's floor
    constexpr float autoGainSeconds = 1.5f;

    // How fast the auto threshold follows the material. Slow enough that it
    // tracks the take rather than the syllable.
    constexpr float autoThresholdRiseSeconds = 0.30f;
    constexpr float autoThresholdFallSeconds = 1.20f;

    /** With auto threshold on, the knob stops being an absolute level and
        becomes an offset from the running loudness. The 18 dB puts the default
        threshold right on the average, so switching it on does not jump.
    */
    constexpr float autoThresholdReferenceDb = 18.0f;

    inline float toDecibels (float linear) noexcept
    {
        return 20.0f * std::log10 (juce::jmax (minimumLevel, linear));
    }

    inline float fromDecibels (float db) noexcept
    {
        return std::pow (10.0f, db * 0.05f);
    }

    /** The ratio control reads "inf:1" at the top of its travel, and the gain
        computer wants a number it can treat as infinite.
    */
    inline float effectiveRatio (float ratio) noexcept
    {
        return ratio >= 19.5f ? 10000.0f : ratio;
    }
}

void CompEngine::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;
    maxBlockSize = (int) juce::jmax (1u, spec.maximumBlockSize);
    numChannels = (int) juce::jlimit (1u, 2u, spec.numChannels);

    // Room for the longest lookahead the parameter allows, plus one block so a
    // read never has to wrap twice.
    delayLength = (int) std::ceil (0.021 * sampleRate) + maxBlockSize + 4;

    for (auto& buffer : delayBuffer)
        buffer.assign ((size_t) delayLength, 0.0f);

    for (auto& follower : rmsFollowers)
        follower.prepare (sampleRate);

    for (auto& smoother : smoothers)
        smoother.prepare (sampleRate);

    makeupGain.reset (sampleRate, 0.02);
    mixAmount.reset (sampleRate, 0.02);

    autoGainCoeff = std::exp (-1.0f / (autoGainSeconds * (float) sampleRate));
    slowRiseCoeff = std::exp (-1.0f / (autoThresholdRiseSeconds * (float) sampleRate));
    slowFallCoeff = std::exp (-1.0f / (autoThresholdFallSeconds * (float) sampleRate));

    reset();
}

void CompEngine::reset()
{
    for (auto& buffer : delayBuffer)
        std::fill (buffer.begin(), buffer.end(), 0.0f);

    delayWritePos = 0;

    for (auto& follower : rmsFollowers) follower.reset();
    for (auto& smoother : smoothers)    smoother.reset();
    for (auto& filter : scHighpass)     filter.reset();
    for (auto& filter : scLowpass)      filter.reset();

    feedbackMemory.fill (0.0f);
    autoGainDb = 0.0f;
    slowLevelDb = -100.0f;
    settingsValid = false;

    reductionDb.store (0.0f, std::memory_order_relaxed);
    detectorDb.store (-100.0f, std::memory_order_relaxed);
}

int CompEngine::computeLatencySamples() const noexcept
{
    return (int) std::round (pending.lookaheadMs * 0.001 * sampleRate);
}

void CompEngine::updateSidechainFilters()
{
    scFiltersActive = sidechainFiltersActive (settings);

    if (! scFiltersActive)
        return;

    const auto hp = dsp::BiquadCoefficients::highPass (sampleRate,
                                                       juce::jlimit (20.0, sampleRate * 0.45,
                                                                     (double) settings.scHighpassHz),
                                                       0.707);
    const auto lp = dsp::BiquadCoefficients::lowPass (sampleRate,
                                                      juce::jlimit (20.0, sampleRate * 0.45,
                                                                    (double) settings.scLowpassHz),
                                                      0.707);

    for (int ch = 0; ch < 2; ++ch)
    {
        scHighpass[(size_t) ch].setCoefficients (hp);
        scLowpass[(size_t) ch].setCoefficients (lp);
    }
}

void CompEngine::applyPendingSettings()
{
    const auto traits = styleTraits (pending.style);

    settings = pending;

    for (int ch = 0; ch < 2; ++ch)
    {
        auto& smoother = smoothers[(size_t) ch];

        smoother.setTimes (settings.attackMs * traits.attackScale,
                           settings.releaseMs * traits.releaseScale);
        smoother.setHold (settings.holdMs);

        // Auto release overrides whatever the style asked for: the point of the
        // switch is that the release stops being a number and starts being a
        // reaction to the material.
        smoother.setProgramDependence (settings.autoRelease ? 0.90f : traits.programDepend);
        smoother.setDualStage (settings.autoRelease ? juce::jmax (0.45f, traits.dualStage)
                                                    : traits.dualStage);

        // The RMS window is tied to the attack time: a detector that averages
        // for longer than the gain takes to move would make the attack control
        // a lie.
        const auto window = juce::jlimit (1.0f, 100.0f,
                                          settings.attackMs * 2.0f * traits.rmsScale);
        rmsFollowers[(size_t) ch].setWindow (window);
    }

    updateSidechainFilters();

    // On the very first block these have to jump rather than ramp. A
    // SmoothedValue starts at zero, so ramping would fade the plugin in over
    // 20 ms every time playback starts — silent in a test that only looks at
    // steady state, and audible on every take.
    const auto makeupTarget = fromDecibels (settings.makeupDb);
    const auto mixTarget = juce::jlimit (0.0f, 1.0f, settings.mix);

    if (settingsValid)
    {
        makeupGain.setTargetValue (makeupTarget);
        mixAmount.setTargetValue (mixTarget);
    }
    else
    {
        makeupGain.setCurrentAndTargetValue (makeupTarget);
        mixAmount.setCurrentAndTargetValue (mixTarget);
    }

    lookaheadSamples = juce::jlimit (0, delayLength - maxBlockSize - 2,
                                     (int) std::round (settings.lookaheadMs * 0.001 * sampleRate));

    settingsValid = true;
}

float CompEngine::staticOutputDb (float inputDb, const CompSettings& s)
{
    const auto traits = styleTraits (s.style);
    const auto knee = s.kneeDb * traits.kneeScale;
    const auto ratio = effectiveRatio (s.ratio);

    const auto reduction = s.direction == Direction::upward
                         ? juce::jlimit (0.0f, s.rangeDb,
                                         dsp::computeUpwardGainDb (inputDb, s.thresholdDb, ratio, knee))
                         : juce::jlimit (-s.rangeDb, 0.0f,
                                         dsp::computeStaticGainDb (inputDb, s.thresholdDb, ratio, knee));

    const auto wet = fromDecibels (inputDb + reduction);
    const auto dry = fromDecibels (inputDb);
    const auto mixed = s.mix * wet + (1.0f - s.mix) * dry;

    return toDecibels (mixed) + s.makeupDb;
}

void CompEngine::process (juce::dsp::AudioBlock<float>& block,
                          const juce::dsp::AudioBlock<const float>* sidechain)
{
    applyPendingSettings();

    const auto numSamples = (int) block.getNumSamples();
    const auto channels = (int) juce::jmin ((size_t) 2, block.getNumChannels());

    if (numSamples <= 0 || channels <= 0)
        return;

    const auto traits = styleTraits (settings.style);
    const auto ratio = effectiveRatio (settings.ratio);
    const auto knee = settings.kneeDb * traits.kneeScale;
    const auto upward = settings.direction == Direction::upward;
    const auto range = juce::jmax (0.0f, settings.rangeDb);
    const auto link = juce::jlimit (0.0f, 1.0f, settings.stereoLink);
    const auto linked = link >= 0.999f;
    const auto useRms = settings.detection == Detection::rms;

    const auto haveExternal = sidechain != nullptr
                           && settings.sidechain == SidechainSource::external
                           && sidechain->getNumChannels() > 0
                           && sidechain->getNumSamples() >= (size_t) numSamples;

    auto worstReduction = 0.0f;
    auto lastDetectorDb = -100.0f;

    // Channel pointers are fetched once: getSample() on an AudioBlock is a
    // pointer walk per call, and this loop runs per sample per channel.
    std::array<float*, 2> out { nullptr, nullptr };
    std::array<const float*, 2> scIn { nullptr, nullptr };

    for (int ch = 0; ch < channels; ++ch)
    {
        out[(size_t) ch] = block.getChannelPointer ((size_t) ch);

        if (haveExternal)
        {
            const auto scChannel = juce::jmin (sidechain->getNumChannels() - 1, (size_t) ch);
            scIn[(size_t) ch] = sidechain->getChannelPointer (scChannel);
        }
    }

    for (int i = 0; i < numSamples; ++i)
    {
        // --- what the detector listens to -------------------------------------
        std::array<float, 2> filtered { 0.0f, 0.0f };
        std::array<float, 2> detector { 0.0f, 0.0f };

        for (int ch = 0; ch < channels; ++ch)
        {
            auto source = traits.feedback ? feedbackMemory[(size_t) ch]
                        : haveExternal    ? scIn[(size_t) ch][i]
                                          : out[(size_t) ch][i];

            if (scFiltersActive)
            {
                source = scHighpass[(size_t) ch].processSample (source);
                source = scLowpass[(size_t) ch].processSample (source);
            }

            filtered[(size_t) ch] = source;
            detector[(size_t) ch] = useRms ? rmsFollowers[(size_t) ch].process (source)
                                           : std::abs (source);
        }

        if (channels == 1)
        {
            filtered[1] = filtered[0];
            detector[1] = detector[0];
        }

        // --- stereo link -------------------------------------------------------
        if (link > 0.0f)
        {
            const auto loudest = juce::jmax (detector[0], detector[1]);

            for (auto& d : detector)
                d += link * (loudest - d);
        }

        // --- gain computer and smoother ---------------------------------------
        std::array<float, 2> gain { 1.0f, 1.0f };

        {
            const auto levelDb = toDecibels (juce::jmax (detector[0], detector[1]));

            // Auto threshold. The follower rises faster than it falls so a
            // sustained passage moves it and a gap does not.
            const auto coeff = levelDb > slowLevelDb ? slowRiseCoeff : slowFallCoeff;
            slowLevelDb = levelDb + coeff * (slowLevelDb - levelDb);

            if (! std::isfinite (slowLevelDb))
                slowLevelDb = -100.0f;

            const auto threshold = settings.autoThreshold
                                 ? juce::jlimit (-60.0f, 0.0f,
                                                 slowLevelDb + settings.thresholdDb
                                                   + autoThresholdReferenceDb)
                                 : settings.thresholdDb;

            /*  The smoother always works on a negative number, whichever way the
                compressor is pointing: "more processing" has to mean the same
                direction to it, or attack and release swap over in upward mode.
                Upward gain is therefore negated on the way in and again on the
                way out.
            */
            auto targetFor = [&] (float db) noexcept
            {
                return upward
                     ? -juce::jlimit (0.0f, range,
                                      dsp::computeUpwardGainDb (db, threshold, ratio, knee))
                     :  juce::jlimit (-range, 0.0f,
                                      dsp::computeStaticGainDb (db, threshold, ratio, knee));
            };

            const auto leftDb = toDecibels (detector[0]);
            const auto target = targetFor (leftDb);
            const auto smoothed = smoothers[0].process (target);

            gain[0] = fromDecibels (upward ? -smoothed : smoothed);
            lastDetectorDb = leftDb;

            if (smoothed < worstReduction)
                worstReduction = smoothed;

            if (linked)
            {
                gain[1] = gain[0];

                // The second smoother is fed the same target rather than left
                // idle, so unlinking mid-signal does not jump.
                smoothers[1].process (target);
            }
            else
            {
                const auto rightSmoothed = smoothers[1].process (targetFor (toDecibels (detector[1])));

                gain[1] = fromDecibels (upward ? -rightSmoothed : rightSmoothed);

                if (rightSmoothed < worstReduction)
                    worstReduction = rightSmoothed;
            }
        }

        // --- auto gain ---------------------------------------------------------
        // Follows the reduction actually being applied, slowly, and hands it
        // back. Slow on purpose: a fast auto gain would undo the compression it
        // is compensating for.
        const auto appliedDb = juce::jmin (smoothers[0].getCurrentDb(),
                                           smoothers[1].getCurrentDb());
        const auto wantedAutoGain = settings.autoGain ? -appliedDb : 0.0f;

        autoGainDb = wantedAutoGain + autoGainCoeff * (autoGainDb - wantedAutoGain);

        const auto makeup = makeupGain.getNextValue() * fromDecibels (autoGainDb);
        const auto wetAmount = mixAmount.getNextValue();
        const auto dryAmount = 1.0f - wetAmount;

        // --- apply -------------------------------------------------------------
        for (int ch = 0; ch < channels; ++ch)
        {
            auto& buffer = delayBuffer[(size_t) ch];

            buffer[(size_t) delayWritePos] = out[(size_t) ch][i];

            auto readPos = delayWritePos - lookaheadSamples;

            if (readPos < 0)
                readPos += delayLength;

            // Both paths read the delayed signal, including the dry one: mixing
            // a delayed wet against an undelayed dry would comb the result.
            const auto delayed = buffer[(size_t) readPos];
            const auto wet = delayed * gain[(size_t) ch];

            // The feedback styles detect after the gain element and before
            // makeup, which is where the detector sits in the hardware they are
            // named after.
            feedbackMemory[(size_t) ch] = wet;

            out[(size_t) ch][i] = settings.scListen
                                ? filtered[(size_t) ch]
                                : (wet * wetAmount + delayed * dryAmount) * makeup;
        }

        if (++delayWritePos >= delayLength)
            delayWritePos = 0;
    }

    for (auto& filter : scHighpass) filter.snapToZero();
    for (auto& filter : scLowpass)  filter.snapToZero();

    reductionDb.store (worstReduction, std::memory_order_relaxed);
    detectorDb.store (lastDetectorDb, std::memory_order_relaxed);
}
} // namespace nodo::comp
