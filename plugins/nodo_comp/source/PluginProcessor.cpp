#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "FactoryPresets.h"

namespace nodo::comp
{
namespace
{
    constexpr int oversamplingFactorLog2 = 1;   // 2x
}

juce::AudioProcessor::BusesProperties NodoCompProcessor::busesProperties()
{
    return BusesProperties()
        .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false);
}

NodoCompProcessor::NodoCompProcessor()
    : juce::AudioProcessor (busesProperties()),
      apvts (*this, nullptr, "NodoComp", createParameterLayout()),
      presetManager (apvts, "Nodo Comp")
{
    bypassValue       = apvts.getRawParameterValue (ids::bypass);
    oversamplingValue = apvts.getRawParameterValue (ids::oversampling);

    /*  The plugin claims its colour here rather than in the editor: the
        shared header and every widget read the accent while they are being
        constructed, and an editor sets its own members up before its body
        ever runs.
    */
    theme::colours::setAccent (theme::colours::suite::comp);

    presetManager.setFactoryPresets (buildFactoryPresets());
}

NodoCompProcessor::~NodoCompProcessor() = default;

void NodoCompProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    hostSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;

    const auto numChannels = (juce::uint32) juce::jlimit (1, 2, getMainBusNumOutputChannels());
    const auto maxBlock = (juce::uint32) juce::jmax (1, maximumExpectedSamplesPerBlock);

    engineBase.prepare ({ hostSampleRate, maxBlock, numChannels });
    engineOversampled.prepare ({ hostSampleRate * 2.0, maxBlock * 2, numChannels });

    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) numChannels, oversamplingFactorLog2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);

    // The detector has to be running at the same rate as the thing it is
    // controlling, so the sidechain gets its own oversampler rather than being
    // read at half the rate of everything else.
    sidechainOversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) numChannels, oversamplingFactorLog2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);

    oversampler->initProcessing ((size_t) maxBlock);
    oversampler->reset();
    sidechainOversampler->initProcessing ((size_t) maxBlock);
    sidechainOversampler->reset();

    for (auto& follower : inputLevels)  follower.prepare (hostSampleRate, 0.35f);
    for (auto& follower : outputLevels) follower.prepare (hostSampleRate, 0.35f);

    bypassRamp.reset (hostSampleRate, 0.01);
    bypassRamp.setCurrentAndTargetValue (bypassValue != nullptr && bypassValue->load() > 0.5f ? 0.0f : 1.0f);

    dryBuffer.setSize ((int) numChannels, (int) maxBlock, false, false, true);
    sidechainBuffer.setSize ((int) numChannels, (int) maxBlock, false, false, true);

    oversamplingActive = oversamplingValue != nullptr && oversamplingValue->load() > 0.5f;
    updateLatency();
}

void NodoCompProcessor::releaseResources()
{
    engineBase.reset();
    engineOversampled.reset();

    if (oversampler != nullptr)          oversampler->reset();
    if (sidechainOversampler != nullptr) sidechainOversampler->reset();
}

bool NodoCompProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainInputChannelSet() != out)
        return false;

    // The sidechain may be absent, mono or stereo. Hosts differ on which one
    // they offer, and refusing any of them means the bus quietly never appears.
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

void NodoCompProcessor::updateLatency()
{
    auto& engine = oversamplingActive ? engineOversampled : engineBase;

    // The engine counts its lookahead in its own samples, which run at twice the
    // rate when oversampling is on.
    const auto lookahead = oversamplingActive ? engine.getLatencySamples() / 2
                                              : engine.getLatencySamples();

    const auto osLatency = oversamplingActive && oversampler != nullptr
                         ? (int) std::round (oversampler->getLatencyInSamples())
                         : 0;

    const auto latency = lookahead + osLatency;

    if (latency != getLatencySamples())
        setLatencySamples (latency);
}

float NodoCompProcessor::getGainReductionDb() const noexcept
{
    return oversamplingActive ? engineOversampled.getGainReductionDb()
                              : engineBase.getGainReductionDb();
}

float NodoCompProcessor::getDetectorLevelDb() const noexcept
{
    return oversamplingActive ? engineOversampled.getDetectorLevelDb()
                              : engineBase.getDetectorLevelDb();
}

float NodoCompProcessor::getMeterLevel (int index) const noexcept
{
    const auto channel = (size_t) juce::jlimit (0, 1, index % 2);

    return index < 2 ? inputLevels[channel].getLevel()
                     : outputLevels[channel].getLevel();
}

void NodoCompProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto main = getBusBuffer (buffer, false, 0);
    const auto numSamples = main.getNumSamples();
    const auto channels = juce::jmin (2, main.getNumChannels());

    if (numSamples <= 0 || channels <= 0)
        return;

    for (int ch = 0; ch < channels; ++ch)
        inputLevels[(size_t) ch].push (main.getReadPointer (ch), numSamples);

    // Sidechain, copied into our own buffer so the engine sees a stable block
    // whether or not the host connected the bus.
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

    const auto wantsOversampling = oversamplingValue != nullptr && oversamplingValue->load() > 0.5f;

    if (wantsOversampling != oversamplingActive)
    {
        oversamplingActive = wantsOversampling;
        engineBase.reset();
        engineOversampled.reset();

        if (oversampler != nullptr)          oversampler->reset();
        if (sidechainOversampler != nullptr) sidechainOversampler->reset();
    }

    auto& engine = oversamplingActive ? engineOversampled : engineBase;
    engine.setSettings (getSettings());
    updateLatency();

    juce::dsp::AudioBlock<float> block (main);

    if (oversamplingActive && oversampler != nullptr)
    {
        auto upsampled = oversampler->processSamplesUp (block);

        if (scEnabled && sidechainOversampler != nullptr)
        {
            juce::dsp::AudioBlock<float> scBlock (sidechainBuffer);
            auto scUpsampled = sidechainOversampler->processSamplesUp (
                scBlock.getSubBlock (0, (size_t) numSamples));
            const juce::dsp::AudioBlock<const float> scConst (scUpsampled);

            engine.process (upsampled, &scConst);
        }
        else
        {
            engine.process (upsampled, nullptr);
        }

        oversampler->processSamplesDown (block);
    }
    else if (scEnabled)
    {
        juce::dsp::AudioBlock<float> scBlock (sidechainBuffer);
        const juce::dsp::AudioBlock<const float> scConst (scBlock.getSubBlock (0, (size_t) numSamples));

        engine.process (block, &scConst);
    }
    else
    {
        engine.process (block, nullptr);
    }

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

void NodoCompProcessor::switchToSlot (bool useSlotB)
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

void NodoCompProcessor::copyCurrentSlotToOther()
{
    (slotIsB ? slotA : slotB) = apvts.copyState();
}

void NodoCompProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    nodo::state::tagState (state, "Nodo Comp");

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void NodoCompProcessor::setStateInformation (const void* data, int sizeInBytes)
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

juce::AudioProcessorEditor* NodoCompProcessor::createEditor()
{
    return new NodoCompEditor (*this);
}
} // namespace nodo::comp

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new nodo::comp::NodoCompProcessor();
}
