#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <nodo_core/nodo_core.h>

#include "DelayParameters.h"
#include "DelayEngine.h"

namespace nodo::delay
{
class NodoDelayProcessor : public juce::AudioProcessor
{
public:
    NodoDelayProcessor();
    ~NodoDelayProcessor() override;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Nodo Delay"; }

    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }

    /** Five seconds of delay plus what the feedback adds. Hosts use this to
        decide how long to keep rendering after the last note, and a delay that
        under-reports it gets its tail cut off in a bounce.
    */
    double getTailLengthSeconds() const override { return 12.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState& getState() noexcept { return apvts; }
    PresetManager& getPresetManager() noexcept { return presetManager; }

    /** The settings including the tempo the host last reported, so anything
        that asks for the resolved delay time gets the same answer the engine
        is working from.
    */
    DelaySettings getSettings();

    float getCurrentTimeMs (int line) const noexcept { return engine.getCurrentTimeMs (line); }
    float getLoopLevel (int line) const noexcept { return engine.getLoopLevel (line); }

    /** 0 and 1 are the LFOs, from -1 to 1; 2 is the envelope, from 0 to 1. */
    float getModulationSource (int index) const noexcept { return engine.getModulationSource (index); }
    float getMeterLevel (int index) const noexcept;
    double getHostBpm() const noexcept { return hostBpm.load (std::memory_order_relaxed); }
    bool isTempoKnown() const noexcept { return tempoKnown.load (std::memory_order_relaxed); }

    void switchToSlot (bool useSlotB);
    void copyCurrentSlotToOther();
    bool isSlotB() const noexcept { return slotIsB; }

private:
    juce::AudioProcessorValueTreeState apvts;
    PresetManager presetManager;

    DelayEngine engine;

    juce::AudioBuffer<float> dryBuffer;
    juce::LinearSmoothedValue<float> bypassRamp { 1.0f };

    std::array<dsp::LevelFollower, 2> inputLevels, outputLevels;
    std::atomic<float>* bypassValue { nullptr };

    std::atomic<double> hostBpm { 120.0 };
    std::atomic<bool> tempoKnown { false };

    juce::ValueTree slotA, slotB;
    bool slotIsB { false };

    double hostSampleRate { 44100.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodoDelayProcessor)
};
} // namespace nodo::delay
