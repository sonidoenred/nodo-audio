namespace nodo::dsp
{
namespace
{
    constexpr double pi = juce::MathConstants<double>::pi;

    /** How far a design may stray from the analog prototype before it is
        rejected outright. Expressed as a magnitude-SQUARED ratio rather than dB
        so the check needs no logarithms: 10^(0.5/10) is half a decibel.
    */
    const double toleranceRatio = std::pow (10.0, 0.5 / 10.0);

    /** Fixed part of the check grid, as fractions of Nyquist.

        An earlier version placed these relative to the band frequency, which
        left the ends of the spectrum unchecked and let designs through that were
        worse than the ones they replaced.
    */
    constexpr std::array<double, 22> checkFractions {
        0.002, 0.004, 0.008, 0.015, 0.03, 0.05, 0.08, 0.12, 0.17, 0.23, 0.30,
        0.37, 0.44, 0.51, 0.58, 0.65, 0.72, 0.79, 0.86, 0.92, 0.96, 0.995
    };

    /** Offsets for the adaptive part of the grid, in units of each feature's own
        half-power bandwidth.
    */
    constexpr std::array<double, 4> bandwidthMultiples { 0.5, 1.0, 2.0, 4.0 };

    constexpr size_t maxProbes = checkFractions.size() + 2 * bandwidthMultiples.size() * 2;

    /** s = sin^2(w/2) for the fixed grid, computed once at startup. */
    const std::array<double, checkFractions.size()>& fixedGridS()
    {
        static const auto table = []
        {
            std::array<double, checkFractions.size()> values {};

            for (size_t i = 0; i < checkFractions.size(); ++i)
            {
                const auto halfW = checkFractions[i] * pi * 0.5;
                values[i] = std::sin (halfW) * std::sin (halfW);
            }

            return values;
        }();

        return table;
    }

    /** A resonance or anti-resonance of the analog prototype.

        Both matter. A shelf's pole sits at w0*sqrt(A) and its notch at
        w0/sqrt(A), and for a high Q it is the notch — twenty-odd dB deep and a
        few percent wide — that a coarse check walks straight past.
    */
    struct Feature { double w, q; };

    /** One point at which a candidate is compared with the prototype.
        @param s  sin^2(w/2), the variable the magnitude algebra is written in
        @param u  frequency / band frequency, the variable the prototype uses
    */
    struct Probe { double s, u; };

    struct ProbeSet
    {
        std::array<Probe, maxProbes> points {};
        size_t count { 0 };

        void add (double s, double u) noexcept
        {
            if (count < points.size())
                points[count++] = { s, u };
        }
    };

    /** Roots of a normalised second-order section placed by the matched
        Z-transform, z = exp(sT).

        This is the part that actually cures cramping. The bilinear transform
        squeezes a section's bandwidth by a factor of sin(w0)/w0 — nearly five to
        one at 20 kHz in a 48 kHz session — so the resonance ends up far narrower
        than the analog filter it claims to imitate. The exponential map has no
        such warping. The same routine places the zeros, since an analog zero
        pair is just another second-order section.

        @param wn  natural frequency in digital radians
        @param qd  the section's Q
    */
    void matchedSection (double wn, double qd, double& c1, double& c2) noexcept
    {
        qd = juce::jmax (1.0e-4, qd);

        if (qd > 0.5)
        {
            const auto damping = wn / (2.0 * qd);
            const auto ringing = wn * std::sqrt (1.0 - 1.0 / (4.0 * qd * qd));
            const auto radius = std::exp (-damping);

            c1 = -2.0 * radius * std::cos (ringing);
            c2 = radius * radius;
        }
        else
        {
            // Overdamped: two real roots rather than a conjugate pair.
            const auto spread = std::sqrt (1.0 / (4.0 * qd * qd) - 1.0);
            const auto p1 = std::exp (-wn * (1.0 / (2.0 * qd) - spread));
            const auto p2 = std::exp (-wn * (1.0 / (2.0 * qd) + spread));

            c1 = -(p1 + p2);
            c2 = p1 * p2;
        }
    }

    /** |c0 + c1 z^-1 + c2 z^-2|^2 as a quadratic in s = sin^2(w/2).

        Derivation: expanding the modulus gives
            c0^2 + c1^2 + c2^2 + 2c1(c0+c2)cos(w) + 2c0c2 cos(2w)
        and substituting cos(w) = 1-2s, cos(2w) = 1-8s(1-s) collects into the
        form below. This is what makes the three-point match a linear system and
        the fit check free of trigonometry.
    */
    double magnitudeSquaredFromS (double c0, double c1, double c2, double s) noexcept
    {
        const auto sum = c0 + c1 + c2;
        return sum * sum - 4.0 * s * (c1 * (c0 + c2) + 4.0 * c0 * c2 * (1.0 - s));
    }

    double denominatorMagnitudeSquared (double a1, double a2, double s) noexcept
    {
        return magnitudeSquaredFromS (1.0, a1, a2, s);
    }

    /** Recovers b0, b1, b2 from the three symmetric quantities

            alpha = (b0+b1+b2)^2,  beta = b1(b0+b2),  gamma = b0*b2

        Both branches give the same magnitude response — only the phase differs —
        so the first one that produces real coefficients is taken.
    */
    bool recoverNumerator (double alpha, double beta, double gamma,
                           double& b0, double& b1, double& b2) noexcept
    {
        if (! std::isfinite (alpha) || ! std::isfinite (beta) || ! std::isfinite (gamma)
            || alpha < 0.0)
            return false;

        const auto sum = std::sqrt (alpha);
        const auto disc = sum * sum - 4.0 * beta;

        if (disc < 0.0)
            return false;

        const auto root = std::sqrt (disc);

        for (const auto outer : { (sum + root) * 0.5, (sum - root) * 0.5 })
        {
            const auto inner = outer * outer - 4.0 * gamma;

            if (inner < 0.0)
                continue;

            const auto innerRoot = std::sqrt (inner);

            b0 = (outer + innerRoot) * 0.5;
            b2 = (outer - innerRoot) * 0.5;
            b1 = sum - outer;

            if (std::isfinite (b0) && std::isfinite (b1) && std::isfinite (b2))
                return true;
        }

        return false;
    }

    ProbeSet buildProbes (double w0, const std::array<Feature, 2>& features)
    {
        ProbeSet probes;

        const auto& table = fixedGridS();

        for (size_t i = 0; i < checkFractions.size(); ++i)
            probes.add (table[i], checkFractions[i] * pi / w0);

        for (const auto& feature : features)
        {
            if (! (feature.w > 1.0e-6))
                continue;

            const auto bandwidth = 1.0 / juce::jmax (0.05, feature.q);

            for (const auto multiple : bandwidthMultiples)
            {
                for (const auto sign : { -1.0, 1.0 })
                {
                    const auto w = feature.w * (1.0 + sign * multiple * bandwidth);

                    if (w <= 1.0e-6 || w >= pi)
                        continue;

                    const auto halfSin = std::sin (w * 0.5);
                    probes.add (halfSin * halfSin, w / w0);
                }
            }
        }

        return probes;
    }

    /** Worst mismatch between a candidate and the prototype over the probes, as
        a magnitude-squared ratio (1.0 is perfect).

        This is the only thing standing between the user and a filter that
        satisfies its constraints exactly and then does something absurd between
        them, which is what an unchecked three-point solve will happily produce.
    */
    template <typename AnalogMagnitudeSquared>
    double fitErrorRatio (const BiquadCoefficients& c, AnalogMagnitudeSquared target,
                          const ProbeSet& probes) noexcept
    {
        auto worst = 1.0;

        for (size_t i = 0; i < probes.count; ++i)
        {
            const auto& probe = probes.points[i];

            const auto den = denominatorMagnitudeSquared (c.a1, c.a2, probe.s);
            const auto num = magnitudeSquaredFromS (c.b0, c.b1, c.b2, probe.s);

            if (! (den > 0.0) || ! (num >= 0.0))
                return std::numeric_limits<double>::infinity();

            const auto wanted = target (probe.u);

            if (! (wanted > 0.0))
                continue;

            const auto ratio = juce::jmax (1.0e-12, num / den) / wanted;
            worst = juce::jmax (worst, juce::jmax (ratio, 1.0 / juce::jmax (1.0e-12, ratio)));
        }

        return worst;
    }

    /** The shared body of all three designs.

        Two candidates are built and measured against the prototype, with the
        classic bilinear filter as the incumbent. The closest one wins. That
        framing is what makes this safe to switch on by default: it can only ever
        move a setting closer to the analog response, never further.

        @param w0      band frequency in digital radians
        @param poleWn  natural frequency of the prototype's poles, digital radians
        @param poleQ   Q of the prototype's poles
        @param zeroWn  natural frequency of the prototype's zeros
        @param zeroQ   Q of the prototype's zeros
    */
    /** Where the analog prototype's zeros are, which decides what the faithful
        numerator looks like once it is mapped into the digital domain.

        - finite: a conjugate pair at some frequency, as in a bell or a shelf.
        - atOrigin: a double zero at DC, as in a high pass. Its image is a double
          zero at z = 1.
        - atInfinity: no finite zeros at all, as in a low pass. Its image is a
          constant numerator — which is precisely what stops the digital filter
          from collapsing to nothing at Nyquist the way the bilinear one does.
    */
    enum class ZeroKind { finite, atOrigin, atInfinity };

    template <typename AnalogMagnitudeSquared>
    bool designMatched (double w0, double poleWn, double poleQ,
                        double zeroWn, double zeroQ,
                        AnalogMagnitudeSquared target,
                        const BiquadCoefficients& bilinear,
                        BiquadCoefficients& result,
                        ZeroKind zeroKind = ZeroKind::finite) noexcept
    {
        // Past this point the matched Z-transform folds the angle back down the
        // spectrum, putting the feature in the wrong place entirely.
        if (! (poleWn > 1.0e-6) || poleWn > 0.98 * pi)
            return false;

        double a1 = 0.0, a2 = 0.0;
        matchedSection (poleWn, poleQ, a1, a2);

        if (! std::isfinite (a1) || ! std::isfinite (a2) || std::abs (a2) >= 0.9999999)
            return false;

        const auto sBand = std::sin (w0 * 0.5) * std::sin (w0 * 0.5);

        if (sBand < 1.0e-9 || sBand > 1.0 - 1.0e-9)
            return false;

        const auto dBand = denominatorMagnitudeSquared (a1, a2, sBand);
        const auto probes = buildProbes (w0, { Feature { poleWn, poleQ },
                                               Feature { zeroWn, zeroQ } });

        auto best = fitErrorRatio (bilinear, target, probes);
        auto found = false;

        auto consider = [&] (const BiquadCoefficients& candidate) noexcept
        {
            const auto error = fitErrorRatio (candidate, target, probes);

            if (error < best)
            {
                best = error;
                result = candidate;
                found = true;
            }
        };

        // CANDIDATE A: map the zeros exactly as the poles were mapped, then scale
        // once so the band frequency reads correctly. Structurally the most
        // faithful option, because every feature of the prototype lands at the
        // right frequency with the right width. This one wins almost every time.
        {
            auto z1 = 0.0, z2 = 0.0;
            auto haveZeros = false;

            switch (zeroKind)
            {
                case ZeroKind::finite:
                    if (zeroWn > 1.0e-6 && zeroWn < 0.98 * pi)
                    {
                        matchedSection (zeroWn, zeroQ, z1, z2);
                        haveZeros = true;
                    }
                    break;

                case ZeroKind::atOrigin:
                    // Double zero at s = 0 maps to a double zero at z = 1.
                    z1 = -2.0;
                    z2 = 1.0;
                    haveZeros = true;
                    break;

                case ZeroKind::atInfinity:
                    // No finite zeros: the numerator is a constant.
                    haveZeros = true;
                    break;
            }

            if (haveZeros)
            {
                const auto numAtBand = magnitudeSquaredFromS (1.0, z1, z2, sBand);

                if (numAtBand > 0.0)
                {
                    const auto k = std::sqrt (target (1.0) * dBand / numAtBand);

                    if (std::isfinite (k) && k > 0.0)
                        consider ({ k, k * z1, k * z2, a1, a2 });
                }
            }
        }

        // CANDIDATE B: solve the magnitude match exactly at DC, the band
        // frequency and Nyquist. Picks up the cases where mapping the zeros is
        // not quite enough.
        {
            const auto dDc  = denominatorMagnitudeSquared (a1, a2, 0.0);
            const auto dNyq = denominatorMagnitudeSquared (a1, a2, 1.0);

            const auto alpha = target (0.0) * dDc;
            const auto beta  = (alpha - target (pi / w0) * dNyq) * 0.25;
            const auto gamma = (alpha - 4.0 * sBand * beta - target (1.0) * dBand)
                             / (16.0 * sBand * (1.0 - sBand));

            BiquadCoefficients candidate { 0.0, 0.0, 0.0, a1, a2 };

            if (recoverNumerator (alpha, beta, gamma, candidate.b0, candidate.b1, candidate.b2))
                consider (candidate);
        }

        // Refuse anything that only looks good next to an incumbent that was
        // itself hopeless.
        return found && best <= toleranceRatio * toleranceRatio;
    }

    double gainToA (double gainDb) noexcept
    {
        return std::pow (10.0, juce::jlimit (-40.0, 40.0, gainDb) / 40.0);
    }

    bool bandRadians (double sampleRate, double frequency, double& w0) noexcept
    {
        if (! (sampleRate > 0.0))
            return false;

        w0 = 2.0 * pi * frequency / sampleRate;
        return w0 > 1.0e-5 && w0 < 0.999 * pi;
    }
}

//==============================================================================
bool designMatchedPeak (double sampleRate, double frequency, double q, double gainDb,
                        BiquadCoefficients& result) noexcept
{
    double w0 = 0.0;

    if (! bandRadians (sampleRate, frequency, w0))
        return false;

    const auto A = gainToA (gainDb);
    q = juce::jlimit (0.05, 40.0, q);

    // Analog peaking prototype, s normalised to the band frequency:
    //     H(s) = (s^2 + (A/q)s + 1) / (s^2 + (1/(Aq))s + 1)
    // Poles and zeros share the band frequency; only their Q differs.
    return designMatched (w0, w0, A * q, w0, q / A,
                          [A, q] (double u) { return analog::peakMagnitudeSquared (u, A, q); },
                          BiquadCoefficients::peak (sampleRate, frequency, q, gainDb),
                          result);
}

bool designMatchedLowShelf (double sampleRate, double frequency, double q, double gainDb,
                            BiquadCoefficients& result) noexcept
{
    double w0 = 0.0;

    if (! bandRadians (sampleRate, frequency, w0))
        return false;

    const auto A = gainToA (gainDb);
    q = juce::jlimit (0.05, 40.0, q);

    // Poles at w0/sqrt(A), zeros at w0*sqrt(A); both keep the band's Q.
    return designMatched (w0, w0 / std::sqrt (A), q, w0 * std::sqrt (A), q,
                          [A, q] (double u) { return analog::lowShelfMagnitudeSquared (u, A, q); },
                          BiquadCoefficients::lowShelf (sampleRate, frequency, q, gainDb),
                          result);
}

bool designMatchedHighShelf (double sampleRate, double frequency, double q, double gainDb,
                             BiquadCoefficients& result) noexcept
{
    double w0 = 0.0;

    if (! bandRadians (sampleRate, frequency, w0))
        return false;

    const auto A = gainToA (gainDb);
    q = juce::jlimit (0.05, 40.0, q);

    // Mirror image of the low shelf: poles at w0*sqrt(A), zeros at w0/sqrt(A).
    return designMatched (w0, w0 * std::sqrt (A), q, w0 / std::sqrt (A), q,
                          [A, q] (double u) { return analog::highShelfMagnitudeSquared (u, A, q); },
                          BiquadCoefficients::highShelf (sampleRate, frequency, q, gainDb),
                          result);
}

//==============================================================================
bool designMatchedLowPass (double sampleRate, double frequency, double q,
                           BiquadCoefficients& result) noexcept
{
    double w0 = 0.0;

    if (! bandRadians (sampleRate, frequency, w0))
        return false;

    q = juce::jlimit (0.05, 40.0, q);

    // H(s) = 1 / (s^2 + s/q + 1): poles at the corner, no finite zeros.
    return designMatched (w0, w0, q, 0.0, 0.0,
                          [q] (double u) { return analog::lowPassMagnitudeSquared (u, q); },
                          BiquadCoefficients::lowPass (sampleRate, frequency, q),
                          result,
                          ZeroKind::atInfinity);
}

bool designMatchedHighPass (double sampleRate, double frequency, double q,
                            BiquadCoefficients& result) noexcept
{
    double w0 = 0.0;

    if (! bandRadians (sampleRate, frequency, w0))
        return false;

    q = juce::jlimit (0.05, 40.0, q);

    // H(s) = s^2 / (s^2 + s/q + 1): same poles, a double zero at DC.
    return designMatched (w0, w0, q, 0.0, 0.0,
                          [q] (double u) { return analog::highPassMagnitudeSquared (u, q); },
                          BiquadCoefficients::highPass (sampleRate, frequency, q),
                          result,
                          ZeroKind::atOrigin);
}

BiquadCoefficients matchedLowPass (double sampleRate, double frequency, double q) noexcept
{
    BiquadCoefficients result;

    return designMatchedLowPass (sampleRate, frequency, q, result)
         ? result : BiquadCoefficients::lowPass (sampleRate, frequency, q);
}

BiquadCoefficients matchedHighPass (double sampleRate, double frequency, double q) noexcept
{
    BiquadCoefficients result;

    return designMatchedHighPass (sampleRate, frequency, q, result)
         ? result : BiquadCoefficients::highPass (sampleRate, frequency, q);
}

BiquadCoefficients matchedPeak (double sampleRate, double frequency, double q, double gainDb) noexcept
{
    BiquadCoefficients result;
    return designMatchedPeak (sampleRate, frequency, q, gainDb, result)
         ? result
         : BiquadCoefficients::peak (sampleRate, frequency, q, gainDb);
}

BiquadCoefficients matchedLowShelf (double sampleRate, double frequency, double q, double gainDb) noexcept
{
    BiquadCoefficients result;
    return designMatchedLowShelf (sampleRate, frequency, q, gainDb, result)
         ? result
         : BiquadCoefficients::lowShelf (sampleRate, frequency, q, gainDb);
}

BiquadCoefficients matchedHighShelf (double sampleRate, double frequency, double q, double gainDb) noexcept
{
    BiquadCoefficients result;
    return designMatchedHighShelf (sampleRate, frequency, q, gainDb, result)
         ? result
         : BiquadCoefficients::highShelf (sampleRate, frequency, q, gainDb);
}
} // namespace nodo::dsp
