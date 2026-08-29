namespace nodo::dsp
{
SpectrumAnalyser::SpectrumAnalyser()
{
    ring.assign ((size_t) ringSize, 0.0f);
    scratch.assign ((size_t) fftSize * 2, 0.0f);
    smoothedDb.assign ((size_t) numBins, -100.0f);

    // For a real sine of amplitude A, the FFT bin magnitude is A * N / 2. A Hann
    // window has a coherent gain of 0.5, which halves that again, so the scaling
    // back to amplitude is 4 / N. Getting this wrong is how an analyser ends up
    // reading a full-scale signal 6 dB low and nobody notices for a year.
    windowGainCorrection = 4.0f / (float) fftSize;
}

void SpectrumAnalyser::prepare (double sampleRate)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    reset();
}

void SpectrumAnalyser::reset()
{
    std::fill (ring.begin(), ring.end(), 0.0f);
    std::fill (smoothedDb.begin(), smoothedDb.end(), floorDb);
    writePos.store (0, std::memory_order_release);
}

void SpectrumAnalyser::pushBlock (const juce::AudioBuffer<float>& buffer)
{
    const auto numSamples  = buffer.getNumSamples();
    const auto numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0)
        return;

    auto pos = writePos.load (std::memory_order_relaxed);
    const auto scale = 1.0f / (float) numChannels;

    for (int i = 0; i < numSamples; ++i)
    {
        float sum = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
            sum += buffer.getReadPointer (ch)[i];

        ring[(size_t) pos] = sum * scale;
        pos = (pos + 1) % ringSize;
    }

    writePos.store (pos, std::memory_order_release);
}

bool SpectrumAnalyser::findPeakBetween (double lowHz,
                                        double highHz,
                                        float thresholdDb,
                                        double& frequencyOut,
                                        float& magnitudeDbOut) const
{
    if (smoothedDb.size() < 3 || currentSampleRate <= 0.0)
        return false;

    const auto binSpacing = currentSampleRate / (double) fftSize;
    auto bestDb = thresholdDb;
    auto bestBin = -1;

    for (int bin = 2; bin < (int) smoothedDb.size() - 1; ++bin)
    {
        const auto frequency = (double) bin * binSpacing;

        if (frequency < lowHz)
            continue;

        if (frequency > highHz)
            break;

        const auto db = smoothedDb[(size_t) bin];

        // Only genuine local maxima: without this the "peak" slides around the
        // slope of a broad hump and the marker never sits still.
        if (db <= smoothedDb[(size_t) bin - 1] || db < smoothedDb[(size_t) bin + 1])
            continue;

        if (db > bestDb)
        {
            bestDb = db;
            bestBin = bin;
        }
    }

    if (bestBin < 0)
        return false;

    const auto left   = (double) smoothedDb[(size_t) bestBin - 1];
    const auto centre = (double) smoothedDb[(size_t) bestBin];
    const auto right  = (double) smoothedDb[(size_t) bestBin + 1];

    const auto denominator = left - 2.0 * centre + right;
    const auto offset = std::abs (denominator) < 1.0e-9
                      ? 0.0
                      : juce::jlimit (-0.5, 0.5, 0.5 * (left - right) / denominator);

    frequencyOut = ((double) bestBin + offset) * binSpacing;
    magnitudeDbOut = (float) centre;
    return true;
}

bool SpectrumAnalyser::update (float secondsSinceLastCall)
{
    const auto pos = writePos.load (std::memory_order_acquire);

    // Copy the most recent fftSize samples, unwrapping the ring.
    auto start = pos - fftSize;
    while (start < 0)
        start += ringSize;

    for (int i = 0; i < fftSize; ++i)
        scratch[(size_t) i] = ring[(size_t) ((start + i) % ringSize)];

    std::fill (scratch.begin() + fftSize, scratch.end(), 0.0f);

    window.multiplyWithWindowingTable (scratch.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (scratch.data(), true);

    const auto decay = decayDbPerSecond * juce::jlimit (0.0f, 0.5f, secondsSinceLastCall);

    for (int bin = 0; bin < numBins; ++bin)
    {
        const auto magnitude = scratch[(size_t) bin] * windowGainCorrection;
        const auto db = juce::jmax (floorDb, juce::Decibels::gainToDecibels (magnitude, floorDb));

        auto& smoothed = smoothedDb[(size_t) bin];

        // Instant attack so transients register, slow decay so the eye can read it.
        smoothed = db > smoothed ? db : juce::jmax (db, smoothed - decay);
    }

    return true;
}
} // namespace nodo::dsp
