#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "FactoryPresets.h"

namespace nodo::gate
{
juce::AudioProcessor::BusesProperties NodoGateProcessor::busesProperties()
{
    return BusesProperties()
        .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false);
}

NodoGateProcessor::NodoGateProcessor()
    : juce::AudioProcessor (busesProperties()),
      apvts (*this, nullptr, "NodoGate", createParameterLayout()),
      presetManager (apvts, "Nodo Gate")
{
    bypassValue = apvts.getRawParameterValue (ids::bypass);

    /*  The plugin claims its colour here rather than in the editor: the shared
        header and every widget read the accent while they are being
        constructed, and an editor sets its own members up before its body ever
        runs.
    */
    theme::colours::setAccent (theme::colours::suite::gate);

    presetManager.setFactoryPresets (buildFactoryPresets());
}

NodoGateProcessor::~NodoGateProcessor() = default;

void NodoGateProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
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
    sidechainBuffer.setSize ((int) numChannels, (int) maxBlock, false, false, true);

    heldNotes = 0;
    engine.setTriggerOpen (false);

    engine.setSettings (getSettings());
    setLatencySamples (engine.computeLatencySamples());
}

void NodoGateProcessor::releaseResources()
{
    engine.reset();
}

bool NodoGateProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainInputChannelSet() != out)
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto& sc = layouts.getChannelSet (true, 1);

        if (! sc.isDisabled()
            && sc != juce::AudioChannelSet::mono()
            && sc != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

float NodoGateProcessor::getMeterLevel (int index) const noexcept
{
    const auto channel = (size_t) juce::jlimit (0, 1, index % 2);

    return index < 2 ? inputLevels[channel].getLevel()
                     : outputLevels[channel].getLevel();
}

float NodoGateProcessor::getMeterRms (int index) const noexcept
{
    const auto channel = (size_t) juce::jlimit (0, 1, index % 2);

    return index < 2 ? inputLevels[channel].getRms()
                     : outputLevels[channel].getRms();
}

void NodoGateProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    auto main = getBusBuffer (buffer, false, 0);
    const auto numSamples = main.getNumSamples();
    const auto channels = juce::jmin (2, main.getNumChannels());

    if (numSamples <= 0 || channels <= 0)
        return;

    /*  MIDI triggering, resolved once per block.

        Per block rather than per sample on purpose: the whole point of the
        trigger is that the attack and release do the shaping, and those are
        measured in milliseconds. Sample-accurate note timing would move the
        opening by at most one buffer, which no one can hear, in exchange for
        splitting every block in two.

        A count rather than a flag, so a chord or two overlapping notes does not
        close the gate the moment the first one lifts.
    */
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();

        if (message.isNoteOn())
            ++heldNotes;
        else if (message.isNoteOff())
            heldNotes = juce::jmax (0, heldNotes - 1);
        else if (message.isAllNotesOff() || message.isAllSoundOff())
            heldNotes = 0;
    }

    engine.setTriggerOpen (heldNotes > 0);

    for (int ch = 0; ch < channels; ++ch)
        inputLevels[(size_t) ch].push (main.getReadPointer (ch), numSamples);

    const auto scBus = getBus (true, 1);
    const auto scEnabled = scBus != nullptr && scBus->isEnabled();

    sidechainConnected.store (scEnabled, std::memory_order_relaxed);

    if (scEnabled)
    {
        auto scBuffer = getBusBuffer (buffer, true, 1);
        sidechainBuffer.setSize (channels, numSamples, false, false, true);

        for (int ch = 0; ch < channels; ++ch)
        {
            const auto sourceChannel = juce::jmin (scBuffer.getNumChannels() - 1, ch);

            if (sourceChannel >= 0)
                sidechainBuffer.copyFrom (ch, 0, scBuffer, sourceChannel, 0, numSamples);
            else
                sidechainBuffer.clear (ch, 0, numSamples);
        }
    }

    const auto bypassRequested = bypassValue != nullptr && bypassValue->load() > 0.5f;
    bypassRamp.setTargetValue (bypassRequested ? 0.0f : 1.0f);
    const auto needsCrossfade = bypassRamp.isSmoothing() || bypassRequested;

    if (needsCrossfade)
    {
        dryBuffer.setSize (channels, numSamples, false, false, true);

        for (int ch = 0; ch < channels; ++ch)
            dryBuffer.copyFrom (ch, 0, main, ch, 0, numSamples);
    }

    engine.setSettings (getSettings());

    juce::dsp::AudioBlock<float> block (main);

    if (scEnabled)
    {
        juce::dsp::AudioBlock<float> scBlock (sidechainBuffer);
        const juce::dsp::AudioBlock<const float> scConst (scBlock.getSubBlock (0, (size_t) numSamples));

        engine.process (block, &scConst);
    }
    else
    {
        engine.process (block, nullptr);
    }

    // The lookahead changes with the parameter, so the reported latency has to
    // follow it. Hosts only act on this between blocks, which is where we are.
    const auto latency = engine.getLatencySamples();

    if (latency != getLatencySamples())
        setLatencySamples (latency);

    if (needsCrossfade)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const auto wet = bypassRamp.getNextValue();
            const auto dry = 1.0f - wet;

            for (int ch = 0; ch < channels; ++ch)
            {
                auto* samples = main.getWritePointer (ch);
                samples[i] = samples[i] * wet + dryBuffer.getReadPointer (ch)[i] * dry;
            }
        }
    }

    for (int ch = 0; ch < channels; ++ch)
        outputLevels[(size_t) ch].push (main.getReadPointer (ch), numSamples);
}

void NodoGateProcessor::switchToSlot (bool useSlotB)
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

void NodoGateProcessor::copyCurrentSlotToOther()
{
    (slotIsB ? slotA : slotB) = apvts.copyState();
}

void NodoGateProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    nodo::state::tagState (state, "Nodo Gate");

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void NodoGateProcessor::setStateInformation (const void* data, int sizeInBytes)
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

juce::AudioProcessorEditor* NodoGateProcessor::createEditor()
{
    return new NodoGateEditor (*this);
}
} // namespace nodo::gate

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new nodo::gate::NodoGateProcessor();
}
