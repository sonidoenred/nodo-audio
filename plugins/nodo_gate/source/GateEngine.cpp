#include "GateEngine.h"

namespace nodo::gate
{
namespace
{
    constexpr float minimumLevel = 1.0e-7f;      // -140 dB, the detector's floor

    /*  The detector's own release, which is not the release knob.

        A rectified sine falls to zero twice per cycle, so a bare peak detector
        reads "silence" halfway through every cycle of a bass note and the gate
        chatters at the note's frequency. Filling those valleys is what a peak
        detector's release is for; 15 ms covers everything down to about 30 Hz
        and is short enough that the gate still starts closing promptly.

        The gain's release, which is what the user hears and what the knob says,
        is a separate stage further down.
    */
    constexpr float detectorReleaseMs = 15.0f;

    inline float toDecibels (float linear) noexcept
    {
        return 20.0f * std::log10 (juce::jmax (minimumLevel, linear));
    }

    inline float fromDecibels (float db) noexcept
    {
        return std::pow (10.0f, db * 0.05f);
    }
}

void GateEngine::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;
    maxBlockSize = (int) juce::jmax (1u, spec.maximumBlockSize);

    // Room for the longest lookahead the parameter allows, plus one block so a
    // read never has to wrap twice.
    delayLength = (int) std::ceil (0.011 * sampleRate) + maxBlockSize + 8;

    for (auto& buffer : delayBuffer)
        buffer.assign ((size_t) delayLength, 0.0f);

    for (auto& follower : peakFollowers)
    {
        follower.prepare (sampleRate);
        follower.setTimes (0.01f, detectorReleaseMs);
    }

    for (auto& follower : rmsFollowers)
        follower.prepare (sampleRate);

    for (auto& envelope : envelopes)
        envelope.prepare (sampleRate);

    mixAmount.reset (sampleRate, 0.02);

    reset();
}

void GateEngine::reset()
{
    for (auto& buffer : delayBuffer)
        std::fill (buffer.begin(), buffer.end(), 0.0f);

    delayWritePos = 0;

    for (auto& follower : peakFollowers) follower.reset();
    for (auto& follower : rmsFollowers)  follower.reset();
    for (auto& envelope : envelopes)     envelope.reset();
    for (auto& filter : scHighpass)      filter.reset();
    for (auto& filter : scLowpass)       filter.reset();

    gateOpen.fill (false);
    settingsValid = false;

    reductionDb.store (0.0f, std::memory_order_relaxed);
    detectorDb.store (-100.0f, std::memory_order_relaxed);
    openNow.store (false, std::memory_order_relaxed);
}

int GateEngine::computeLatencySamples() const noexcept
{
    return (int) std::round (pending.lookaheadMs * 0.001 * sampleRate);
}

void GateEngine::applyPendingSettings()
{
    const auto traits = styleTraits (pending.style);

    settings = pending;

    for (int ch = 0; ch < 2; ++ch)
    {
        auto& envelope = envelopes[(size_t) ch];

        envelope.setTimes (settings.attackMs * traits.attackScale,
                           settings.releaseMs * traits.releaseScale);
        envelope.setHold (settings.holdMs + traits.holdAddMs);
        envelope.setProgramDependence (traits.programDepend);
        envelope.setDualStage (traits.dualStage);

        if (traits.rmsMs > 0.0f)
            rmsFollowers[(size_t) ch].setWindow (traits.rmsMs);
    }

    useRms = traits.rmsMs > 0.0f;

    scFiltersActive = sidechainFiltersActive (settings);

    if (scFiltersActive)
    {
        const auto hp = dsp::BiquadCoefficients::highPass (
            sampleRate, juce::jlimit (20.0, sampleRate * 0.45, (double) settings.scHighpassHz), 0.707);
        const auto lp = dsp::BiquadCoefficients::lowPass (
            sampleRate, juce::jlimit (20.0, sampleRate * 0.45, (double) settings.scLowpassHz), 0.707);

        for (int ch = 0; ch < 2; ++ch)
        {
            scHighpass[(size_t) ch].setCoefficients (hp);
            scLowpass[(size_t) ch].setCoefficients (lp);
        }
    }

    // As in the compressor: a SmoothedValue starts at zero, so ramping on the
    // very first block would fade the plugin in from silence every time the
    // transport starts.
    const auto mixTarget = juce::jlimit (0.0f, 1.0f, settings.mix);

    if (settingsValid)
        mixAmount.setTargetValue (mixTarget);
    else
        mixAmount.setCurrentAndTargetValue (mixTarget);

    lookaheadSamples = juce::jlimit (0, delayLength - maxBlockSize - 2,
                                     (int) std::round (settings.lookaheadMs * 0.001 * sampleRate));

    settingsValid = true;
}

float GateEngine::staticOutputDb (float inputDb, const GateSettings& s)
{
    const auto traits = styleTraits (s.style);
    const auto knee = s.kneeDb * traits.kneeScale;

    /*  Drawn without the hysteresis, on purpose. The Schmitt trigger means
        there is no single transfer curve — there are two, and which one you are
        on depends on where you have just been. Drawing the opening one is the
        honest choice: it is the curve that decides whether a sound gets through
        at all, and it is the one the threshold knob is pointing at.
    */
    const auto gainDb = s.direction == Direction::ducking
                      ? juce::jmax (-s.rangeDb,
                                    dsp::computeStaticGainDb (inputDb, s.thresholdDb,
                                                              s.ratio, knee))
                      : expansionGainDb (inputDb, s.thresholdDb,
                                         expansionSlope (s.ratio), knee, s.rangeDb);

    const auto wet = fromDecibels (inputDb + gainDb);
    const auto dry = fromDecibels (inputDb);

    return toDecibels (s.mix * wet + (1.0f - s.mix) * dry);
}

void GateEngine::process (juce::dsp::AudioBlock<float>& block,
                          const juce::dsp::AudioBlock<const float>* sidechain)
{
    applyPendingSettings();

    const auto numSamples = (int) block.getNumSamples();
    const auto channels = (int) juce::jmin ((size_t) 2, block.getNumChannels());

    if (numSamples <= 0 || channels <= 0)
        return;

    const auto traits = styleTraits (settings.style);
    const auto knee = settings.kneeDb * traits.kneeScale;
    const auto slope = expansionSlope (settings.ratio);
    const auto range = juce::jmax (0.0f, settings.rangeDb);
    const auto ducking = settings.direction == Direction::ducking;
    const auto hysteresis = juce::jmax (0.0f, settings.hysteresisDb);
    const auto midSide = settings.channelMode == ChannelMode::midSide && channels > 1;
    const auto linked = settings.channelMode == ChannelMode::linked || channels < 2;
    const auto midiTriggered = settings.trigger == Trigger::midi;
    const auto triggerIsOpen = triggerOpen.load (std::memory_order_relaxed);

    const auto haveExternal = sidechain != nullptr
                           && settings.sidechain == SidechainSource::external
                           && sidechain->getNumChannels() > 0
                           && sidechain->getNumSamples() >= (size_t) numSamples;

    std::array<float*, 2> audio { nullptr, nullptr };
    std::array<const float*, 2> scIn { nullptr, nullptr };

    for (int ch = 0; ch < channels; ++ch)
    {
        audio[(size_t) ch] = block.getChannelPointer ((size_t) ch);

        if (haveExternal)
        {
            const auto scChannel = juce::jmin (sidechain->getNumChannels() - 1, (size_t) ch);
            scIn[(size_t) ch] = sidechain->getChannelPointer (scChannel);
        }
    }

    auto worstReduction = 0.0f;
    auto lastDetectorDb = -100.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        // --- what the detector listens to -------------------------------------
        std::array<float, 2> filtered { 0.0f, 0.0f }, watched { 0.0f, 0.0f },
                             detector { 0.0f, 0.0f };

        for (int ch = 0; ch < channels; ++ch)
        {
            auto source = haveExternal ? scIn[(size_t) ch][i] : audio[(size_t) ch][i];

            if (scFiltersActive)
            {
                source = scHighpass[(size_t) ch].processSample (source);
                source = scLowpass[(size_t) ch].processSample (source);
            }

            filtered[(size_t) ch] = source;
            watched[(size_t) ch] = source;
        }

        if (channels == 1)
        {
            filtered[1] = filtered[0];
            watched[1] = watched[0];
        }

        /*  In mid/side the detector reads the components, not the channels, and
            it has to read them *before* rectifying: the side signal is the
            difference of the two waveforms, which is not the difference of their
            two envelopes. A gate on the side triggered by a rectified difference
            would open every time the lead vocal did, which is the opposite of
            the point.
        */
        if (midSide)
        {
            const auto mid  = (watched[0] + watched[1]) * 0.5f;
            const auto side = (watched[0] - watched[1]) * 0.5f;
            watched[0] = mid;
            watched[1] = side;
        }

        for (int ch = 0; ch < 2; ++ch)
            detector[(size_t) ch] = useRms ? rmsFollowers[(size_t) ch].process (watched[(size_t) ch])
                                           : peakFollowers[(size_t) ch].process (watched[(size_t) ch]);

        // --- the Schmitt trigger and the static curve --------------------------
        std::array<float, 2> gain { 1.0f, 1.0f };

        const auto gates = linked ? 1 : 2;
        auto lastTarget = 0.0f;

        for (int g = 0; g < gates; ++g)
        {
            const auto rawLevel = linked ? juce::jmax (detector[0], detector[1])
                                         : detector[(size_t) g];
            const auto levelDb = toDecibels (rawLevel);

            auto& isOpenNow = gateOpen[(size_t) g];

            /*  Two thresholds, not one. The gate opens at the threshold and
                closes only once the level has fallen a further `hysteresis`
                below it, so material sitting right on the threshold cannot open
                and close on every wobble. Ducking gets the same treatment with
                the sense reversed: it engages at the threshold and disengages
                lower down.
            */
            if (isOpenNow)
            {
                if (levelDb < settings.thresholdDb - hysteresis)
                    isOpenNow = false;
            }
            else if (levelDb > settings.thresholdDb)
            {
                isOpenNow = true;
            }

            const auto effectiveThreshold = isOpenNow ? settings.thresholdDb - hysteresis
                                                      : settings.thresholdDb;

            auto target = ducking
                        ? juce::jmax (-range, dsp::computeStaticGainDb (levelDb, effectiveThreshold,
                                                                        settings.ratio, knee))
                        : expansionGainDb (levelDb, effectiveThreshold, slope, knee, range);

            /*  With MIDI triggering the detector stops deciding anything: the
                note does. Everything else — attack, hold, release, range — still
                applies, which is what makes a triggered gate sound like the
                same gate rather than like a switch.
            */
            if (midiTriggered)
                target = triggerIsOpen ? 0.0f : -range;

            const auto smoothed = envelopes[(size_t) g].process (target);

            gain[(size_t) g] = fromDecibels (smoothed);
            lastTarget = target;

            if (smoothed < worstReduction)
                worstReduction = smoothed;

            if (g == 0)
                lastDetectorDb = levelDb;
        }

        if (linked)
        {
            gain[1] = gain[0];

            // The second envelope is fed the same target rather than left idle,
            // so switching to an unlinked mode mid-signal does not jump.
            envelopes[1].process (lastTarget);
        }

        const auto wetAmount = mixAmount.getNextValue();
        const auto dryAmount = 1.0f - wetAmount;

        // --- apply -------------------------------------------------------------
        std::array<float, 2> delayed { 0.0f, 0.0f };

        for (int ch = 0; ch < channels; ++ch)
        {
            auto& buffer = delayBuffer[(size_t) ch];

            buffer[(size_t) delayWritePos] = audio[(size_t) ch][i];

            auto readPos = delayWritePos - lookaheadSamples;

            if (readPos < 0)
                readPos += delayLength;

            delayed[(size_t) ch] = buffer[(size_t) readPos];
        }

        if (midSide && channels > 1)
        {
            /*  Rotate, apply, rotate back. Only the gain differs between the two
                components; nothing is filtered, so the round trip is exact.
            */
            const auto mid  = (delayed[0] + delayed[1]) * 0.5f;
            const auto side = (delayed[0] - delayed[1]) * 0.5f;

            const auto gatedMid  = mid  * gain[0];
            const auto gatedSide = side * gain[1];

            const auto left  = gatedMid + gatedSide;
            const auto right = gatedMid - gatedSide;

            audio[0][i] = settings.scListen ? filtered[0] : left  * wetAmount + delayed[0] * dryAmount;
            audio[1][i] = settings.scListen ? filtered[1] : right * wetAmount + delayed[1] * dryAmount;
        }
        else
        {
            for (int ch = 0; ch < channels; ++ch)
            {
                // Both paths read the delayed signal, including the dry one:
                // mixing a delayed wet against an undelayed dry would comb.
                const auto wet = delayed[(size_t) ch] * gain[(size_t) ch];

                audio[(size_t) ch][i] = settings.scListen
                                      ? filtered[(size_t) ch]
                                      : wet * wetAmount + delayed[(size_t) ch] * dryAmount;
            }
        }

        if (++delayWritePos >= delayLength)
            delayWritePos = 0;
    }

    for (auto& filter : scHighpass) filter.snapToZero();
    for (auto& filter : scLowpass)  filter.snapToZero();

    reductionDb.store (worstReduction, std::memory_order_relaxed);
    detectorDb.store (lastDetectorDb, std::memory_order_relaxed);
    openNow.store (gateOpen[0] || (! linked && gateOpen[1]), std::memory_order_relaxed);
}
} // namespace nodo::gate
