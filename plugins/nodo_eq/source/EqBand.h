#pragma once

#include <juce_dsp/juce_dsp.h>
#include "EqParameters.h"

namespace nodo::eq
{
/** One band of the equaliser.

    Bells, shelves, notches, band passes and all passes are a single biquad. A
    tilt shelf is two: a low shelf pulling one way and a high shelf pushing the
    other, pivoting at the band frequency. Cuts are a Butterworth cascade of one
    to eight biquads, which is what gives the 12 to 96 dB per octave options; the
    per-stage Q values come from nodo::dsp::butterworthQ so the combined passband
    stays flat instead of developing a resonant bump at the corner.

    Nothing in here allocates once prepare() has run.
*/
class EqBand
{
public:
    static constexpr int maxStages = nodo::dsp::maxButterworthStages;

    /** A band's complete filter design: up to eight cascaded sections. */
    struct Design
    {
        std::array<nodo::dsp::BiquadCoefficients, maxStages> stages {};
        int numStages { 0 };
    };

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    /** Recomputes coefficients. Cheap and allocation-free, so it is safe to call
        once per 32-sample chunk, which is how parameter changes stay click-free.
    */
    void setSettings (const BandSettings& settings, double sampleRate, FilterMode mode) noexcept;

    /** Installs a design directly, for uses that are not a band's own response
        (the solo monitor filter, for instance).
    */
    void setDesign (const Design& newDesign) noexcept;

    /** @param channelMask  bit 0 == channel 0, bit 1 == channel 1. A band set to
                             Left, Right, Mid or Side only touches one of them.
    */
    void process (juce::dsp::AudioBlock<float>& block, int channelMask = 0b11) noexcept;

    static Design designFor (const BandSettings& settings, double sampleRate, FilterMode mode) noexcept;

    /** A band pass or cut that isolates the region this band acts on, so that
        soloing a band lets you hear what it is working on rather than what it is
        doing. This is the single most useful teaching feature in the plugin.
    */
    static Design soloDesignFor (const BandSettings& settings, double sampleRate) noexcept;

    /** Linear magnitude of this band alone. Used to draw the curve, so it runs on
        the message thread from the target values rather than the smoothed ones.
    */
    static double magnitudeForFrequency (const BandSettings& settings,
                                         double frequency,
                                         double sampleRate,
                                         FilterMode mode) noexcept;

private:
    std::vector<std::array<nodo::dsp::Biquad, maxStages>> filters;
    Design design;
    bool active { false };
};
} // namespace nodo::eq
