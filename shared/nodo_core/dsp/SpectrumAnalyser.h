#pragma once

#include <atomic>
#include <vector>
#include <juce_dsp/juce_dsp.h>

namespace nodo::dsp
{
/** Real-time spectrum analyser feeding the plugin GUIs.

    The audio thread writes a mono sum into a lock-free ring buffer and never
    blocks. The GUI thread takes a snapshot of the most recent window, applies a
    Hann window and an FFT, and keeps a smoothed magnitude curve with an instant
    attack and a controllable decay so the display does not flicker.

    A torn read is possible in theory (the audio thread may overwrite part of the
    window while the GUI copies it) but at 4096 samples and ~30 fps it is both
    rare and invisible. Locking here would be far worse: it would put the audio
    thread at the mercy of the message thread.
*/
class SpectrumAnalyser
{
public:
    static constexpr int fftOrder   = 12;
    static constexpr int fftSize    = 1 << fftOrder;   // 4096
    static constexpr int numBins    = fftSize / 2;
    static constexpr int ringSize   = fftSize * 2;

    SpectrumAnalyser();

    void prepare (double sampleRate);
    void reset();

    /** Audio thread. Sums all channels to mono and stores them. */
    void pushBlock (const juce::AudioBuffer<float>& buffer);

    /** GUI thread. Recomputes the smoothed magnitude curve.
        @param secondsSinceLastCall  used to scale the decay
        @returns true if magnitudes changed
    */
    bool update (float secondsSinceLastCall);

    /** GUI thread. Magnitudes in dBFS, index 0 == DC, index numBins-1 == Nyquist. */
    const std::vector<float>& getMagnitudesDb() const noexcept { return smoothedDb; }

    float getBinFrequency (int bin) const noexcept
    {
        return (float) bin * (float) currentSampleRate / (float) fftSize;
    }

    /** La inversa, sin redondear. La necesita el dibujo: por debajo de unos
        cientos de hercios hay menos de un bin por pixel, y quedarse con el bin
        mas cercano es lo que convierte el grave en una escalera.
    */
    float getBinForFrequency (float hz) const noexcept
    {
        return hz * (float) fftSize / (float) juce::jmax (1.0, currentSampleRate);
    }

    /** Finds the strongest local maximum of the spectrum inside a frequency
        range.

        Ahora mismo no la llama nadie: el ecualizador enganchaba el raton al pico
        mas cercano y se quito, porque el pico sale del espectro vivo y el punto
        acababa bailando con la musica en vez de seguir al raton. Se queda
        porque es una medida del analizador, esta comprobada, y el dia que haya
        un "pegate al pico" con una tecla pulsada es exactamente esto.

        The reported frequency is refined by parabolic interpolation across the
        three bins around the maximum, so it is not quantised to the bin spacing
        (about 12 Hz at 48 kHz) — which matters, because 12 Hz is most of a
        semitone down at the bottom of a bass part.

        @returns false when nothing in the range is a peak above the threshold
    */
    bool findPeakBetween (double lowHz,
                          double highHz,
                          float thresholdDb,
                          double& frequencyOut,
                          float& magnitudeDbOut) const;

    void setDecayRate (float dbPerSecond) noexcept { decayDbPerSecond = dbPerSecond; }
    void setFloorDb   (float db) noexcept          { floorDb = db; }

private:
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize,
                                                 juce::dsp::WindowingFunction<float>::hann,
                                                 false };

    std::vector<float> ring;          // audio thread writes, GUI reads
    std::atomic<int>   writePos { 0 };

    std::vector<float> scratch;       // 2 * fftSize, GUI only
    std::vector<float> smoothedDb;    // numBins, GUI only

    double currentSampleRate { 44100.0 };
    float  decayDbPerSecond { 26.0f };

    /** Deliberately lower than the range the GUI displays. The display tilts the
        spectrum by a few dB per octave so that music reads as roughly level, and
        that tilt would otherwise lift the silence floor into view as a diagonal
        ramp across the top end. Keeping the floor further down than the largest
        tilt (about 19 dB at 20 kHz) means silence stays pinned to the bottom.
    */
    float  floorDb { -120.0f };
    float  windowGainCorrection { 1.0f };
};
} // namespace nodo::dsp
