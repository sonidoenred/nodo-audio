#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "FactoryPresets.h"

namespace nodo::limit
{
juce::AudioProcessor::BusesProperties NodoLimitProcessor::busesProperties()
{
    return BusesProperties()
        .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false);
}

NodoLimitProcessor::NodoLimitProcessor()
    : juce::AudioProcessor (busesProperties()),
      apvts (*this, nullptr, "NodoLimit", createParameterLayout()),
      presetManager (apvts, "Nodo Limit")
{
    bypassValue = apvts.getRawParameterValue (ids::bypass);
    /*  The plugin claims its colour here rather than in the editor: the
        shared header and every widget read the accent while they are being
        constructed, and an editor sets its own members up before its body
        ever runs.
    */
    theme::colours::setAccent (theme::colours::suite::limit);

    presetManager.setFactoryPresets (buildFactoryPresets());
}

NodoLimitProcessor::~NodoLimitProcessor() = default;

void NodoLimitProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    hostSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;

    const auto numChannels = (juce::uint32) juce::jlimit (1, 2, getMainBusNumOutputChannels());
    const auto maxBlock = (juce::uint32) juce::jmax (1, maximumExpectedSamplesPerBlock);

    engine.prepare ({ hostSampleRate, maxBlock, numChannels });
    loudness.prepare (hostSampleRate);

    for (auto& follower : inputLevels)  follower.prepare (hostSampleRate, 0.35f);
    for (auto& follower : outputLevels) follower.prepare (hostSampleRate, 0.35f);

    bypassRamp.reset (hostSampleRate, 0.01);
    matchGain.reset (hostSampleRate, 0.05);
    matchGain.setCurrentAndTargetValue (1.0f);
    bypassRamp.setCurrentAndTargetValue (bypassValue != nullptr && bypassValue->load() > 0.5f ? 0.0f : 1.0f);

    dryBuffer.setSize ((int) numChannels, (int) maxBlock, false, false, true);
    sidechainBuffer.setSize ((int) numChannels, (int) maxBlock, false, false, true);

    engine.setSettings (getSettings());
    setLatencySamples (engine.computeLatencySamples());
}

void NodoLimitProcessor::releaseResources()
{
    engine.reset();
    loudness.reset();
}

bool NodoLimitProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

float NodoLimitProcessor::getMeterLevel (int index) const noexcept
{
    const auto channel = (size_t) juce::jlimit (0, 1, index % 2);

    return index < 2 ? inputLevels[channel].getLevel()
                     : outputLevels[channel].getLevel();
}

float NodoLimitProcessor::getDistanceToTargetLu()
{
    const auto settings = getSettings();

    if (settings.target == LoudnessTarget::off)
        return 0.0f;

    const auto integrated = loudness.getIntegratedLufs();

    if (integrated <= -150.0f)
        return 0.0f;

    return targetLufs (settings.target) - integrated;
}

void NodoLimitProcessor::nudgeTowardsTarget()
{
    const auto distance = getDistanceToTargetLu();

    if (std::abs (distance) < 0.05f)
        return;

    if (auto* parameter = apvts.getParameter (ids::inputGain))
    {
        const auto current = getSettings().inputGainDb;

        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (current + distance));
        parameter->endChangeGesture();
    }
}

void NodoLimitProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto main = getBusBuffer (buffer, false, 0);
    const auto numSamples = main.getNumSamples();
    const auto channels = juce::jmin (2, main.getNumChannels());

    if (numSamples <= 0 || channels <= 0)
        return;

    if (resetRequested.exchange (false, std::memory_order_relaxed))
    {
        loudness.reset();
        engine.clearPeak();
        engine.clearSafetyClipCount();
    }

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
        /*  Matched bypass. The limited signal is louder than the dry one by the
            input gain minus whatever the limiter took back, so the dry path is
            brought up by exactly that to make the comparison about the sound
            rather than about which one is louder — which is the comparison every
            limiter wins by default.

            The engine keeps running while bypassed, so the figure stays current
            instead of being frozen at whatever it was when bypass went on.
        */
        const auto settings = getSettings();
        const auto matchDb = settings.bypassMatch
                           ? settings.inputGainDb + engine.getAverageReductionDb()
                           : 0.0f;

        matchGain.setTargetValue (juce::Decibels::decibelsToGain (matchDb, -60.0f));

        for (int i = 0; i < numSamples; ++i)
        {
            const auto wet = bypassRamp.getNextValue();
            const auto dry = 1.0f - wet;
            const auto match = matchGain.getNextValue();

            for (int ch = 0; ch < channels; ++ch)
            {
                auto* samples = main.getWritePointer (ch);
                samples[i] = samples[i] * wet + dryBuffer.getReadPointer (ch)[i] * dry * match;
            }
        }
    }
    else
    {
        matchGain.setCurrentAndTargetValue (1.0f);
    }

    for (int ch = 0; ch < channels; ++ch)
        outputLevels[(size_t) ch].push (main.getReadPointer (ch), numSamples);

    // Loudness is measured on the output, which is the only place the number
    // means anything to someone about to upload a master.
    const float* pointers[2] { main.getReadPointer (0),
                               main.getReadPointer (channels > 1 ? 1 : 0) };
    loudness.push (pointers, channels, numSamples);
}

void NodoLimitProcessor::switchToSlot (bool useSlotB)
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

void NodoLimitProcessor::copyCurrentSlotToOther()
{
    (slotIsB ? slotA : slotB) = apvts.copyState();
}

void NodoLimitProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    nodo::state::tagState (state, "Nodo Limit");

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void NodoLimitProcessor::setStateInformation (const void* data, int sizeInBytes)
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

juce::AudioProcessorEditor* NodoLimitProcessor::createEditor()
{
    return new NodoLimitEditor (*this);
}
} // namespace nodo::limit

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new nodo::limit::NodoLimitProcessor();
}
