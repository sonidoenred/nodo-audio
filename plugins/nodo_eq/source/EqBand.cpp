#include "EqBand.h"

namespace nodo::eq
{
void EqBand::prepare (const juce::dsp::ProcessSpec& spec)
{
    filters.assign ((size_t) juce::jmax (1u, spec.numChannels), {});
    reset();
}

void EqBand::reset()
{
    for (auto& channel : filters)
        for (auto& filter : channel)
            filter.reset();
}

EqBand::Design EqBand::designFor (const BandSettings& settings, double sampleRate,
                                  FilterMode mode) noexcept
{
    using Coeffs = nodo::dsp::BiquadCoefficients;

    Design result;

    if (! settings.enabled || sampleRate <= 0.0)
        return result;

    const auto f = (double) settings.frequency;
    const auto q = (double) settings.q;
    const auto g = (double) settings.gainDb;
    const auto analog = mode == FilterMode::analogMatched;

    // Bells, shelves and cuts all have an analog-matched design. Notches, band
    // passes and all passes stay bilinear in both modes: their shape is defined
    // by where their zeros are, and moving those to chase the prototype would
    // trade the thing they are for a closer curve.
    auto peak = [&] (double freq, double quality, double gain)
    {
        return analog ? nodo::dsp::matchedPeak (sampleRate, freq, quality, gain)
                      : Coeffs::peak (sampleRate, freq, quality, gain);
    };

    auto lowShelf = [&] (double freq, double quality, double gain)
    {
        return analog ? nodo::dsp::matchedLowShelf (sampleRate, freq, quality, gain)
                      : Coeffs::lowShelf (sampleRate, freq, quality, gain);
    };

    auto highShelf = [&] (double freq, double quality, double gain)
    {
        return analog ? nodo::dsp::matchedHighShelf (sampleRate, freq, quality, gain)
                      : Coeffs::highShelf (sampleRate, freq, quality, gain);
    };

    switch (settings.type)
    {
        case FilterType::bell:
            result.stages[0] = peak (f, q, g);
            result.numStages = 1;
            break;

        case FilterType::lowShelf:
            result.stages[0] = lowShelf (f, q, g);
            result.numStages = 1;
            break;

        case FilterType::highShelf:
            result.stages[0] = highShelf (f, q, g);
            result.numStages = 1;
            break;

        case FilterType::notch:
            result.stages[0] = Coeffs::notch (sampleRate, f, q);
            result.numStages = 1;
            break;

        case FilterType::bandPass:
            result.stages[0] = Coeffs::bandPass (sampleRate, f, q);
            result.numStages = 1;
            break;

        case FilterType::allPass:
            result.stages[0] = Coeffs::allPass (sampleRate, f, q);
            result.numStages = 1;
            break;

        case FilterType::tiltShelf:
            // Pivot at the band frequency: lows go one way, highs the other, so
            // the total tilt across the spectrum is the gain value.
            result.stages[0] = lowShelf  (f, q, -g * 0.5);
            result.stages[1] = highShelf (f, q,  g * 0.5);
            result.numStages = 2;
            break;

        case FilterType::lowCut:
        case FilterType::highCut:
        {
            const auto numStages = juce::jlimit (1, maxStages, settings.slopeStages);
            const auto qs = nodo::dsp::butterworthQ (numStages);

            // Each section of the cascade is matched on its own, with its own
            // Butterworth Q. A bilinear low pass has a double zero at Nyquist
            // and so falls to nothing there, while the prototype it claims to
            // be still has a real value: at 44.1 kHz that is the difference
            // between the high cut you drew and one that is visibly steeper at
            // the top of the band.
            for (int s = 0; s < numStages; ++s)
            {
                const auto stageQ = (double) qs[(size_t) s];

                result.stages[(size_t) s] = settings.type == FilterType::lowCut
                    ? (analog ? nodo::dsp::matchedHighPass (sampleRate, f, stageQ)
                              : Coeffs::highPass (sampleRate, f, stageQ))
                    : (analog ? nodo::dsp::matchedLowPass (sampleRate, f, stageQ)
                              : Coeffs::lowPass (sampleRate, f, stageQ));
            }

            result.numStages = numStages;
            break;
        }
    }

    return result;
}

EqBand::Design EqBand::soloDesignFor (const BandSettings& settings, double sampleRate) noexcept
{
    using Coeffs = nodo::dsp::BiquadCoefficients;

    Design result;

    if (sampleRate <= 0.0)
        return result;

    const auto f = (double) settings.frequency;
    const auto q = juce::jlimit (0.4, 12.0, (double) settings.q);

    switch (settings.type)
    {
        case FilterType::lowShelf:
            // A shelf acts on everything below its corner, so listen to that.
            result.stages[0] = Coeffs::lowPass (sampleRate, f, 0.54119610);
            result.stages[1] = Coeffs::lowPass (sampleRate, f, 1.30656296);
            result.numStages = 2;
            break;

        case FilterType::highShelf:
            result.stages[0] = Coeffs::highPass (sampleRate, f, 0.54119610);
            result.stages[1] = Coeffs::highPass (sampleRate, f, 1.30656296);
            result.numStages = 2;
            break;

        case FilterType::lowCut:
        case FilterType::highCut:
        {
            // For a cut, the interesting thing is what is being thrown away, so
            // solo plays the complement of the filter at the same steepness.
            const auto numStages = juce::jlimit (1, maxStages, settings.slopeStages);
            const auto qs = nodo::dsp::butterworthQ (numStages);

            for (int s = 0; s < numStages; ++s)
                result.stages[(size_t) s] = settings.type == FilterType::lowCut
                    ? Coeffs::lowPass  (sampleRate, f, (double) qs[(size_t) s])
                    : Coeffs::highPass (sampleRate, f, (double) qs[(size_t) s]);

            result.numStages = numStages;
            break;
        }

        case FilterType::bell:
        case FilterType::notch:
        case FilterType::bandPass:
        case FilterType::tiltShelf:
        case FilterType::allPass:
            // A band pass centred on the band. Two cascaded sections rather than
            // one, because a single band pass has 6 dB/oct skirts and leaks too
            // much of the rest of the mix to be useful for listening.
            result.stages[0] = Coeffs::bandPass (sampleRate, f, q);
            result.stages[1] = Coeffs::bandPass (sampleRate, f, q);
            result.numStages = 2;
            break;
    }

    return result;
}

void EqBand::setDesign (const Design& newDesign) noexcept
{
    design = newDesign;
    active = design.numStages > 0;

    if (! active)
        return;

    for (auto& channel : filters)
        for (int s = 0; s < design.numStages; ++s)
            channel[(size_t) s].setCoefficients (design.stages[(size_t) s]);
}

void EqBand::setSettings (const BandSettings& settings, double sampleRate,
                          FilterMode mode) noexcept
{
    setDesign (designFor (settings, sampleRate, mode));
}

void EqBand::process (juce::dsp::AudioBlock<float>& block, int channelMask) noexcept
{
    if (! active || design.numStages == 0 || filters.empty())
        return;

    const auto numChannels = (int) juce::jmin (block.getNumChannels(), filters.size());
    const auto numSamples = (int) block.getNumSamples();

    for (int ch = 0; ch < numChannels; ++ch)
    {
        if ((channelMask & (1 << ch)) == 0)
            continue;

        auto* data = block.getChannelPointer ((size_t) ch);

        for (int s = 0; s < design.numStages; ++s)
        {
            auto& filter = filters[(size_t) ch][(size_t) s];

            for (int i = 0; i < numSamples; ++i)
                data[i] = filter.processSample (data[i]);

            filter.snapToZero();
        }
    }
}

double EqBand::magnitudeForFrequency (const BandSettings& settings,
                                      double frequency,
                                      double sampleRate,
                                      FilterMode mode) noexcept
{
    const auto d = designFor (settings, sampleRate, mode);

    double magnitude = 1.0;

    for (int s = 0; s < d.numStages; ++s)
        magnitude *= d.stages[(size_t) s].magnitudeAt (frequency, sampleRate);

    return magnitude;
}
} // namespace nodo::eq
