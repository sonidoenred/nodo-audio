#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <nodo_core/nodo_core.h>

#include "VerbParameters.h"
#include "VerbEngine.h"

namespace nodo::verb
{
class NodoVerbProcessor : public juce::AudioProcessor
{
public:
    NodoVerbProcessor();
    ~NodoVerbProcessor() override;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Nodo Verb"; }

    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }

    /** The longest decay the plugin offers, plus the longest predelay and a
        margin. Hosts use this to decide how long to keep rendering after the
        last note, and a reverb that under-reports it gets its tail cut off in a
        bounce — which is the one place a reverb's tail is the whole point.
    */
    double getTailLengthSeconds() const override { return 25.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState& getState() noexcept { return apvts; }
    PresetManager& getPresetManager() noexcept { return presetManager; }

    VerbSettings getSettings() { return readSettings (apvts); }

    float getDuckingDb() const noexcept { return engine.getDuckingDb(); }
    float getTailLevel() const noexcept { return engine.getTailLevel(); }
    float getMeterLevel (int index) const noexcept;

    /** Lo mismo pero el RMS, para las cifras de la cabecera. */
    float getMeterRms (int index) const noexcept;
    double getEngineSampleRate() const noexcept { return hostSampleRate; }

    void switchToSlot (bool useSlotB);
    void copyCurrentSlotToOther();
    bool isSlotB() const noexcept { return slotIsB; }

private:
    juce::AudioProcessorValueTreeState apvts;
    PresetManager presetManager;

    VerbEngine engine;

    juce::AudioBuffer<float> dryBuffer;
    juce::LinearSmoothedValue<float> bypassRamp { 1.0f };

    std::array<dsp::LevelFollower, 2> inputLevels, outputLevels;
    std::atomic<float>* bypassValue { nullptr };

    juce::ValueTree slotA, slotB;
    bool slotIsB { false };

    double hostSampleRate { 44100.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodoVerbProcessor)
};
} // namespace nodo::verb
