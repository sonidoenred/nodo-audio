#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <nodo_core/nodo_core.h>

#include "EssParameters.h"
#include "EssEngine.h"

namespace nodo::ess
{
class NodoEssProcessor : public juce::AudioProcessor
{
public:
    NodoEssProcessor();
    ~NodoEssProcessor() override;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Nodo Ess"; }

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

    EssSettings getSettings() { return readSettings (apvts); }

    float getGainReductionDb() const noexcept { return engine.getGainReductionDb(); }
    float getDetectorDb() const noexcept { return engine.getDetectorDb(); }
    float getEffectiveThresholdDb() const noexcept { return engine.getEffectiveThresholdDb(); }
    float getAdaptiveReferenceDb() const noexcept { return engine.getAdaptiveReferenceDb(); }
    float getMeterLevel (int index) const noexcept;

    void switchToSlot (bool useSlotB);
    void copyCurrentSlotToOther();
    bool isSlotB() const noexcept { return slotIsB; }

private:
    juce::AudioProcessorValueTreeState apvts;
    PresetManager presetManager;

    EssEngine engine;

    juce::AudioBuffer<float> dryBuffer;
    juce::LinearSmoothedValue<float> bypassRamp { 1.0f };

    std::array<dsp::LevelFollower, 2> inputLevels, outputLevels;
    std::atomic<float>* bypassValue { nullptr };

    juce::ValueTree slotA, slotB;
    bool slotIsB { false };

    double hostSampleRate { 44100.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodoEssProcessor)
};
} // namespace nodo::ess
