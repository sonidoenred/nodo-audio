namespace nodo::dsp
{
namespace
{
    struct Common
    {
        double w0, cosw0, alpha;
    };

    Common commonTerms (double sampleRate, double frequency, double q) noexcept
    {
        const auto safeRate = juce::jmax (8000.0, sampleRate);
        const auto safeFreq = juce::jlimit (5.0, safeRate * 0.4999, frequency);
        const auto safeQ    = juce::jlimit (0.025, 60.0, q);

        Common c {};
        c.w0    = 2.0 * juce::MathConstants<double>::pi * safeFreq / safeRate;
        c.cosw0 = std::cos (c.w0);
        c.alpha = std::sin (c.w0) / (2.0 * safeQ);
        return c;
    }

    BiquadCoefficients normalise (double b0, double b1, double b2,
                                  double a0, double a1, double a2) noexcept
    {
        const auto inv = 1.0 / (std::abs (a0) < 1.0e-12 ? 1.0e-12 : a0);
        return { b0 * inv, b1 * inv, b2 * inv, a1 * inv, a2 * inv };
    }
}

BiquadCoefficients BiquadCoefficients::peak (double sampleRate, double frequency,
                                             double q, double gainDb) noexcept
{
    const auto c = commonTerms (sampleRate, frequency, q);
    const auto A = std::pow (10.0, juce::jlimit (-40.0, 40.0, gainDb) / 40.0);

    return normalise (1.0 + c.alpha * A, -2.0 * c.cosw0, 1.0 - c.alpha * A,
                      1.0 + c.alpha / A, -2.0 * c.cosw0, 1.0 - c.alpha / A);
}

BiquadCoefficients BiquadCoefficients::lowShelf (double sampleRate, double frequency,
                                                 double q, double gainDb) noexcept
{
    const auto c = commonTerms (sampleRate, frequency, q);
    const auto A = std::pow (10.0, juce::jlimit (-40.0, 40.0, gainDb) / 40.0);
    const auto sqrtA2alpha = 2.0 * std::sqrt (A) * c.alpha;

    return normalise (       A * ((A + 1.0) - (A - 1.0) * c.cosw0 + sqrtA2alpha),
                       2.0 * A * ((A - 1.0) - (A + 1.0) * c.cosw0),
                             A * ((A + 1.0) - (A - 1.0) * c.cosw0 - sqrtA2alpha),
                                  (A + 1.0) + (A - 1.0) * c.cosw0 + sqrtA2alpha,
                      -2.0 *      ((A - 1.0) + (A + 1.0) * c.cosw0),
                                  (A + 1.0) + (A - 1.0) * c.cosw0 - sqrtA2alpha);
}

BiquadCoefficients BiquadCoefficients::highShelf (double sampleRate, double frequency,
                                                  double q, double gainDb) noexcept
{
    const auto c = commonTerms (sampleRate, frequency, q);
    const auto A = std::pow (10.0, juce::jlimit (-40.0, 40.0, gainDb) / 40.0);
    const auto sqrtA2alpha = 2.0 * std::sqrt (A) * c.alpha;

    return normalise (        A * ((A + 1.0) + (A - 1.0) * c.cosw0 + sqrtA2alpha),
                      -2.0 *  A * ((A - 1.0) + (A + 1.0) * c.cosw0),
                              A * ((A + 1.0) + (A - 1.0) * c.cosw0 - sqrtA2alpha),
                                   (A + 1.0) - (A - 1.0) * c.cosw0 + sqrtA2alpha,
                       2.0 *      ((A - 1.0) - (A + 1.0) * c.cosw0),
                                   (A + 1.0) - (A - 1.0) * c.cosw0 - sqrtA2alpha);
}

BiquadCoefficients BiquadCoefficients::notch (double sampleRate, double frequency, double q) noexcept
{
    const auto c = commonTerms (sampleRate, frequency, q);
    return normalise (1.0, -2.0 * c.cosw0, 1.0,
                      1.0 + c.alpha, -2.0 * c.cosw0, 1.0 - c.alpha);
}

BiquadCoefficients BiquadCoefficients::highPass (double sampleRate, double frequency, double q) noexcept
{
    const auto c = commonTerms (sampleRate, frequency, q);
    const auto k = (1.0 + c.cosw0) * 0.5;

    return normalise (k, -(1.0 + c.cosw0), k,
                      1.0 + c.alpha, -2.0 * c.cosw0, 1.0 - c.alpha);
}

BiquadCoefficients BiquadCoefficients::lowPass (double sampleRate, double frequency, double q) noexcept
{
    const auto c = commonTerms (sampleRate, frequency, q);
    const auto k = (1.0 - c.cosw0) * 0.5;

    return normalise (k, 1.0 - c.cosw0, k,
                      1.0 + c.alpha, -2.0 * c.cosw0, 1.0 - c.alpha);
}

BiquadCoefficients BiquadCoefficients::bandPass (double sampleRate, double frequency, double q) noexcept
{
    const auto c = commonTerms (sampleRate, frequency, q);
    return normalise (c.alpha, 0.0, -c.alpha,
                      1.0 + c.alpha, -2.0 * c.cosw0, 1.0 - c.alpha);
}

BiquadCoefficients BiquadCoefficients::allPass (double sampleRate, double frequency, double q) noexcept
{
    const auto c = commonTerms (sampleRate, frequency, q);
    return normalise (1.0 - c.alpha, -2.0 * c.cosw0, 1.0 + c.alpha,
                      1.0 + c.alpha, -2.0 * c.cosw0, 1.0 - c.alpha);
}

double BiquadCoefficients::magnitudeAt (double frequency, double sampleRate) const noexcept
{
    if (sampleRate <= 0.0)
        return 1.0;

    // Evaluate H(z) on the unit circle at z = e^{jw}.
    const auto w = 2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;
    const auto cw = std::cos (w), sw = std::sin (w);
    const auto c2w = std::cos (2.0 * w), s2w = std::sin (2.0 * w);

    const auto numRe = b0 + b1 * cw + b2 * c2w;
    const auto numIm =    - b1 * sw - b2 * s2w;
    const auto denRe = 1.0 + a1 * cw + a2 * c2w;
    const auto denIm =     - a1 * sw - a2 * s2w;

    const auto denMag = std::sqrt (denRe * denRe + denIm * denIm);

    if (denMag < 1.0e-12)
        return 1.0;

    return std::sqrt (numRe * numRe + numIm * numIm) / denMag;
}
} // namespace nodo::dsp
