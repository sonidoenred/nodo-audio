#pragma once

#include <juce_dsp/juce_dsp.h>
#include "EqBand.h"

namespace nodo::eq
{
/** The band cascade.

    Continuous parameters (frequency, gain, Q) are smoothed and the block is
    processed in short chunks, recomputing coefficients per chunk. That is what
    keeps an automated sweep free of zipper noise: updating coefficients once per
    audio block is audible as stepping, and updating them per sample is wasteful.
    Thirty-two samples is the compromise, and it is inaudible.

    Frequency is smoothed in log2 space so a sweep from 100 Hz to 10 kHz moves at
    a constant number of octaves per second rather than rushing the top.

    Disabled bands cost nothing: they design to zero stages and return early.
*/
class EqEngine
{
public:
    static constexpr int chunkSize = 32;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    /** Sets the values the smoothers ramp towards. Call once per audio block. */
    void setTargets (const std::array<BandSettings, numBands>& targets);

    /** Jumps straight to the targets, for preset loads and prepareToPlay. */
    void snapToTargets();

    /** Isolates one band for listening. Pass -1 for normal operation.

        Solo replaces the whole chain rather than sitting after it: the point is
        to hear the material the band is acting on, not the band's own output.
    */
    void setSoloBand (int band) noexcept { soloBand = band; }

    /** Bilinear or analog-matched design for bells and shelves. Changing this
        invalidates every cached design, since the coefficients differ.
    */
    void setFilterMode (FilterMode mode) noexcept
    {
        if (mode != filterMode)
        {
            filterMode = mode;
            designsDirty = true;
        }
    }

    /** sidechain may be null. When it is not, and the dynamic bands have been
        told to use it, every band's detector reads it instead of the signal
        being equalised — the difference between a dynamic EQ and a ducker.
    */
    void process (juce::dsp::AudioBlock<float>& block,
                  const juce::dsp::AudioBlock<const float>* sidechain = nullptr) noexcept;

    /** Where the dynamic bands listen. One switch for all of them rather than
        one per band: with twenty-four bands, per-band routing would add
        twenty-four automatable parameters to serve a case nobody has asked for,
        and "duck these bands from that track" is what people actually do.
    */
    void setDynamicSidechain (bool useExternal) noexcept { externalSidechain = useExternal; }

    /** Which of the two output components a response curve is being drawn for.

        A band that acts on Left and a band that acts on Mid are diagonal in
        different bases, so there is no single transfer function to draw once both
        are in play. These two components are the honest reading: what happens to
        the first output component (L or M) and to the second (R or S).
    */
    enum class Component { combined, first, second };

    /** Combined response of all bands in dB, for the GUI curve. */
    static double magnitudeDbAt (const std::array<BandSettings, numBands>& bands,
                                 double frequency,
                                 double sampleRate,
                                 FilterMode mode,
                                 Component component = Component::combined);

    /** True when any enabled band is doing something other than plain stereo, so
        the display knows it needs to split the curve in two.
    */
    static bool hasSplitBands (const std::array<BandSettings, numBands>& bands) noexcept;

    /** Where a band's gain actually is right now, in dB.

        For a static band this is just its gain. For a dynamic one it is wherever
        the detector has pushed it this instant, which is what the display draws
        so the curve moves with the music. Written by the audio thread, read by
        the GUI, so it is atomic and relaxed: the display only ever needs a recent
        value, never a precise history.
    */
    float getCurrentGainDb (int band) const noexcept
    {
        return currentGainDb[(size_t) juce::jlimit (0, numBands - 1, band)].load (std::memory_order_relaxed);
    }

private:
    struct Smoothers
    {
        juce::SmoothedValue<float> logFrequency;
        juce::SmoothedValue<float> gainDb;
        juce::SmoothedValue<float> q;
    };

    /** Per-band level detection for the dynamic mode.

        The detector filter is the SAME design the solo button uses, which is a
        deliberate choice rather than a convenient one: it means the thing the
        dynamics react to is exactly the thing you hear when you solo the band.
        No explaining away a de-esser that clamps down on something you cannot
        find.
    */
    struct Detector
    {
        std::array<nodo::dsp::Biquad, EqBand::maxStages> filters;
        nodo::dsp::EnvelopeFollower follower;
        EqBand::Design design;
        BandSettings designedFor;
        bool valid { false };
    };

    void updateDetector (int band, const BandSettings& settings);
    float detectLevelDb (int band, const BandSettings& settings, size_t numSamples) noexcept;

    std::array<EqBand, numBands> bands;
    std::array<Smoothers, numBands> smoothers;
    std::array<BandSettings, numBands> targetSettings;
    std::array<BandSettings, numBands> appliedSettings;
    std::array<Detector, numBands> detectors;
    std::array<std::atomic<float>, numBands> currentGainDb {};

    /** A copy of each chunk as it arrives, so every detector sees the untouched
        input rather than whatever the bands before it have already done. Without
        this the bands would feed each other and a couple of dynamic bands in the
        same region would chase their own tails.
    */
    std::array<std::array<float, chunkSize>, 2> detectorInput {};
    std::array<float, chunkSize> detectorScratch {};
    bool externalSidechain { false };

    EqBand soloFilter;
    BandSettings appliedSoloSettings;
    int soloBand { -1 };
    FilterMode filterMode { FilterMode::analogMatched };
    bool designsDirty { true };

    double sampleRate { 44100.0 };
};
} // namespace nodo::eq
