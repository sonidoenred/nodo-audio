#include "EqEngine.h"

namespace nodo::eq
{
namespace
{
    constexpr double smoothingSeconds = 0.03;

    float safeLog2 (float hz) noexcept
    {
        return std::log2 (juce::jmax (1.0f, hz));
    }

    /** Rotates a stereo block between left/right and mid/side, in place.

            M = (L + R) / 2      L = M + S
            S = (L - R) / 2      R = M - S

        The halving lives on the forward pass so that a centred mono signal keeps
        its level through the round trip, and so the pair is an exact inverse.
    */
    void rotate (juce::dsp::AudioBlock<float>& block, bool toMidSide) noexcept
    {
        if (block.getNumChannels() < 2)
            return;

        auto* a = block.getChannelPointer (0);
        auto* b = block.getChannelPointer (1);
        const auto n = block.getNumSamples();

        if (toMidSide)
        {
            for (size_t i = 0; i < n; ++i)
            {
                const auto mid  = (a[i] + b[i]) * 0.5f;
                const auto side = (a[i] - b[i]) * 0.5f;
                a[i] = mid;
                b[i] = side;
            }
        }
        else
        {
            for (size_t i = 0; i < n; ++i)
            {
                const auto left  = a[i] + b[i];
                const auto right = a[i] - b[i];
                a[i] = left;
                b[i] = right;
            }
        }
    }
}

void EqEngine::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;

    for (auto& band : bands)
        band.prepare (spec);

    for (auto& detector : detectors)
    {
        detector.follower.prepare (sampleRate);
        detector.valid = false;

        for (auto& filter : detector.filters)
            filter.reset();
    }

    soloFilter.prepare (spec);
    appliedSoloSettings = {};

    for (auto& s : smoothers)
    {
        s.logFrequency.reset (sampleRate, smoothingSeconds);
        s.gainDb.reset (sampleRate, smoothingSeconds);
        s.q.reset (sampleRate, smoothingSeconds);
    }

    snapToTargets();
}

void EqEngine::reset()
{
    for (auto& band : bands)
        band.reset();

    for (auto& detector : detectors)
    {
        detector.follower.reset();

        for (auto& filter : detector.filters)
            filter.reset();
    }

    soloFilter.reset();
}

void EqEngine::updateDetector (int band, const BandSettings& settings)
{
    auto& detector = detectors[(size_t) band];

    detector.design = EqBand::soloDesignFor (settings, sampleRate);
    detector.designedFor = settings;
    detector.valid = true;

    for (int stage = 0; stage < detector.design.numStages; ++stage)
        detector.filters[(size_t) stage].setCoefficients (detector.design.stages[(size_t) stage]);

    detector.follower.setTimes (settings.attackMs, settings.releaseMs);
}

float EqEngine::detectLevelDb (int band, const BandSettings& settings, size_t numSamples) noexcept
{
    auto& detector = detectors[(size_t) band];

    // Collapse the input to the one signal this band actually acts on, so a Mid
    // band is triggered by centred material and a Side band is not.
    for (size_t i = 0; i < numSamples; ++i)
    {
        const auto l = detectorInput[0][i];
        const auto r = detectorInput[1][i];

        switch (settings.channel)
        {
            case ChannelMode::left:  detectorScratch[i] = l; break;
            case ChannelMode::right: detectorScratch[i] = r; break;
            case ChannelMode::side:  detectorScratch[i] = (l - r) * 0.5f; break;
            case ChannelMode::mid:
            case ChannelMode::stereo:
            default:                 detectorScratch[i] = (l + r) * 0.5f; break;
        }
    }

    for (int stage = 0; stage < detector.design.numStages; ++stage)
    {
        auto& filter = detector.filters[(size_t) stage];

        for (size_t i = 0; i < numSamples; ++i)
            detectorScratch[i] = filter.processSample (detectorScratch[i]);

        filter.snapToZero();
    }

    float envelope = 0.0f;

    for (size_t i = 0; i < numSamples; ++i)
        envelope = detector.follower.process (detectorScratch[i]);

    return juce::Decibels::gainToDecibels (envelope, -120.0f);
}

void EqEngine::setTargets (const std::array<BandSettings, numBands>& targets)
{
    targetSettings = targets;

    for (int i = 0; i < numBands; ++i)
    {
        smoothers[(size_t) i].logFrequency.setTargetValue (safeLog2 (targets[(size_t) i].frequency));
        smoothers[(size_t) i].gainDb.setTargetValue (targets[(size_t) i].gainDb);
        smoothers[(size_t) i].q.setTargetValue (targets[(size_t) i].q);

        /*  La ganancia viva se publica tambien desde aqui, y no solo desde el
            bucle de proceso, para que valga algo antes de que llegue la primera
            muestra: en un plugin recien abierto, o con el transporte parado, el
            motor no ha corrido nunca y la pantalla dibujaba cero para una banda
            que esta en +7. Se pisa unas lineas mas abajo en cuanto hay audio, y
            la posicion de reposo de una banda dinamica es justamente su
            ganancia, asi que el valor de partida es el correcto para las dos.
        */
        currentGainDb[(size_t) i].store (targets[(size_t) i].enabled
                                            ? targets[(size_t) i].gainDb : 0.0f,
                                         std::memory_order_relaxed);
    }
}

void EqEngine::snapToTargets()
{
    for (int i = 0; i < numBands; ++i)
    {
        const auto& t = targetSettings[(size_t) i];
        smoothers[(size_t) i].logFrequency.setCurrentAndTargetValue (safeLog2 (t.frequency));
        smoothers[(size_t) i].gainDb.setCurrentAndTargetValue (t.gainDb);
        smoothers[(size_t) i].q.setCurrentAndTargetValue (t.q);

        bands[(size_t) i].setSettings (t, sampleRate, filterMode);
        appliedSettings[(size_t) i] = t;
        updateDetector (i, t);
        currentGainDb[(size_t) i].store (t.gainDb, std::memory_order_relaxed);
    }

    designsDirty = false;
}

void EqEngine::process (juce::dsp::AudioBlock<float>& block,
                        const juce::dsp::AudioBlock<const float>* sidechain) noexcept
{
    // Solo bypasses the chain entirely: the listener wants the raw material in
    // that region, not the material after the band has already acted on it.
    if (soloBand >= 0 && soloBand < numBands && targetSettings[(size_t) soloBand].enabled)
    {
        const auto& settings = targetSettings[(size_t) soloBand];

        if (settings != appliedSoloSettings)
        {
            soloFilter.setDesign (EqBand::soloDesignFor (settings, sampleRate));
            appliedSoloSettings = settings;
        }

        soloFilter.process (block);
        return;
    }

    auto remaining = block.getNumSamples();
    size_t offset = 0;

    // Mid/side needs two channels to rotate between. On a mono bus every band
    // behaves as if it were set to Stereo.
    const auto canSplit = block.getNumChannels() >= 2;

    while (remaining > 0)
    {
        const auto thisChunk = juce::jmin ((size_t) chunkSize, remaining);
        auto sub = block.getSubBlock (offset, thisChunk);

        // Snapshot the chunk before any band touches it: every detector reads
        // this, not the running signal.
        const auto useSidechain = externalSidechain
                               && sidechain != nullptr
                               && sidechain->getNumChannels() > 0
                               && sidechain->getNumSamples() >= offset + thisChunk;

        for (size_t ch = 0; ch < 2; ++ch)
        {
            if (useSidechain)
            {
                const auto source = juce::jmin (ch, sidechain->getNumChannels() - 1);
                const auto* data = sidechain->getChannelPointer (source) + offset;

                for (size_t i = 0; i < thisChunk; ++i)
                    detectorInput[ch][i] = data[i];
            }
            else
            {
                const auto source = juce::jmin (ch, block.getNumChannels() - 1);
                const auto* data = sub.getChannelPointer (source);

                for (size_t i = 0; i < thisChunk; ++i)
                    detectorInput[ch][i] = data[i];
            }
        }

        // Bands are applied strictly in order, converting between left/right and
        // mid/side only when the next band needs the other domain.
        //
        // The order genuinely matters: a filter on Left and a filter on Mid are
        // both diagonal, but in different bases, and diagonal operators in
        // different bases do not commute. Sorting the bands to save a conversion
        // would quietly change the sound.
        auto inMidSide = false;

        for (int i = 0; i < numBands; ++i)
        {
            auto& smoother = smoothers[(size_t) i];

            auto current = targetSettings[(size_t) i];
            current.frequency = std::exp2 (smoother.logFrequency.skip ((int) thisChunk));
            current.gainDb    = smoother.gainDb.skip ((int) thisChunk);
            current.q         = smoother.q.skip ((int) thisChunk);

            // Disabled bands never touch the audio, so there is no point paying
            // for their coefficients either.
            if (! current.enabled && ! appliedSettings[(size_t) i].enabled)
                continue;

            // A band coming back on, or one that has just changed which part of
            // the image it works on, must start from silence. Otherwise the state
            // left in its biquads pops.
            if ((current.enabled && ! appliedSettings[(size_t) i].enabled)
                || current.channel != appliedSettings[(size_t) i].channel)
                bands[(size_t) i].reset();

            /*  La dinamica se suma a la ganancia de la banda, no la sustituye.

                La banda vive donde dice su mando y el recorrido dice cuanto se
                mueve desde ahi: con el recorrido a cero, encender la dinamica
                no cambia una sola muestra. Antes la banda descansaba en plano y
                viajaba hasta su ganancia, asi que encender la dinamica la
                apagaba hasta que entrara senal — que es exactamente lo que se
                vio al probarlo: la curva se caia al pulsar DYN.
            */
            if (current.enabled && current.dynamic)
            {
                if (! detectors[(size_t) i].valid
                    || detectors[(size_t) i].designedFor != current)
                    updateDetector (i, current);

                nodo::dsp::DynamicSettings dynamics;
                dynamics.thresholdDb = current.thresholdDb;
                dynamics.ratio = current.ratio;
                dynamics.rangeDb = current.dynRangeDb;
                dynamics.direction = current.dynamicMode == DynamicMode::below
                                   ? nodo::dsp::DynamicDirection::below
                                   : nodo::dsp::DynamicDirection::above;

                current.gainDb += nodo::dsp::computeDynamicGainDb (
                    detectLevelDb (i, current, thisChunk), dynamics);
            }

            currentGainDb[(size_t) i].store (current.enabled ? current.gainDb : 0.0f,
                                             std::memory_order_relaxed);

            if (designsDirty || current != appliedSettings[(size_t) i])
            {
                bands[(size_t) i].setSettings (current, sampleRate, filterMode);
                appliedSettings[(size_t) i] = current;
            }

            if (! current.enabled)
                continue;

            if (canSplit)
            {
                const auto wantsMidSide = isMidSide (current.channel);

                if (wantsMidSide != inMidSide)
                {
                    rotate (sub, wantsMidSide);
                    inMidSide = wantsMidSide;
                }

                bands[(size_t) i].process (sub, channelMaskFor (current.channel));
            }
            else
            {
                bands[(size_t) i].process (sub);
            }
        }

        if (inMidSide)
            rotate (sub, false);

        designsDirty = false;
        offset += thisChunk;
        remaining -= thisChunk;
    }
}

bool EqEngine::hasSplitBands (const std::array<BandSettings, numBands>& bandSettings) noexcept
{
    for (const auto& settings : bandSettings)
        if (settings.enabled && settings.channel != ChannelMode::stereo)
            return true;

    return false;
}

double EqEngine::magnitudeDbAt (const std::array<BandSettings, numBands>& bandSettings,
                                double frequency,
                                double sampleRate,
                                FilterMode mode,
                                Component component)
{
    double db = 0.0;

    for (const auto& settings : bandSettings)
    {
        if (! settings.enabled)
            continue;

        if (component != Component::combined)
        {
            // A stereo band lands on both components; the rest on one or the other.
            const auto wanted = component == Component::first ? 0b01 : 0b10;

            if ((channelMaskFor (settings.channel) & wanted) == 0)
                continue;
        }

        const auto magnitude = EqBand::magnitudeForFrequency (settings, frequency, sampleRate, mode);
        db += juce::Decibels::gainToDecibels (magnitude, -120.0);
    }

    return db;
}
} // namespace nodo::eq
