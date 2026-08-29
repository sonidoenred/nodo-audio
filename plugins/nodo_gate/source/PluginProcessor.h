#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <nodo_core/nodo_core.h>

#include "GateParameters.h"
#include "GateEngine.h"

namespace nodo::gate
{
class NodoGateProcessor : public juce::AudioProcessor
{
public:
    NodoGateProcessor();
    ~NodoGateProcessor() override;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Nodo Gate"; }

    /** True because of the MIDI trigger. A host that is told a plugin takes no
        MIDI will never offer to route any to it, so this has to be declared
        whether or not the trigger is currently switched on.
    */
    bool acceptsMidi() const override  { return true; }
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

    GateSettings getSettings() { return readSettings (apvts); }

    float getGainReductionDb() const noexcept { return engine.getGainReductionDb(); }
    float getDetectorLevelDb() const noexcept { return engine.getDetectorLevelDb(); }
    bool  isGateOpen() const noexcept { return engine.isOpen(); }
    float getMeterLevel (int index) const noexcept;

    /** True when the host has actually connected something to the sidechain bus,
        so the interface can say so instead of leaving the user guessing why the
        external setting is doing nothing.
    */
    bool isSidechainConnected() const noexcept { return sidechainConnected.load (std::memory_order_relaxed); }

    void switchToSlot (bool useSlotB);
    void copyCurrentSlotToOther();
    bool isSlotB() const noexcept { return slotIsB; }

private:
    static BusesProperties busesProperties();

    juce::AudioProcessorValueTreeState apvts;
    PresetManager presetManager;

    GateEngine engine;

    juce::AudioBuffer<float> dryBuffer, sidechainBuffer;
    juce::LinearSmoothedValue<float> bypassRamp { 1.0f };

    std::array<dsp::LevelFollower, 2> inputLevels, outputLevels;
    std::atomic<float>* bypassValue { nullptr };
    std::atomic<bool> sidechainConnected { false };

    /** How many MIDI notes are currently held. A count rather than a flag, so
        overlapping notes do not close the gate when the first one lifts.
    */
    int heldNotes { 0 };

    juce::ValueTree slotA, slotB;
    bool slotIsB { false };

    double hostSampleRate { 44100.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodoGateProcessor)
};
} // namespace nodo::gate
