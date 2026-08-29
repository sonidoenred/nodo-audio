#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <nodo_core/nodo_core.h>

#include "EqEngine.h"

namespace nodo::eq
{
class NodoEqProcessor : public juce::AudioProcessor
{
public:
    NodoEqProcessor();
    ~NodoEqProcessor() override;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParameter; }

    //==============================================================================
    juce::AudioProcessorValueTreeState& getState() noexcept { return apvts; }
    PresetManager& getPresetManager() noexcept { return presetManager; }

    /** Target settings for every band, read straight from the parameters.
        Used by the editor to draw the response curve.
    */
    std::array<BandSettings, numBands> getBandSettings() const;

    /** The rate the curve should be drawn at: the oversampled rate when
        oversampling is on, so the drawn curve matches what is actually heard.
    */
    double getDisplaySampleRate() const noexcept;

    /** Where each band's gain actually is right now, which for a dynamic band
        moves with the music. The display reads this so the curve is live.
    */
    /** Si el motor ha llegado a correr alguna vez.

        Un plugin recien abierto, o abierto en una maquina sin tarjeta de
        sonido, no ha procesado ni una muestra: la ganancia viva de cada banda
        vale cero porque nadie la ha calculado todavia, no porque la banda este
        plana. La pantalla lo usa para dibujar lo ajustado en vez de un cero que
        no significa nada.
    */
    bool hasProcessedAudio() const noexcept
    {
        return audioHasRun.load (std::memory_order_relaxed);
    }

    float getCurrentGainDb (int band) const noexcept
    {
        return (oversamplingActive ? engineOversampled : engineBase).getCurrentGainDb (band);
    }

    /** Linear magnitude for the header meters: 0 and 1 are the input channels,
        2 and 3 the output.
    */
    float getMeterLevel (int index) const noexcept
    {
        const auto i = (size_t) juce::jlimit (0, 3, index);
        return i < 2 ? inputLevels[i].getLevel() : outputLevels[i - 2].getLevel();
    }

    AnalyserMode getAnalyserMode() const;
    FilterMode   getFilterMode() const;

    nodo::dsp::SpectrumAnalyser& getPreAnalyser() noexcept  { return preAnalyser; }
    nodo::dsp::SpectrumAnalyser& getPostAnalyser() noexcept { return postAnalyser; }

    /** A/B comparison. The editor's header drives this. */
    void switchToSlot (bool useSlotB);
    bool isSlotB() const noexcept { return slotIsB; }

    /** True when the host has actually connected the sidechain input. */
    bool isSidechainConnected() const noexcept { return sidechainConnected.load (std::memory_order_relaxed); }
    void copyCurrentSlotToOther();

    /** Band solo.

        Deliberately NOT a parameter: soloing is a monitoring action, not part of
        the sound of a preset. Making it automatable would mean a saved session
        could reopen with one band soloed and the user hunting for why the mix
        sounds like a telephone.
    */
    void setSoloBand (int band) noexcept { soloBand.store (band, std::memory_order_relaxed); }
    int  getSoloBand() const noexcept    { return soloBand.load (std::memory_order_relaxed); }

    /** GUI-only settings that should persist but not appear as automation. */
    float getDisplayRangeDb() const;
    void setDisplayRangeDb (float rangeDb);

    bool getPianoRollVisible() const;
    void setPianoRollVisible (bool shouldBeVisible);

private:
    static juce::AudioProcessor::BusesProperties busesProperties();
    void updateLatency();

    juce::AudioProcessorValueTreeState apvts;
    PresetManager presetManager;

    EqEngine engineBase;      // prepared at the host sample rate
    EqEngine engineOversampled;  // prepared at 2x, used when oversampling is on

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

    nodo::dsp::SpectrumAnalyser preAnalyser, postAnalyser;
    std::array<nodo::dsp::LevelFollower, 2> inputLevels, outputLevels;

    juce::SmoothedValue<float> outputGain;
    juce::SmoothedValue<float> bypassRamp;   // click-free bypass crossfade
    juce::AudioBuffer<float> dryBuffer, sidechainBuffer;
    std::unique_ptr<juce::dsp::Oversampling<float>> sidechainOversampler;
    std::atomic<bool> sidechainConnected { false };
    std::atomic<bool> audioHasRun { false };

    std::atomic<float>* bypassValue { nullptr };
    std::atomic<float>* outputValue { nullptr };
    std::atomic<float>* autoGainValue { nullptr };
    std::atomic<float>* oversamplingValue { nullptr };
    std::atomic<float>* analyserValue { nullptr };
    std::atomic<float>* filterModeValue { nullptr };
    std::atomic<float>* dynSidechainValue { nullptr };
    juce::AudioProcessorParameter* bypassParameter { nullptr };

    std::atomic<int> soloBand { -1 };

    juce::ValueTree slotA, slotB;
    bool slotIsB { false };

    double hostSampleRate { 44100.0 };
    bool oversamplingActive { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodoEqProcessor)
};
} // namespace nodo::eq
