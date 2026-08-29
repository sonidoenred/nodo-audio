#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <nodo_core/nodo_core.h>

#include "LimitParameters.h"
#include "LimitEngine.h"

namespace nodo::limit
{
class NodoLimitProcessor : public juce::AudioProcessor
{
public:
    NodoLimitProcessor();
    ~NodoLimitProcessor() override;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Nodo Limit"; }

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

    LimitSettings getSettings() { return readSettings (apvts); }

    float getGainReductionDb() const noexcept { return engine.getGainReductionDb(); }
    float getOutputPeakDb() const noexcept { return engine.getPeakDb(); }
    float getMomentaryLufs() const noexcept { return loudness.getMomentaryLufs(); }
    float getShortTermLufs() const noexcept { return loudness.getShortTermLufs(); }
    float getIntegratedLufs() const noexcept { return loudness.getIntegratedLufs(); }

    /** How many samples the safety clipper has had to touch since the last
        reset. Shown on screen because "the ceiling holds" is a claim, and a
        claim with a counter next to it is a fact.
    */
    juce::int64 getSafetyClipCount() const noexcept { return engine.getSafetyClipCount(); }

    /** How far the integrated loudness is from the chosen target, in LU. Returns
        zero when there is no target or nothing to measure yet.
    */
    float getDistanceToTargetLu();

    /** Adds the distance to the target to the input gain. One step: limiting
        eats part of any increase, so getting there usually takes a second nudge,
        and pretending otherwise would be a worse lie than asking for the click.
    */
    void nudgeTowardsTarget();

    /** GUI. Index 0 and 1 are input channels, 2 and 3 output. */
    float getMeterLevel (int index) const noexcept;

    /** Clears the held peak and restarts the integrated loudness measurement.
        Deferred to the audio thread rather than done here: the meter's state is
        only ever touched from there.
    */
    void requestMeterReset() noexcept { resetRequested.store (true, std::memory_order_relaxed); }

    bool isSidechainConnected() const noexcept { return sidechainConnected.load (std::memory_order_relaxed); }

    void switchToSlot (bool useSlotB);
    void copyCurrentSlotToOther();
    bool isSlotB() const noexcept { return slotIsB; }

private:
    static BusesProperties busesProperties();

    juce::AudioProcessorValueTreeState apvts;
    PresetManager presetManager;

    LimitEngine engine;
    dsp::LoudnessMeter loudness;

    juce::AudioBuffer<float> dryBuffer, sidechainBuffer;
    juce::LinearSmoothedValue<float> bypassRamp { 1.0f };
    juce::LinearSmoothedValue<float> matchGain { 1.0f };

    std::array<dsp::LevelFollower, 2> inputLevels, outputLevels;
    std::atomic<bool> sidechainConnected { false };
    std::atomic<bool> resetRequested { false };

    std::atomic<float>* bypassValue { nullptr };

    juce::ValueTree slotA, slotB;
    bool slotIsB { false };

    double hostSampleRate { 44100.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodoLimitProcessor)
};
} // namespace nodo::limit
