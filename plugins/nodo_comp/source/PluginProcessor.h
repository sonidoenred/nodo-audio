#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <nodo_core/nodo_core.h>

#include "CompParameters.h"
#include "CompEngine.h"

namespace nodo::comp
{
class NodoCompProcessor : public juce::AudioProcessor
{
public:
    NodoCompProcessor();
    ~NodoCompProcessor() override;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Nodo Comp"; }

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

    juce::AudioProcessorValueTreeState& getState() noexcept { return apvts; }
    PresetManager& getPresetManager() noexcept { return presetManager; }

    CompSettings getSettings() { return readSettings (apvts); }

    /** GUI. Current reduction in dB, at or below zero. */
    float getGainReductionDb() const noexcept;

    /** GUI. What the detector is hearing, in dB. */
    float getDetectorLevelDb() const noexcept;

    /** GUI. Index 0 and 1 are input channels, 2 and 3 output. */
    float getMeterLevel (int index) const noexcept;

    /** True when the host has actually connected something to the sidechain
        bus, so the interface can say so instead of leaving the user wondering
        why External changes nothing.
    */
    bool isSidechainConnected() const noexcept { return sidechainConnected.load (std::memory_order_relaxed); }

    void switchToSlot (bool useSlotB);
    void copyCurrentSlotToOther();
    bool isSlotB() const noexcept { return slotIsB; }

private:
    static BusesProperties busesProperties();
    void updateLatency();

    juce::AudioProcessorValueTreeState apvts;
    PresetManager presetManager;

    CompEngine engineBase, engineOversampled;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler, sidechainOversampler;
    bool oversamplingActive { false };

    juce::AudioBuffer<float> dryBuffer, sidechainBuffer;
    juce::LinearSmoothedValue<float> bypassRamp { 1.0f };

    std::array<dsp::LevelFollower, 2> inputLevels, outputLevels;
    std::atomic<bool> sidechainConnected { false };

    std::atomic<float>* bypassValue { nullptr };
    std::atomic<float>* oversamplingValue { nullptr };

    juce::ValueTree slotA, slotB;
    bool slotIsB { false };

    double hostSampleRate { 44100.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodoCompProcessor)
};
} // namespace nodo::comp
