#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "FactoryPresets.h"

namespace nodo::delay
{
NodoDelayProcessor::NodoDelayProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "NodoDelay", createParameterLayout()),
      presetManager (apvts, "Nodo Delay")
{
    bypassValue = apvts.getRawParameterValue (ids::bypass);

    /*  The plugin claims its colour here rather than in the editor: the shared
        header and every widget read the accent while they are being
        constructed, and an editor sets its own members up before its body ever
        runs.
    */
    theme::colours::setAccent (theme::colours::suite::delay);

    presetManager.setFactoryPresets (buildFactoryPresets());
}

NodoDelayProcessor::~NodoDelayProcessor() = default;

DelaySettings NodoDelayProcessor::getSettings()
{
    auto settings = readSettings (apvts);
    settings.bpm = hostBpm.load (std::memory_order_relaxed);

    return settings;
}

void NodoDelayProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    hostSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;

    const auto numChannels = (juce::uint32) juce::jlimit (1, 2, getMainBusNumOutputChannels());
    const auto maxBlock = (juce::uint32) juce::jmax (1, maximumExpectedSamplesPerBlock);

    engine.prepare ({ hostSampleRate, maxBlock, numChannels });

    for (auto& follower : inputLevels)  follower.prepare (hostSampleRate, 0.35f);
    for (auto& follower : outputLevels) follower.prepare (hostSampleRate, 0.35f);

    bypassRamp.reset (hostSampleRate, 0.01);
    bypassRamp.setCurrentAndTargetValue (bypassValue != nullptr && bypassValue->load() > 0.5f ? 0.0f : 1.0f);

    dryBuffer.setSize ((int) numChannels, (int) maxBlock, false, false, true);

    engine.setSettings (getSettings());
}

void NodoDelayProcessor::releaseResources()
{
    engine.reset();
}

bool NodoDelayProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

float NodoDelayProcessor::getMeterLevel (int index) const noexcept
{
    const auto channel = (size_t) juce::jlimit (0, 1, index % 2);

    return index < 2 ? inputLevels[channel].getLevel()
                     : outputLevels[channel].getLevel();
}

float NodoDelayProcessor::getMeterRms (int index) const noexcept
{
    const auto channel = (size_t) juce::jlimit (0, 1, index % 2);

    return index < 2 ? inputLevels[channel].getRms()
                     : outputLevels[channel].getRms();
}

void NodoDelayProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    const auto channels = juce::jmin (2, buffer.getNumChannels());

    if (numSamples <= 0 || channels <= 0)
        return;

    /*  The host's tempo, read once per block. A delay that is synced and cannot
        see the tempo is worse than useless — it is confidently wrong — so the
        last known value is kept and the interface says whether the host has
        ever given us one.
    */
    if (auto* playHead = getPlayHead())
    {
        if (const auto position = playHead->getPosition())
        {
            if (const auto bpm = position->getBpm())
            {
                if (*bpm > 1.0)
                {
                    hostBpm.store (*bpm, std::memory_order_relaxed);
                    tempoKnown.store (true, std::memory_order_relaxed);
                }
            }
        }
    }

    for (int ch = 0; ch < channels; ++ch)
        inputLevels[(size_t) ch].push (buffer.getReadPointer (ch), numSamples);

    const auto bypassRequested = bypassValue != nullptr && bypassValue->load() > 0.5f;
    bypassRamp.setTargetValue (bypassRequested ? 0.0f : 1.0f);
    const auto needsCrossfade = bypassRamp.isSmoothing() || bypassRequested;

    if (needsCrossfade)
    {
        dryBuffer.setSize (channels, numSamples, false, false, true);

        for (int ch = 0; ch < channels; ++ch)
            dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);
    }

    engine.setSettings (getSettings());

    juce::dsp::AudioBlock<float> block (buffer);
    engine.process (block);

    if (needsCrossfade)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const auto wet = bypassRamp.getNextValue();
            const auto dry = 1.0f - wet;

            for (int ch = 0; ch < channels; ++ch)
            {
                auto* samples = buffer.getWritePointer (ch);
                samples[i] = samples[i] * wet + dryBuffer.getReadPointer (ch)[i] * dry;
            }
        }
    }

    for (int ch = 0; ch < channels; ++ch)
        outputLevels[(size_t) ch].push (buffer.getReadPointer (ch), numSamples);
}

void NodoDelayProcessor::switchToSlot (bool useSlotB)
{
    if (useSlotB == slotIsB)
        return;

    (slotIsB ? slotB : slotA) = apvts.copyState();
    slotIsB = useSlotB;

    auto& destination = slotIsB ? slotB : slotA;

    if (! destination.isValid())
        destination = apvts.copyState();

    apvts.replaceState (destination.createCopy());
}

void NodoDelayProcessor::copyCurrentSlotToOther()
{
    (slotIsB ? slotA : slotB) = apvts.copyState();
}

void NodoDelayProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    nodo::state::tagState (state, "Nodo Delay");

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void NodoDelayProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr)
        return;

    auto tree = juce::ValueTree::fromXml (*xml);

    if (! tree.isValid() || ! tree.hasType (apvts.state.getType()))
        return;

    nodo::state::migrateIfNeeded (tree);
    apvts.replaceState (tree);
}

juce::AudioProcessorEditor* NodoDelayProcessor::createEditor()
{
    return new NodoDelayEditor (*this);
}
} // namespace nodo::delay

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new nodo::delay::NodoDelayProcessor();
}
