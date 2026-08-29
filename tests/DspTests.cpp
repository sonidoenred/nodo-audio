/*
    Verification for the Nodo DSP primitives.

    These are not unit tests for their own sake. Filter coefficient maths is the
    kind of code that compiles perfectly and is quietly wrong by 3 dB, and you
    cannot hear a 0.5 dB error in a shelf while you are also judging the music.
    The important test here is the round trip: design a filter, run a real sine
    wave through the actual difference equation, measure the output, and check it
    against the analytic magnitude used to draw the curve on screen. If those two
    agree, then what the user sees and what the user hears are the same thing.
*/

#include <nodo_core/nodo_core.h>
#include "EqEngine.h"
#include "FactoryPresets.h"
#include "CompTests.h"
#include "LimitTests.h"
#include "EssTests.h"
#include "GateTests.h"
#include "DelayTests.h"
#include "VerbTests.h"
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
int failures = 0;
int checks = 0;

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

double toDb (double linear)
{
    return 20.0 * std::log10 (juce::jmax (1.0e-12, linear));
}

/** Runs a sine wave through the real difference equation and returns the output
    amplitude in dB relative to the input. This is ground truth: it exercises the
    coefficients and the state update together.
*/
double measuredGainDb (const nodo::dsp::BiquadCoefficients& coefficients,
                       double frequency,
                       double sampleRate)
{
    nodo::dsp::Biquad filter;
    filter.setCoefficients (coefficients);

    const auto warmup = 40000;
    const auto measure = 40000;
    const auto omega = 2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;

    for (int i = 0; i < warmup; ++i)
        filter.processSample ((float) std::sin (omega * i));

    double peak = 0.0;

    for (int i = 0; i < measure; ++i)
    {
        const auto out = filter.processSample ((float) std::sin (omega * (warmup + i)));
        peak = juce::jmax (peak, (double) std::abs (out));
    }

    return toDb (peak);
}

void testButterworthQ()
{
    std::printf ("\nButterworth cascade Q values\n");

    const auto one = nodo::dsp::butterworthQ (1);
    checkClose (one[0], 0.70710678, 1.0e-6, "order 2, stage 1");

    const auto two = nodo::dsp::butterworthQ (2);
    checkClose (two[0], 0.54119610, 1.0e-6, "order 4, stage 1");
    checkClose (two[1], 1.30656296, 1.0e-6, "order 4, stage 2");

    const auto four = nodo::dsp::butterworthQ (4);
    checkClose (four[0], 0.50979558, 1.0e-6, "order 8, stage 1");
    checkClose (four[3], 2.56291545, 1.0e-6, "order 8, stage 4");
}

void testBellFilter()
{
    std::printf ("\nBell filter\n");

    constexpr double sr = 48000.0;
    constexpr double fc = 1000.0;

    for (double gainDb : { -12.0, -6.0, 6.0, 12.0 })
    {
        const auto c = nodo::dsp::BiquadCoefficients::peak (sr, fc, 1.0, gainDb);

        checkClose (toDb (c.magnitudeAt (fc, sr)), gainDb, 0.01,
                    "analytic gain at centre (" + juce::String (gainDb, 0) + " dB)");

        checkClose (measuredGainDb (c, fc, sr), gainDb, 0.05,
                    "measured gain at centre (" + juce::String (gainDb, 0) + " dB)");

        // Five octaves away a Q of 1 bell should be back to unity.
        checkClose (toDb (c.magnitudeAt (fc / 32.0, sr)), 0.0, 0.15,
                    "5 octaves below is flat (" + juce::String (gainDb, 0) + " dB)");
    }
}

void testCutFilters()
{
    std::printf ("\nCut filters and slopes\n");

    constexpr double sr = 48000.0;
    constexpr double fc = 500.0;

    // A single Butterworth section is -3.01 dB at the corner frequency.
    const auto hp = nodo::dsp::BiquadCoefficients::highPass (sr, fc, 0.70710678);
    checkClose (toDb (hp.magnitudeAt (fc, sr)), -3.0103, 0.02, "12 dB/oct high pass at fc");
    checkClose (measuredGainDb (hp, fc, sr), -3.0103, 0.05, "12 dB/oct high pass at fc (measured)");

    // Asymptotic slope: two octaves below the corner a 12 dB/oct filter is 24 dB
    // down, a 24 dB/oct filter is 48 dB down, and so on, all the way to 96.
    for (int stages = 1; stages <= 8; ++stages)
    {
        const auto qs = nodo::dsp::butterworthQ (stages);
        double db = 0.0;

        for (int s = 0; s < stages; ++s)
            db += toDb (nodo::dsp::BiquadCoefficients::highPass (sr, fc, qs[(size_t) s])
                          .magnitudeAt (fc / 4.0, sr));

        const auto expected = -2.0 * 12.0 * stages;   // two octaves
        checkClose (db, expected, 1.0,
                    juce::String (12 * stages) + " dB/oct, two octaves below fc");
    }

    // The cascade must stay flat in the passband rather than developing a bump,
    // which is the whole point of using Butterworth Q values per stage.
    for (int stages = 1; stages <= 8; ++stages)
    {
        const auto qs = nodo::dsp::butterworthQ (stages);
        double db = 0.0;

        for (int s = 0; s < stages; ++s)
            db += toDb (nodo::dsp::BiquadCoefficients::highPass (sr, fc, qs[(size_t) s])
                          .magnitudeAt (fc * 8.0, sr));

        checkClose (db, 0.0, 0.1,
                    juce::String (12 * stages) + " dB/oct passband is flat");
    }

    const auto lp = nodo::dsp::BiquadCoefficients::lowPass (sr, fc, 0.70710678);
    checkClose (toDb (lp.magnitudeAt (fc, sr)), -3.0103, 0.02, "12 dB/oct low pass at fc");
    checkClose (toDb (lp.magnitudeAt (fc / 8.0, sr)), 0.0, 0.05, "low pass passband is flat");
}

void testShelves()
{
    std::printf ("\nShelving filters\n");

    constexpr double sr = 48000.0;
    constexpr double fc = 1000.0;

    for (double gainDb : { -9.0, 9.0 })
    {
        const auto low = nodo::dsp::BiquadCoefficients::lowShelf (sr, fc, 0.707, gainDb);
        checkClose (toDb (low.magnitudeAt (20.0, sr)), gainDb, 0.3,
                    "low shelf reaches full gain at 20 Hz (" + juce::String (gainDb, 0) + ")");
        checkClose (toDb (low.magnitudeAt (18000.0, sr)), 0.0, 0.3,
                    "low shelf is flat at 18 kHz (" + juce::String (gainDb, 0) + ")");
        checkClose (toDb (low.magnitudeAt (fc, sr)), gainDb * 0.5, 0.25,
                    "low shelf is half gain at fc (" + juce::String (gainDb, 0) + ")");

        const auto high = nodo::dsp::BiquadCoefficients::highShelf (sr, fc, 0.707, gainDb);
        checkClose (toDb (high.magnitudeAt (18000.0, sr)), gainDb, 0.3,
                    "high shelf reaches full gain at 18 kHz (" + juce::String (gainDb, 0) + ")");
        checkClose (toDb (high.magnitudeAt (20.0, sr)), 0.0, 0.3,
                    "high shelf is flat at 20 Hz (" + juce::String (gainDb, 0) + ")");

        checkClose (measuredGainDb (high, 18000.0, sr), gainDb, 0.2,
                    "high shelf measured at 18 kHz (" + juce::String (gainDb, 0) + ")");
    }
}

void testNotch()
{
    std::printf ("\nNotch\n");

    constexpr double sr = 48000.0;
    constexpr double fc = 2000.0;

    const auto n = nodo::dsp::BiquadCoefficients::notch (sr, fc, 8.0);
    check (toDb (n.magnitudeAt (fc, sr)) < -60.0, "notch rejects the centre frequency",
           toDb (n.magnitudeAt (fc, sr)), -60.0);
    checkClose (toDb (n.magnitudeAt (fc / 8.0, sr)), 0.0, 0.1, "notch is flat three octaves below");
}

void testExtraShapes()
{
    std::printf ("\nBand pass, all pass and tilt shelf\n");

    constexpr double sr = 48000.0;
    constexpr double fc = 1000.0;

    const auto bp = nodo::dsp::BiquadCoefficients::bandPass (sr, fc, 1.0);
    checkClose (toDb (bp.magnitudeAt (fc, sr)), 0.0, 0.01, "band pass is unity at centre");
    check (toDb (bp.magnitudeAt (fc / 16.0, sr)) < -20.0, "band pass rejects four octaves below",
           toDb (bp.magnitudeAt (fc / 16.0, sr)), -20.0);
    check (toDb (bp.magnitudeAt (fc * 16.0, sr)) < -20.0, "band pass rejects four octaves above",
           toDb (bp.magnitudeAt (fc * 16.0, sr)), -20.0);

    // An all pass must not change the magnitude anywhere at all: that is its
    // entire definition, and it is the easiest thing in the world to get wrong.
    const auto ap = nodo::dsp::BiquadCoefficients::allPass (sr, fc, 0.707);
    double worst = 0.0;

    for (double f = 20.0; f < 20000.0; f *= 1.1)
        worst = juce::jmax (worst, std::abs (toDb (ap.magnitudeAt (f, sr))));

    checkClose (worst, 0.0, 0.001, "all pass is flat across the spectrum");

    // Tilt: the total swing between the extremes should equal the gain setting.
    nodo::eq::BandSettings tilt;
    tilt.enabled = true;
    tilt.type = nodo::eq::FilterType::tiltShelf;
    tilt.frequency = 1000.0f;
    tilt.q = 0.5f;
    tilt.gainDb = 12.0f;

    const auto low = toDb (nodo::eq::EqBand::magnitudeForFrequency (tilt, 20.0, sr, nodo::eq::FilterMode::digital));
    const auto high = toDb (nodo::eq::EqBand::magnitudeForFrequency (tilt, 20000.0, sr, nodo::eq::FilterMode::digital));

    checkClose (high - low, 12.0, 0.6, "tilt shelf swings the full gain across the range");
    checkClose (toDb (nodo::eq::EqBand::magnitudeForFrequency (tilt, fc, sr, nodo::eq::FilterMode::digital)), 0.0, 0.2,
                "tilt shelf pivots at its frequency");
}

void testMatchedCuts()
{
    std::printf ("\nAnalog matched cuts\n");

    /*  The bilinear low pass puts a double zero at Nyquist, so it reads minus
        infinity there while the prototype still has a real value. The matched
        design does not, and the check is the same one used for the bells: run a
        real sine through the real difference equation and compare against the
        analog magnitude.
    */
    constexpr double sr = 44100.0;
    const auto nyquist = sr * 0.5;

    auto analogLowPassDb = [] (double frequency, double corner, double q)
    {
        const auto u = frequency / corner;
        return 10.0 * std::log10 (nodo::dsp::analog::lowPassMagnitudeSquared (u, q));
    };

    auto analogHighPassDb = [] (double frequency, double corner, double q)
    {
        const auto u = frequency / corner;
        return 10.0 * std::log10 (nodo::dsp::analog::highPassMagnitudeSquared (u, q));
    };

    // A 16 kHz high cut at 44.1 kHz: the corner is close enough to Nyquist for
    // the difference to be audible rather than academic.
    const auto corner = 16000.0;
    const auto q = 0.7071067811865476;

    const auto matched = nodo::dsp::matchedLowPass (sr, corner, q);
    const auto bilinear = nodo::dsp::BiquadCoefficients::lowPass (sr, corner, q);

    auto worstMatched = 0.0, worstBilinear = 0.0;

    for (auto frequency : { 8000.0, 12000.0, 16000.0, 18000.0, 20000.0 })
    {
        const auto wanted = analogLowPassDb (frequency, corner, q);

        worstMatched = juce::jmax (worstMatched,
                                   std::abs (measuredGainDb (matched, frequency, sr) - wanted));
        worstBilinear = juce::jmax (worstBilinear,
                                    std::abs (measuredGainDb (bilinear, frequency, sr) - wanted));
    }

    std::printf ("        worst error over the band: matched %.2f dB, bilinear %.2f dB\n",
                 worstMatched, worstBilinear);

    check (worstMatched < worstBilinear, "the matched high cut beats the bilinear one",
           worstMatched, worstBilinear);
    check (worstMatched < 1.5, "  and stays within 1.5 dB of the prototype", worstMatched, 0.0);

    // Just under Nyquist is where the bilinear design falls off a cliff.
    const auto nearNyquist = nyquist * 0.98;
    const auto wantedThere = analogLowPassDb (nearNyquist, corner, q);

    std::printf ("        at %.0f Hz: prototype %.2f dB, matched %.2f dB, bilinear %.2f dB\n",
                 nearNyquist, wantedThere,
                 measuredGainDb (matched, nearNyquist, sr),
                 measuredGainDb (bilinear, nearNyquist, sr));

    check (std::abs (measuredGainDb (matched, nearNyquist, sr) - wantedThere) < 3.0,
           "and it still has a real value just under Nyquist",
           measuredGainDb (matched, nearNyquist, sr), wantedThere);

    // The high pass has the same treatment in the corner.
    const auto hpCorner = 80.0;
    const auto matchedHp = nodo::dsp::matchedHighPass (sr, hpCorner, q);

    auto worstHp = 0.0;

    for (auto frequency : { 20.0, 40.0, 80.0, 160.0, 400.0 })
        worstHp = juce::jmax (worstHp,
                              std::abs (measuredGainDb (matchedHp, frequency, sr)
                                        - analogHighPassDb (frequency, hpCorner, q)));

    check (worstHp < 1.0, "the matched low cut tracks its prototype too", worstHp, 0.0);

    // Neither design may become unstable anywhere in the range, at any Q the
    // Butterworth cascade will ask for.
    int unstable = 0;

    for (auto frequency : { 20.0, 100.0, 1000.0, 10000.0, 19000.0, 21000.0 })
    {
        for (auto quality : { 0.51, 0.7071, 1.31, 2.56, 5.1 })
        {
            for (auto design : { nodo::dsp::matchedLowPass (sr, frequency, quality),
                                 nodo::dsp::matchedHighPass (sr, frequency, quality) })
            {
                // Poles inside the unit circle: |a2| < 1 and |a1| < 1 + a2.
                if (! (std::abs (design.a2) < 1.0 && std::abs (design.a1) < 1.0 + design.a2))
                    ++unstable;
            }
        }
    }

    check (unstable == 0, "every matched cut in the range is stable", (double) unstable, 0.0);
}

void testSoloDesigns()
{
    std::printf ("\nSolo monitor filters\n");

    constexpr double sr = 48000.0;

    auto magnitudeOf = [] (const nodo::eq::EqBand::Design& d, double f, double rate)
    {
        double m = 1.0;

        for (int s = 0; s < d.numStages; ++s)
            m *= d.stages[(size_t) s].magnitudeAt (f, rate);

        return toDb (m);
    };

    nodo::eq::BandSettings bell;
    bell.enabled = true;
    bell.type = nodo::eq::FilterType::bell;
    bell.frequency = 2000.0f;
    bell.q = 2.0f;

    const auto bellSolo = nodo::eq::EqBand::soloDesignFor (bell, sr);
    check (bellSolo.numStages > 0, "bell solo produces a filter", bellSolo.numStages, 1.0);
    check (magnitudeOf (bellSolo, 100.0, sr) < -20.0,
           "bell solo rejects material far below the band",
           magnitudeOf (bellSolo, 100.0, sr), -20.0);

    // Soloing a low cut should let you hear what is being removed, so it has to
    // pass the lows, not the highs. Getting this backwards would be silent but
    // completely wrong.
    nodo::eq::BandSettings lowCut;
    lowCut.enabled = true;
    lowCut.type = nodo::eq::FilterType::lowCut;
    lowCut.frequency = 200.0f;
    lowCut.slopeStages = 2;

    const auto cutSolo = nodo::eq::EqBand::soloDesignFor (lowCut, sr);
    checkClose (magnitudeOf (cutSolo, 40.0, sr), 0.0, 0.5,
                "low cut solo passes the lows it removes");
    check (magnitudeOf (cutSolo, 3000.0, sr) < -40.0,
           "low cut solo rejects the material it keeps",
           magnitudeOf (cutSolo, 3000.0, sr), -40.0);
}

/** Worst deviation, in dB, between a digital design and the analog prototype it
    is supposed to be imitating, measured from 20 Hz up to just under Nyquist.
*/
struct MatchReport
{
    double worstErrorDb { 0.0 };
    double worstFrequency { 0.0 };
    bool   usedFallback { false };
};

template <typename AnalogMagnitudeSquared>
MatchReport compareToAnalog (const nodo::dsp::BiquadCoefficients& digital,
                             AnalogMagnitudeSquared analogMagSq,
                             double sampleRate,
                             double bandFrequency,
                             double upperLimitFraction = 0.98)
{
    MatchReport report;
    const auto nyquist = sampleRate * 0.5;

    for (double f = 20.0; f < nyquist * upperLimitFraction; f *= 1.02)
    {
        const auto digitalDb = toDb (digital.magnitudeAt (f, sampleRate));
        const auto analogDb = 10.0 * std::log10 (juce::jmax (1.0e-12, analogMagSq (f / bandFrequency)));
        const auto error = std::abs (digitalDb - analogDb);

        if (error > report.worstErrorDb)
        {
            report.worstErrorDb = error;
            report.worstFrequency = f;
        }
    }

    return report;
}

void testCrampingCompensation()
{
    std::printf ("\nAnalog-matched design (cramping)\n");

    // The classic case: a wide bell high up the spectrum. The bilinear design is
    // forced to exactly 0 dB at Nyquist, so its top skirt gets crushed.
    {
        constexpr double sr = 44100.0;
        constexpr double f0 = 10000.0;
        constexpr double q = 1.0;
        constexpr double gainDb = 12.0;

        const auto A = std::pow (10.0, gainDb / 40.0);
        auto target = [A] (double u) { return nodo::dsp::analog::peakMagnitudeSquared (u, A, q); };

        const auto rbjError = compareToAnalog (
            nodo::dsp::BiquadCoefficients::peak (sr, f0, q, gainDb), target, sr, f0).worstErrorDb;
        const auto matchedError = compareToAnalog (
            nodo::dsp::matchedPeak (sr, f0, q, gainDb), target, sr, f0).worstErrorDb;

        std::printf ("        bell at 10 kHz: bilinear %.2f dB off, matched %.2f dB off\n",
                     rbjError, matchedError);

        check (rbjError > 2.0, "bilinear bell really is cramped up high (baseline)",
               rbjError, 2.0);
        // The acceptance test inside the designer works to about a decibel on its
        // probe grid, so a dense sweep can find a little more than that. What
        // matters is the ratio: several times closer, not perfect.
        check (matchedError < 0.8, "matched bell tracks the analog prototype",
               matchedError, 0.8);
        check (matchedError < rbjError * 0.25,
               "matched bell is at least four times closer than bilinear",
               matchedError, rbjError * 0.25);
    }

    // Shelves suffer the same squeeze through their transition region.
    {
        constexpr double sr = 44100.0;
        constexpr double f0 = 8000.0;
        constexpr double q = 0.707;
        constexpr double gainDb = 9.0;

        const auto A = std::pow (10.0, gainDb / 40.0);
        auto target = [A] (double u) { return nodo::dsp::analog::highShelfMagnitudeSquared (u, A, q); };

        const auto rbjError = compareToAnalog (
            nodo::dsp::BiquadCoefficients::highShelf (sr, f0, q, gainDb), target, sr, f0).worstErrorDb;
        const auto matchedError = compareToAnalog (
            nodo::dsp::matchedHighShelf (sr, f0, q, gainDb), target, sr, f0).worstErrorDb;

        std::printf ("        high shelf at 8 kHz: bilinear %.2f dB off, matched %.2f dB off\n",
                     rbjError, matchedError);

        check (matchedError < rbjError, "matched high shelf beats bilinear",
               matchedError, rbjError);
    }

    // The whole usable parameter space. Two properties matter here, and they are
    // what make the mode safe to leave switched on:
    //
    //   1. every accepted design is stable, and
    //   2. no accepted design is further from the analog prototype than the
    //      bilinear one it replaced.
    //
    // The second is the real guarantee. Switching this on can move a setting
    // closer to the analog response or leave it alone, but never make it worse.
    {
        double worstAccepted = 0.0, sumMatched = 0.0, sumBilinear = 0.0;
        int accepted = 0, fallbacks = 0, worseThanBilinear = 0;
        bool allStable = true;

        auto stable = [] (const nodo::dsp::BiquadCoefficients& x)
        {
            return std::isfinite (x.a1) && std::isfinite (x.a2)
                && std::isfinite (x.b0) && std::isfinite (x.b1) && std::isfinite (x.b2)
                && std::abs (x.a2) < 1.0
                && std::abs (x.a1) < 1.0 + x.a2 + 1.0e-9;
        };

        for (double sr : { 44100.0, 48000.0, 96000.0 })
        {
            for (double f0 = 25.0; f0 < sr * 0.47; f0 *= 1.4)
            {
                for (double q : { 0.2, 0.707, 1.5, 4.0, 10.0, 18.0 })
                {
                    for (double gainDb : { -24.0, -18.0, -9.0, -3.0, 3.0, 9.0, 18.0, 24.0 })
                    {
                        const auto A = std::pow (10.0, gainDb / 40.0);
                        nodo::dsp::BiquadCoefficients c;

                        auto examine = [&] (bool designed, auto analogTarget, auto bilinear)
                        {
                            if (! designed)
                            {
                                ++fallbacks;
                                return;
                            }

                            ++accepted;
                            allStable = allStable && stable (c);

                            const auto matchedError = compareToAnalog (c, analogTarget, sr, f0).worstErrorDb;
                            const auto bilinearError = compareToAnalog (bilinear, analogTarget, sr, f0).worstErrorDb;

                            worstAccepted = juce::jmax (worstAccepted, matchedError);
                            sumMatched += matchedError;
                            sumBilinear += bilinearError;

                            if (matchedError > bilinearError + 1.0e-6)
                                ++worseThanBilinear;
                        };

                        examine (nodo::dsp::designMatchedPeak (sr, f0, q, gainDb, c),
                                 [A, q] (double u) { return nodo::dsp::analog::peakMagnitudeSquared (u, A, q); },
                                 nodo::dsp::BiquadCoefficients::peak (sr, f0, q, gainDb));

                        examine (nodo::dsp::designMatchedLowShelf (sr, f0, q, gainDb, c),
                                 [A, q] (double u) { return nodo::dsp::analog::lowShelfMagnitudeSquared (u, A, q); },
                                 nodo::dsp::BiquadCoefficients::lowShelf (sr, f0, q, gainDb));

                        examine (nodo::dsp::designMatchedHighShelf (sr, f0, q, gainDb, c),
                                 [A, q] (double u) { return nodo::dsp::analog::highShelfMagnitudeSquared (u, A, q); },
                                 nodo::dsp::BiquadCoefficients::highShelf (sr, f0, q, gainDb));
                    }
                }
            }
        }

        std::printf ("        %d designs accepted, %d fell back to bilinear (%.0f%% coverage)\n",
                     accepted, fallbacks,
                     100.0 * accepted / juce::jmax (1, accepted + fallbacks));
        std::printf ("        mean error vs analog: matched %.3f dB, bilinear %.3f dB\n",
                     sumMatched / juce::jmax (1, accepted), sumBilinear / juce::jmax (1, accepted));

        check (allStable, "every accepted design is stable", allStable ? 1.0 : 0.0, 1.0);
        check (worseThanBilinear == 0,
               "no accepted design is worse than the one it replaced",
               (double) worseThanBilinear, 0.0);
        check (worstAccepted < 1.5, "worst accepted design stays within 1.5 dB of analog",
               worstAccepted, 1.5);
        check (sumMatched < sumBilinear * 0.35,
               "matched designs are on average at least three times closer",
               sumMatched / juce::jmax (1, accepted), sumBilinear * 0.35 / juce::jmax (1, accepted));
        check (accepted > 10 * fallbacks, "the matched design covers the great majority of settings",
               (double) accepted / juce::jmax (1, fallbacks), 10.0);
    }
}

/** Runs a stereo signal through a configured engine and reports the peak level
    of each output channel in dB, plus how far the output strays from the input.
*/
struct StereoResult
{
    double leftPeakDb { -120.0 };
    double rightPeakDb { -120.0 };
    double worstDeviation { 0.0 };   // linear, versus the input
};

StereoResult runEngine (nodo::eq::EqEngine& engine,
                        double sampleRate,
                        double frequency,
                        float leftAmplitude,
                        float rightAmplitude)
{
    constexpr int blockSize = 256;
    constexpr int warmupBlocks = 60;
    constexpr int measureBlocks = 40;

    juce::AudioBuffer<float> buffer (2, blockSize);
    const auto omega = 2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;

    StereoResult result;
    int sampleIndex = 0;

    for (int block = 0; block < warmupBlocks + measureBlocks; ++block)
    {
        const auto measuring = block >= warmupBlocks;
        const auto firstIndex = sampleIndex;

        for (int i = 0; i < blockSize; ++i, ++sampleIndex)
        {
            const auto s = (float) std::sin (omega * sampleIndex);
            buffer.setSample (0, i, s * leftAmplitude);
            buffer.setSample (1, i, s * rightAmplitude);
        }

        juce::dsp::AudioBlock<float> audioBlock (buffer);
        engine.process (audioBlock);

        if (! measuring)
            continue;

        for (int i = 0; i < blockSize; ++i)
        {
            const auto l = buffer.getSample (0, i);
            const auto r = buffer.getSample (1, i);

            result.leftPeakDb = juce::jmax (result.leftPeakDb, (double) std::abs (l));
            result.rightPeakDb = juce::jmax (result.rightPeakDb, (double) std::abs (r));

            const auto original = (float) std::sin (omega * (firstIndex + i));
            result.worstDeviation = juce::jmax (result.worstDeviation,
                (double) juce::jmax (std::abs (l - original * leftAmplitude),
                                     std::abs (r - original * rightAmplitude)));
        }
    }

    result.leftPeakDb = toDb (result.leftPeakDb);
    result.rightPeakDb = toDb (result.rightPeakDb);
    return result;
}

template <typename Configure>
void configureEngine (nodo::eq::EqEngine& engine, double sampleRate, Configure configure)
{
    engine.prepare ({ sampleRate, 256, 2 });
    engine.setFilterMode (nodo::eq::FilterMode::digital);

    std::array<nodo::eq::BandSettings, nodo::eq::numBands> bands {};
    bands[0].enabled = true;
    bands[0].type = nodo::eq::FilterType::bell;
    bands[0].frequency = 1000.0f;
    bands[0].gainDb = 12.0f;
    bands[0].q = 1.0f;

    configure (bands[0]);

    engine.setTargets (bands);
    engine.snapToTargets();
}

void makeEngine (nodo::eq::EqEngine& engine, double sampleRate,
                 nodo::eq::ChannelMode channel, float gainDb = 12.0f)
{
    configureEngine (engine, sampleRate, [channel, gainDb] (nodo::eq::BandSettings& b)
    {
        b.channel = channel;
        b.gainDb = gainDb;
    });
}

void testChannelRouting()
{
    std::printf ("\nMid/side and left/right routing\n");

    constexpr double sr = 48000.0;

    // A Mid band must be deaf to material that lives entirely in the sides.
    // Getting the rotation backwards would still "work" and sound plausible,
    // which is exactly why this is worth a test.
    {
        nodo::eq::EqEngine engine; makeEngine (engine, sr, nodo::eq::ChannelMode::mid);
        const auto sideOnly = runEngine (engine, sr, 1000.0, 1.0f, -1.0f);

        checkClose (sideOnly.leftPeakDb, 0.0, 0.05, "Mid band leaves a pure side signal alone");
        check (sideOnly.worstDeviation < 0.01, "  and does so sample for sample",
               sideOnly.worstDeviation, 0.01);

        nodo::eq::EqEngine engine2; makeEngine (engine2, sr, nodo::eq::ChannelMode::mid);
        const auto midOnly = runEngine (engine2, sr, 1000.0, 1.0f, 1.0f);
        checkClose (midOnly.leftPeakDb, 12.0, 0.1, "Mid band boosts centred material by its gain");
        checkClose (midOnly.rightPeakDb, 12.0, 0.1, "  equally on both outputs");
    }

    // And the mirror image.
    {
        nodo::eq::EqEngine engine; makeEngine (engine, sr, nodo::eq::ChannelMode::side);
        const auto midOnly = runEngine (engine, sr, 1000.0, 1.0f, 1.0f);

        checkClose (midOnly.leftPeakDb, 0.0, 0.05, "Side band leaves centred material alone");
        check (midOnly.worstDeviation < 0.01, "  and does so sample for sample",
               midOnly.worstDeviation, 0.01);

        nodo::eq::EqEngine engine2; makeEngine (engine2, sr, nodo::eq::ChannelMode::side);
        const auto sideOnly = runEngine (engine2, sr, 1000.0, 1.0f, -1.0f);
        checkClose (sideOnly.leftPeakDb, 12.0, 0.1, "Side band boosts out-of-phase material");
    }

    // Left/right is the easy case, but a mask off by one bit would swap them.
    {
        nodo::eq::EqEngine engine; makeEngine (engine, sr, nodo::eq::ChannelMode::left);
        const auto both = runEngine (engine, sr, 1000.0, 1.0f, 1.0f);

        checkClose (both.leftPeakDb, 12.0, 0.1, "Left band boosts the left channel");
        checkClose (both.rightPeakDb, 0.0, 0.05, "  and not the right one");

        nodo::eq::EqEngine engine2; makeEngine (engine2, sr, nodo::eq::ChannelMode::right);
        const auto both2 = runEngine (engine2, sr, 1000.0, 1.0f, 1.0f);

        checkClose (both2.leftPeakDb, 0.0, 0.05, "Right band leaves the left channel alone");
        checkClose (both2.rightPeakDb, 12.0, 0.1, "  and boosts the right one");
    }

    // The rotation into mid/side and back must be an exact inverse, or every
    // session with one M/S band would quietly lose or gain level.
    {
        nodo::eq::EqEngine engine; makeEngine (engine, sr, nodo::eq::ChannelMode::side, 0.0f);
        const auto asymmetric = runEngine (engine, sr, 1000.0, 1.0f, 0.35f);

        checkClose (asymmetric.leftPeakDb, 0.0, 0.01, "left/right round trip preserves the left channel");
        checkClose (asymmetric.rightPeakDb, toDb (0.35), 0.01, "  and the right one");
        check (asymmetric.worstDeviation < 1.0e-5, "  to within a millionth of full scale",
               asymmetric.worstDeviation, 1.0e-5);
    }

    // Stereo bands must behave exactly as they did before any of this existed.
    {
        nodo::eq::EqEngine engine; makeEngine (engine, sr, nodo::eq::ChannelMode::stereo);
        const auto both = runEngine (engine, sr, 1000.0, 1.0f, 0.5f);

        checkClose (both.leftPeakDb, 12.0, 0.1, "Stereo band boosts both channels");
        checkClose (both.rightPeakDb, toDb (0.5) + 12.0, 0.1, "  keeping their balance");
    }
}

void testGainComputer()
{
    std::printf ("\nDynamic gain computer\n");

    nodo::dsp::DynamicSettings settings;
    settings.thresholdDb = -20.0f;
    settings.ratio = 4.0f;
    settings.kneeDb = 6.0f;
    settings.rangeDb = -12.0f;

    checkClose (nodo::dsp::computeDynamicGainDb (-40.0f, settings), 0.0, 1.0e-6,
                "idle well below threshold");
    checkClose (nodo::dsp::computeDynamicGainDb (-23.1f, settings), 0.0, 1.0e-6,
                "still idle just below the knee");

    // 10 dB over, ratio 4 -> 7.5 dB of movement, signed to match the range.
    checkClose (nodo::dsp::computeDynamicGainDb (-10.0f, settings), -7.5, 0.01,
                "10 dB over at 4:1 gives 7.5 dB");

    // The range is a hard stop: a cut of 12 dB never becomes 13.
    checkClose (nodo::dsp::computeDynamicGainDb (0.0f, settings), -12.0, 0.01,
                "never travels past the band's own gain");

    // Exactly at the threshold the quadratic knee gives slope * knee / 8, which
    // for 4:1 over 6 dB is 0.5625 dB. Worth pinning to the closed form rather
    // than to a range: it is the one point where the two straight sections and
    // the curve all have to agree.
    checkClose (nodo::dsp::computeDynamicGainDb (-20.0f, settings), -0.5625, 0.001,
                "knee is exactly slope x knee / 8 at the threshold");
    checkClose (nodo::dsp::computeDynamicGainDb (-17.01f, settings),
                nodo::dsp::computeDynamicGainDb (-16.99f, settings), 0.02,
                "knee joins the straight section smoothly");

    // Monotonic: more level can never mean less work.
    auto monotonic = true;
    auto previous = 0.0f;

    for (float level = -60.0f; level <= 6.0f; level += 0.25f)
    {
        const auto value = nodo::dsp::computeDynamicGainDb (level, settings);

        if (value > previous + 1.0e-6f)
            monotonic = false;

        previous = value;
    }

    check (monotonic, "response is monotonic across the whole range",
           monotonic ? 1.0 : 0.0, 1.0);

    // Upward mode is the mirror image.
    settings.direction = nodo::dsp::DynamicDirection::below;
    settings.rangeDb = 6.0f;

    checkClose (nodo::dsp::computeDynamicGainDb (0.0f, settings), 0.0, 1.0e-6,
                "upward mode idles when the signal is loud");
    checkClose (nodo::dsp::computeDynamicGainDb (-28.0f, settings), 6.0, 0.01,
                "upward mode lifts by the full range when quiet");
}

void testEnvelopeFollower()
{
    std::printf ("\nEnvelope follower\n");

    constexpr double sr = 48000.0;

    nodo::dsp::EnvelopeFollower follower;
    follower.prepare (sr);
    follower.setTimes (10.0f, 200.0f);

    // A one-pole covers 1 - 1/e of the distance in its stated time constant.
    const auto attackSamples = (int) (0.010 * sr);

    for (int i = 0; i < attackSamples; ++i)
        follower.process (1.0f);

    checkClose (follower.getEnvelope(), 1.0 - std::exp (-1.0), 0.01,
                "attack reaches 63% in its stated time");

    for (int i = 0; i < (int) (0.5 * sr); ++i)
        follower.process (1.0f);

    checkClose (follower.getEnvelope(), 1.0, 0.001, "settles at the input level");

    const auto releaseSamples = (int) (0.200 * sr);

    for (int i = 0; i < releaseSamples; ++i)
        follower.process (0.0f);

    checkClose (follower.getEnvelope(), std::exp (-1.0), 0.01,
                "release falls to 37% in its stated time");
}

void testDynamicBand()
{
    std::printf ("\nDynamic band, end to end\n");

    constexpr double sr = 48000.0;

    /*  La banda vive donde dice su ganancia y viaja lo que dice su recorrido.
        Un de-esser es una banda plana con un recorrido negativo: en reposo no
        hace nada, y cuando la senal pasa el umbral baja doce decibelios. Antes
        esto se escribia al reves —ganancia -12 y reposo en plano— y encender la
        dinamica apagaba la banda, que es justo lo que se vio al probarlo.
    */
    auto configureDynamic = [] (nodo::eq::BandSettings& b)
    {
        b.frequency = 1000.0f;
        b.gainDb = 0.0f;            // donde vive: plana
        b.dynRangeDb = -12.0f;      // y cuanto se mueve desde ahi
        b.q = 1.0f;
        b.dynamic = true;
        b.dynamicMode = nodo::eq::DynamicMode::above;
        b.thresholdDb = -20.0f;
        b.ratio = 8.0f;
        b.attackMs = 5.0f;
        b.releaseMs = 60.0f;
    };

    // Loud material in the band: the band should pull down by its full range.
    {
        nodo::eq::EqEngine engine;
        configureEngine (engine, sr, configureDynamic);
        const auto loud = runEngine (engine, sr, 1000.0, 1.0f, 1.0f);

        checkClose (loud.leftPeakDb, -12.0, 0.4, "loud material pulls the band down by its range");
        checkClose (engine.getCurrentGainDb (0), -12.0, 0.2, "  and the reported gain agrees");
    }

    // Quiet material: the band should be doing nothing at all.
    {
        nodo::eq::EqEngine engine;
        configureEngine (engine, sr, configureDynamic);
        const auto quiet = runEngine (engine, sr, 1000.0, 0.01f, 0.01f);

        checkClose (quiet.leftPeakDb, toDb (0.01), 0.1, "quiet material passes untouched");
        checkClose (engine.getCurrentGainDb (0), 0.0, 0.1, "  band stays idle");
    }

    // The detector hears what the solo button hears, so a tone far outside the
    // band must not trigger it. Without the detector filter this would clamp on
    // anything loud anywhere in the mix.
    {
        nodo::eq::EqEngine engine;
        configureEngine (engine, sr, [configureDynamic] (nodo::eq::BandSettings& b)
        {
            configureDynamic (b);
            b.frequency = 6000.0f;
        });

        runEngine (engine, sr, 150.0, 1.0f, 1.0f);
        checkClose (engine.getCurrentGainDb (0), 0.0, 0.5,
                    "a band at 6 kHz ignores a loud 150 Hz tone");
    }

    // Upward mode on a quiet signal.
    {
        nodo::eq::EqEngine engine;
        configureEngine (engine, sr, [] (nodo::eq::BandSettings& b)
        {
            b.frequency = 1000.0f;
            b.gainDb = 0.0f;
            b.dynRangeDb = 6.0f;
            b.q = 1.0f;
            b.dynamic = true;
            b.dynamicMode = nodo::eq::DynamicMode::below;
            b.thresholdDb = -20.0f;
            b.ratio = 8.0f;
            b.attackMs = 5.0f;
            b.releaseMs = 60.0f;
        });

        const auto quiet = runEngine (engine, sr, 1000.0, 0.02f, 0.02f);
        checkClose (quiet.leftPeakDb, toDb (0.02) + 6.0, 0.4,
                    "upward mode lifts quiet material by the full range");
    }

    /*  Recorrido cero: encender la dinamica no cambia una sola muestra.

        Es la comprobacion que faltaba y la que motivo todo el cambio. Una
        banda plana con la dinamica encendida y sin recorrido tiene que ser
        transparente pase lo que pase con el nivel; y una banda que si tiene
        ganancia tiene que seguir dandola, en vez de caerse a plano al pulsar
        el boton.
    */
    {
        nodo::eq::EqEngine engine;
        configureEngine (engine, sr, [configureDynamic] (nodo::eq::BandSettings& b)
        {
            configureDynamic (b);
            b.dynRangeDb = 0.0f;
        });

        const auto flat = runEngine (engine, sr, 1000.0, 1.0f, 1.0f);
        check (flat.worstDeviation < 0.01, "a zero range band is transparent",
               flat.worstDeviation, 0.01);
    }

    {
        nodo::eq::EqEngine engine;
        configureEngine (engine, sr, [configureDynamic] (nodo::eq::BandSettings& b)
        {
            configureDynamic (b);
            b.gainDb = 7.0f;
            b.dynRangeDb = 0.0f;
        });

        const auto loud = runEngine (engine, sr, 1000.0, 1.0f, 1.0f);

        checkClose (loud.leftPeakDb, 7.0, 0.4,
                    "turning the dynamics on does not move a band that has no travel");
        checkClose (engine.getCurrentGainDb (0), 7.0, 0.2,
                    "  and it rests at its gain, not at zero");
    }
}

/** The smallest AudioProcessor that can hold a real parameter tree, so the
    factory presets can be loaded through exactly the path the plugin uses.
*/
class HarnessProcessor : public juce::AudioProcessor
{
public:
    HarnessProcessor()
        : apvts (*this, nullptr, "NodoEQ", nodo::eq::createParameterLayout()) {}

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    const juce::String getName() const override { return "Harness"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    juce::AudioProcessorValueTreeState apvts;
};

void testExternalSidechain()
{
    std::printf ("\nExternal sidechain for the dynamic bands\n");

    constexpr double sr = 48000.0;
    constexpr int blockSize = 256;

    auto configureDynamic = [] (nodo::eq::BandSettings& b)
    {
        b.frequency = 1000.0f;
        b.gainDb = 0.0f;            // plana en reposo
        b.dynRangeDb = -12.0f;      // y doce decibelios de recorrido
        b.q = 1.0f;
        b.dynamic = true;
        b.dynamicMode = nodo::eq::DynamicMode::above;
        b.thresholdDb = -20.0f;
        b.ratio = 8.0f;
        b.attackMs = 5.0f;
        b.releaseMs = 60.0f;
    };

    /*  Main signal quiet, sidechain loud, both at the band's frequency. With
        the sidechain selected the band should clamp anyway: that is the whole
        point of ducking one track from another.
    */
    auto run = [&] (bool useSidechain, float mainAmplitude, float sideAmplitude)
    {
        nodo::eq::EqEngine engine;
        configureEngine (engine, sr, configureDynamic);
        engine.setDynamicSidechain (useSidechain);

        juce::AudioBuffer<float> buffer (2, blockSize), sidechain (2, blockSize);
        const auto omega = 2.0 * juce::MathConstants<double>::pi * 1000.0 / sr;
        int index = 0;

        for (int block = 0; block < 80; ++block)
        {
            for (int i = 0; i < blockSize; ++i, ++index)
            {
                const auto sample = (float) std::sin (omega * index);

                for (int ch = 0; ch < 2; ++ch)
                {
                    buffer.setSample (ch, i, sample * mainAmplitude);
                    sidechain.setSample (ch, i, sample * sideAmplitude);
                }
            }

            juce::dsp::AudioBlock<float> audioBlock (buffer);
            const juce::dsp::AudioBlock<const float> sidechainBlock (sidechain);
            engine.process (audioBlock, &sidechainBlock);
        }

        return engine.getCurrentGainDb (0);
    };

    checkClose (run (true, 0.02f, 1.0f), -12.0, 0.3,
                "a loud sidechain ducks a quiet signal by the full range");

    checkClose (run (false, 0.02f, 1.0f), 0.0, 0.1,
                "the same signals with the sidechain off leave the band idle");

    checkClose (run (true, 1.0f, 0.005f), 0.0, 0.3,
                "a quiet sidechain leaves the band idle however loud the input is");

    // A sidechain that is connected but not selected must change nothing.
    checkClose (run (false, 1.0f, 0.005f), -12.0, 0.3,
                "with the sidechain off the band still hears its own input");
}

void testFactoryPresets()
{
    std::printf ("\nFactory presets\n");

    HarnessProcessor harness;
    nodo::PresetManager presets (harness.apvts, "Nodo EQ Test");
    presets.setFactoryPresets (nodo::eq::buildFactoryPresets());

    const auto& definitions = nodo::eq::getPresetDefinitions();
    const auto built = nodo::eq::buildFactoryPresets();

    check (! definitions.empty(), "there are presets at all",
           (double) definitions.size(), 1.0);

    /*  Mojibake canary. juce::String reads a plain const char* as ASCII, so a
        preset written in Spanish arrives on screen as "Sala pequeÃ±a" - the
        text is still a valid string, so nothing fails, and nobody finds out
        until they open the menu and look. These two characters cannot appear
        in correct Spanish, which makes them a cheap alarm.
    */
    auto mangled = 0;

    for (const auto& preset : built)
        if ((preset.name + preset.category + preset.description)
                .containsAnyOf (juce::String::fromUTF8 ("\u00c3\u00c2")))
            ++mangled;

    check (mangled == 0, "and the accents survived the trip to the screen",
           (double) mangled, 0.0);


    // Every identifier a preset writes to has to exist. A typo here would load
    // silently and do nothing, which is the worst kind of bug: no crash, no
    // message, just a preset that quietly is not the preset.
    int missingIds = 0, outOfRange = 0;

    for (const auto& preset : built)
    {
        for (const auto& [id, value] : preset.assignments)
        {
            auto* parameter = harness.apvts.getParameter (id);

            if (parameter == nullptr)
            {
                ++missingIds;
                std::printf ("        unknown parameter: %s\n", id.toRawUTF8());
                continue;
            }

            // convertTo0to1 clamps, so a value outside the range would be
            // silently rounded rather than rejected. Compare the round trip.
            const auto normalised = parameter->convertTo0to1 (value);
            const auto restored = parameter->convertFrom0to1 (normalised);

            if (std::abs (restored - value) > juce::jmax (0.01f, std::abs (value) * 0.001f))
            {
                ++outOfRange;
                std::printf ("        %s: %.3f does not survive the range (got %.3f)\n",
                             id.toRawUTF8(), value, restored);
            }
        }
    }

    check (missingIds == 0, "every preset writes to a parameter that exists",
           (double) missingIds, 0.0);
    check (outOfRange == 0, "every preset value fits its parameter's range",
           (double) outOfRange, 0.0);

    // Names are the menu key, so duplicates would make one preset unreachable.
    juce::StringArray names;
    auto duplicates = 0;

    for (const auto& preset : built)
    {
        if (names.contains (preset.name))
            ++duplicates;

        names.add (preset.name);
    }

    check (duplicates == 0, "no two presets share a name", (double) duplicates, 0.0);

    // Load every one and confirm the bands land where the definition says, and
    // that nothing is left switched on from the preset before it.
    int mismatches = 0, leftovers = 0, failedLoads = 0;

    for (const auto& definition : definitions)
    {
        if (! presets.loadFactoryPreset (definition.name))
        {
            ++failedLoads;
            continue;
        }

        for (size_t i = 0; i < definition.bands.size(); ++i)
        {
            const auto& wanted = definition.bands[i];
            const auto actual = nodo::eq::readBand (harness.apvts, (int) i);

            const auto ok = actual.enabled
                && actual.type == wanted.type
                && std::abs (actual.frequency - wanted.frequency) < wanted.frequency * 0.001f
                && std::abs (actual.gainDb - wanted.gainDb) < 0.01f
                && actual.channel == wanted.channel
                && actual.dynamic == wanted.dynamic;

            if (! ok)
            {
                ++mismatches;
                std::printf ("        %s band %d did not load as written\n",
                             definition.name, (int) i + 1);
            }
        }

        for (auto i = (int) definition.bands.size(); i < nodo::eq::numBands; ++i)
            if (nodo::eq::readBand (harness.apvts, i).enabled)
                ++leftovers;
    }

    check (failedLoads == 0, "every preset loads", (double) failedLoads, 0.0);
    check (mismatches == 0, "every band lands exactly as written", (double) mismatches, 0.0);
    check (leftovers == 0, "no preset leaves a band on from the one before it",
           (double) leftovers, 0.0);

    std::printf ("        %d presets across %d categories\n",
                 (int) definitions.size(), presets.getFactoryCategories().size());
}

void testDeEsserPreset()
{
    std::printf ("\nDe-esser preset, end to end\n");

    constexpr double sr = 48000.0;

    const nodo::eq::PresetDefinition* deEsser = nullptr;

    for (const auto& definition : nodo::eq::getPresetDefinitions())
        if (juce::String (definition.name) == "De-esser")
            deEsser = &definition;

    if (deEsser == nullptr || deEsser->bands.empty())
    {
        check (false, "the De-esser preset exists", 0.0, 1.0);
        return;
    }

    const auto& band = deEsser->bands.front();

    auto configure = [&band] (nodo::eq::BandSettings& b)
    {
        b.type = band.type;
        b.frequency = band.frequency;
        b.gainDb = band.gainDb;
        b.q = band.q;
        b.dynamic = band.dynamic;
        b.dynRangeDb = band.dynRangeDb;
        b.dynamicMode = band.dynamicMode;
        b.thresholdDb = band.thresholdDb;
        b.ratio = band.ratio;
        b.attackMs = band.attackMs;
        b.releaseMs = band.releaseMs;
    };

    // A loud tone in the sibilance region should be pulled down.
    {
        nodo::eq::EqEngine engine;
        configureEngine (engine, sr, configure);
        const auto sibilant = runEngine (engine, sr, band.frequency, 1.0f, 1.0f);

        check (sibilant.leftPeakDb < -4.0, "sibilance in the band gets pulled down",
               sibilant.leftPeakDb, -4.0);
    }

    // The same level in the midrange must go straight through: a de-esser that
    // reacts to a vowel is a de-esser nobody keeps switched on.
    {
        nodo::eq::EqEngine engine;
        configureEngine (engine, sr, configure);
        const auto vowel = runEngine (engine, sr, 500.0, 1.0f, 1.0f);

        checkClose (vowel.leftPeakDb, 0.0, 0.2, "midrange at the same level is untouched");
    }
}

void testStability()
{
    std::printf ("\nStability and numerical safety\n");

    juce::Random random (1234);

    // Sweep every filter type across the whole parameter space and confirm that
    // nothing produces a non-finite output, including the degenerate corners
    // (frequency at Nyquist, extreme Q, extreme gain).
    bool allFinite = true;
    double worstPeak = 0.0;

    for (int trial = 0; trial < 4000; ++trial)
    {
        const auto sr = random.nextBool() ? 44100.0 : 96000.0;
        const auto frequency = 10.0 + random.nextDouble() * sr * 0.55;   // deliberately past Nyquist
        const auto q = 0.05 + random.nextDouble() * 30.0;
        const auto gainDb = -30.0 + random.nextDouble() * 60.0;

        std::vector<nodo::dsp::BiquadCoefficients> designs {
            nodo::dsp::BiquadCoefficients::peak (sr, frequency, q, gainDb),
            nodo::dsp::BiquadCoefficients::lowShelf (sr, frequency, q, gainDb),
            nodo::dsp::BiquadCoefficients::highShelf (sr, frequency, q, gainDb),
            nodo::dsp::BiquadCoefficients::notch (sr, frequency, q),
            nodo::dsp::BiquadCoefficients::highPass (sr, frequency, q),
            nodo::dsp::BiquadCoefficients::lowPass (sr, frequency, q)
        };

        for (const auto& design : designs)
        {
            nodo::dsp::Biquad filter;
            filter.setCoefficients (design);

            for (int i = 0; i < 2000; ++i)
            {
                const auto out = filter.processSample (random.nextFloat() * 2.0f - 1.0f);

                if (! std::isfinite (out))
                    allFinite = false;

                worstPeak = juce::jmax (worstPeak, (double) std::abs (out));
            }
        }
    }

    check (allFinite, "no NaN or Inf across 4000 randomised designs", allFinite ? 1.0 : 0.0, 1.0);
    check (worstPeak < 1.0e6, "no runaway output from extreme settings", worstPeak, 1.0e6);
}

void testDenormalHandling()
{
    std::printf ("\nDenormal flushing\n");

    nodo::dsp::Biquad filter;
    filter.setCoefficients (nodo::dsp::BiquadCoefficients::lowPass (48000.0, 200.0, 0.707));

    for (int i = 0; i < 1000; ++i)
        filter.processSample (1.0f);

    // Feed silence and confirm the state decays to exactly zero rather than
    // grinding away in denormal territory.
    for (int i = 0; i < 200000; ++i)
    {
        filter.processSample (0.0f);
        filter.snapToZero();
    }

    const auto tail = std::abs (filter.processSample (0.0f));
    check (tail == 0.0f, "filter state reaches exact zero after silence", (double) tail, 0.0);
}

void testAnalyserSanity()
{
    std::printf ("\nSpectrum analyser\n");

    constexpr double sr = 48000.0;
    constexpr double toneFrequency = 1000.0;

    nodo::dsp::SpectrumAnalyser analyser;
    analyser.prepare (sr);

    juce::AudioBuffer<float> buffer (1, 512);

    // Full-scale sine, pushed for well over one FFT window.
    double phase = 0.0;
    const auto increment = 2.0 * juce::MathConstants<double>::pi * toneFrequency / sr;

    for (int block = 0; block < 40; ++block)
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            buffer.setSample (0, i, (float) std::sin (phase));
            phase += increment;
        }

        analyser.pushBlock (buffer);
    }

    analyser.update (0.033f);

    const auto& magnitudes = analyser.getMagnitudesDb();

    // Find the loudest bin and confirm it lands on the tone.
    int peakBin = 0;

    for (int bin = 1; bin < (int) magnitudes.size(); ++bin)
        if (magnitudes[(size_t) bin] > magnitudes[(size_t) peakBin])
            peakBin = bin;

    const auto peakFrequency = analyser.getBinFrequency (peakBin);
    checkClose (peakFrequency, toneFrequency, sr / (double) nodo::dsp::SpectrumAnalyser::fftSize * 1.5,
                "peak bin lands on the 1 kHz tone");

    // The tone sits between bins, so Hann scalloping loss costs up to 1.4 dB.
    checkClose (magnitudes[(size_t) peakBin], 0.0, 1.5,
                "full-scale sine reads about 0 dBFS");

    /*  La busqueda de picos tiene que caer sobre el tono con mucha mas precision
        que la separacion entre bins: doce hercios en el grave son casi medio
        semitono. Hoy no la usa la interfaz —el enganche al pico se quito porque
        bailaba con la musica— pero es una medida del analizador y sigue siendo
        cierta o no lo es.
    */
    double foundFrequency = 0.0;
    float foundDb = 0.0f;

    const auto found = analyser.findPeakBetween (400.0, 3000.0, -85.0f,
                                                 foundFrequency, foundDb);

    check (found, "the peak search finds a peak", found ? 1.0 : 0.0, 1.0);

    if (found)
    {
        checkClose (foundFrequency, toneFrequency, 2.0,
                    "and lands within 2 Hz of the tone");
        checkClose (foundDb, 0.0, 1.5, "and reports a sane level");
    }

    // Silence must not produce a phantom peak to click on.
    nodo::dsp::SpectrumAnalyser quiet;
    quiet.prepare (sr);
    quiet.update (0.5f);
    quiet.update (0.5f);

    double quietFrequency = 0.0;
    float quietDb = 0.0f;
    const auto foundInSilence = quiet.findPeakBetween (20.0, 20000.0, -85.0f,
                                                       quietFrequency, quietDb);

    check (! foundInSilence, "no phantom peak in silence", foundInSilence ? 1.0 : 0.0, 0.0);
}
} // namespace

int main()
{
    std::printf ("Nodo DSP verification\n=====================\n");

    testButterworthQ();
    testBellFilter();
    testCutFilters();
    testShelves();
    testNotch();
    testExtraShapes();
    testCrampingCompensation();
    testMatchedCuts();
    testSoloDesigns();
    testChannelRouting();
    testGainComputer();
    testEnvelopeFollower();
    testDynamicBand();
    testExternalSidechain();
    testFactoryPresets();
    testDeEsserPreset();
    testStability();
    testDenormalHandling();
    testAnalyserSanity();

    const auto compressor = nodo::tests::runCompressorTests();
    const auto limiter = nodo::tests::runLimiterTests();
    const auto deEsser = nodo::tests::runDeEsserTests();
    const auto gate = nodo::tests::runGateTests();
    const auto delayLine = nodo::tests::runDelayTests();
    const auto reverb = nodo::tests::runVerbTests();

    const auto totalChecks = checks + compressor.checks + limiter.checks + deEsser.checks
                           + gate.checks + delayLine.checks + reverb.checks;
    const auto totalFailures = failures + compressor.failures + limiter.failures
                             + deEsser.failures + gate.failures + delayLine.failures
                             + reverb.failures;

    std::printf ("\n---------------------------------------------------------------------\n");
    std::printf ("%d checks, %d failures  (EQ %d, Comp %d, Limit %d, Ess %d, Gate %d, Delay %d, Verb %d)\n",
                 totalChecks, totalFailures, checks, compressor.checks, limiter.checks,
                 deEsser.checks, gate.checks, delayLine.checks, reverb.checks);

    return totalFailures == 0 ? 0 : 1;
}
