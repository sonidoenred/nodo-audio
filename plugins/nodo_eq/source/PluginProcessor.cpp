#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "FactoryPresets.h"

namespace nodo::eq
{
namespace
{
    const juce::Identifier displayRangeProperty { "displayRangeDb" };
    const juce::Identifier pianoRollProperty { "pianoRollVisible" };
    constexpr int oversamplingFactorLog2 = 1;   // 2x
}

juce::AudioProcessor::BusesProperties NodoEqProcessor::busesProperties()
{
    return BusesProperties()
        .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false);
}

NodoEqProcessor::NodoEqProcessor()
    : juce::AudioProcessor (busesProperties()),
      apvts (*this, nullptr, "NodoEQ", createParameterLayout()),
      presetManager (apvts, "Nodo EQ")
{
    bypassValue       = apvts.getRawParameterValue (ids::bypass);
    outputValue       = apvts.getRawParameterValue (ids::outputGain);
    autoGainValue     = apvts.getRawParameterValue (ids::autoGain);
    oversamplingValue = apvts.getRawParameterValue (ids::oversampling);
    analyserValue     = apvts.getRawParameterValue (ids::analyserMode);
    filterModeValue   = apvts.getRawParameterValue (ids::filterMode);
    dynSidechainValue = apvts.getRawParameterValue (ids::dynSidechain);
    bypassParameter   = apvts.getParameter (ids::bypass);

    /*  The plugin claims its colour here rather than in the editor: the
        shared header and every widget read the accent while they are being
        constructed, and an editor sets its own members up before its body
        ever runs.
    */
    theme::colours::setAccent (theme::colours::suite::eq);

    presetManager.setFactoryPresets (buildFactoryPresets());

    if (! apvts.state.hasProperty (displayRangeProperty))
        apvts.state.setProperty (displayRangeProperty, 12.0f, nullptr);
}

NodoEqProcessor::~NodoEqProcessor() = default;

void NodoEqProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    hostSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;

    const auto numChannels = (juce::uint32) juce::jlimit (1, 2, getMainBusNumOutputChannels());
    const auto maxBlock = (juce::uint32) juce::jmax (1, maximumExpectedSamplesPerBlock);

    // Both engines are prepared up front so that toggling oversampling at run
    // time never has to allocate on the audio thread.
    engineBase.prepare ({ hostSampleRate, maxBlock, numChannels });
    engineOversampled.prepare ({ hostSampleRate * 2.0,
                                 maxBlock * 2,
                                 numChannels });

    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) numChannels,
        oversamplingFactorLog2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true, true);

    // The detector has to run at the rate of the thing it controls, so the
    // sidechain is upsampled alongside the audio rather than read at half rate.
    sidechainOversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) numChannels,
        oversamplingFactorLog2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true, true);

    oversampler->initProcessing ((size_t) maxBlock);
    oversampler->reset();
    sidechainOversampler->initProcessing ((size_t) maxBlock);
    sidechainOversampler->reset();

    preAnalyser.prepare (hostSampleRate);
    postAnalyser.prepare (hostSampleRate);

    for (auto& follower : inputLevels)  follower.prepare (hostSampleRate, 0.35f);
    for (auto& follower : outputLevels) follower.prepare (hostSampleRate, 0.35f);

    outputGain.reset (hostSampleRate, 0.02);
    outputGain.setCurrentAndTargetValue (1.0f);

    bypassRamp.reset (hostSampleRate, 0.01);
    bypassRamp.setCurrentAndTargetValue (bypassValue != nullptr && bypassValue->load() > 0.5f ? 0.0f : 1.0f);

    dryBuffer.setSize ((int) numChannels, (int) maxBlock, false, false, true);
    sidechainBuffer.setSize ((int) numChannels, (int) maxBlock, false, false, true);

    oversamplingActive = oversamplingValue != nullptr && oversamplingValue->load() > 0.5f;
    updateLatency();
}

void NodoEqProcessor::releaseResources()
{
    engineBase.reset();
    engineOversampled.reset();

    if (oversampler != nullptr)
        oversampler->reset();

    if (sidechainOversampler != nullptr)
        sidechainOversampler->reset();
}

bool NodoEqProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainInputChannelSet() != out)
        return false;

    // The sidechain may be absent, mono or stereo; hosts differ on which they
    // offer, and refusing one means the bus quietly never shows up.
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

void NodoEqProcessor::updateLatency()
{
    const auto latency = oversamplingActive && oversampler != nullptr
                       ? (int) std::round (oversampler->getLatencyInSamples())
                       : 0;

    if (latency != getLatencySamples())
        setLatencySamples (latency);
}

std::array<BandSettings, numBands> NodoEqProcessor::getBandSettings() const
{
    std::array<BandSettings, numBands> settings;

    for (int i = 0; i < numBands; ++i)
        settings[(size_t) i] = readBand (apvts, i);

    return settings;
}

double NodoEqProcessor::getDisplaySampleRate() const noexcept
{
    return oversamplingActive ? hostSampleRate * 2.0 : hostSampleRate;
}

AnalyserMode NodoEqProcessor::getAnalyserMode() const
{
    return analyserValue != nullptr ? (AnalyserMode) (int) analyserValue->load()
                                    : AnalyserMode::both;
}

FilterMode NodoEqProcessor::getFilterMode() const
{
    return filterModeValue != nullptr ? (FilterMode) (int) filterModeValue->load()
                                      : FilterMode::analogMatched;
}

float NodoEqProcessor::getDisplayRangeDb() const
{
    return (float) apvts.state.getProperty (displayRangeProperty, 12.0f);
}

void NodoEqProcessor::setDisplayRangeDb (float rangeDb)
{
    apvts.state.setProperty (displayRangeProperty, rangeDb, nullptr);
}

bool NodoEqProcessor::getPianoRollVisible() const
{
    return (bool) apvts.state.getProperty (pianoRollProperty, false);
}

void NodoEqProcessor::setPianoRollVisible (bool shouldBeVisible)
{
    apvts.state.setProperty (pianoRollProperty, shouldBeVisible, nullptr);
}

void NodoEqProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    audioHasRun.store (true, std::memory_order_relaxed);

    auto main = getBusBuffer (buffer, false, 0);
    const auto totalOut = juce::jmin (2, main.getNumChannels());
    const auto numSamples = main.getNumSamples();

    if (totalOut <= 0 || numSamples <= 0)
        return;

    for (int ch = 0; ch < totalOut; ++ch)
        inputLevels[(size_t) ch].push (main.getReadPointer (ch), numSamples);

    // Sidechain, copied out so the engine always sees a stable block.
    const auto scBus = getBus (true, 1);
    const auto scEnabled = scBus != nullptr && scBus->isEnabled();

    sidechainConnected.store (scEnabled, std::memory_order_relaxed);

    if (scEnabled)
    {
        auto scBuffer = getBusBuffer (buffer, true, 1);
        sidechainBuffer.setSize (totalOut, numSamples, false, false, true);

        for (int ch = 0; ch < totalOut; ++ch)
        {
            const auto sourceChannel = juce::jmin (scBuffer.getNumChannels() - 1, ch);

            if (sourceChannel >= 0)
                sidechainBuffer.copyFrom (ch, 0, scBuffer, sourceChannel, 0, numSamples);
            else
                sidechainBuffer.clear (ch, 0, numSamples);
        }
    }

    const auto mode = getAnalyserMode();

    if (mode == AnalyserMode::pre || mode == AnalyserMode::both)
        preAnalyser.pushBlock (main);

    // Keep a dry copy so bypass can crossfade instead of switching abruptly.
    const auto bypassRequested = bypassValue != nullptr && bypassValue->load() > 0.5f;
    bypassRamp.setTargetValue (bypassRequested ? 0.0f : 1.0f);
    const auto needsCrossfade = bypassRamp.isSmoothing() || bypassRequested;

    if (needsCrossfade)
    {
        dryBuffer.setSize (totalOut, numSamples, false, false, true);

        for (int ch = 0; ch < totalOut; ++ch)
            dryBuffer.copyFrom (ch, 0, main, ch, 0, numSamples);
    }

    const auto wantsOversampling = oversamplingValue != nullptr && oversamplingValue->load() > 0.5f;

    if (wantsOversampling != oversamplingActive)
    {
        oversamplingActive = wantsOversampling;
        engineBase.reset();
        engineOversampled.reset();

        if (oversampler != nullptr)
            oversampler->reset();

        if (sidechainOversampler != nullptr)
            sidechainOversampler->reset();

        updateLatency();
    }

    auto& engine = oversamplingActive ? engineOversampled : engineBase;
    engine.setTargets (getBandSettings());
    engine.setSoloBand (soloBand.load (std::memory_order_relaxed));
    engine.setFilterMode (getFilterMode());
    engine.setDynamicSidechain (dynSidechainValue != nullptr && dynSidechainValue->load() > 0.5f);

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

    // Output gain, with auto gain compensating the average boost or cut applied
    // by the curve so A/B comparisons are not decided by loudness alone.
    auto targetDb = outputValue != nullptr ? outputValue->load() : 0.0f;

    if (autoGainValue != nullptr && autoGainValue->load() > 0.5f)
    {
        const auto settings = getBandSettings();
        const auto rate = getDisplaySampleRate();

        // Average the response over a handful of octaves rather than integrating
        // properly: it is cheap, stable, and close enough to feel right.
        static constexpr std::array<double, 7> probes {
            60.0, 150.0, 400.0, 1000.0, 2500.0, 6000.0, 12000.0 };

        double sum = 0.0;

        for (auto f : probes)
            sum += EqEngine::magnitudeDbAt (settings, f, rate, getFilterMode());

        targetDb -= (float) (sum / (double) probes.size());
    }

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (targetDb, -60.0f));

    for (int i = 0; i < numSamples; ++i)
    {
        const auto gain = outputGain.getNextValue();

        for (int ch = 0; ch < totalOut; ++ch)
            main.getWritePointer (ch)[i] *= gain;
    }

    if (needsCrossfade)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const auto wet = bypassRamp.getNextValue();
            const auto dry = 1.0f - wet;

            for (int ch = 0; ch < totalOut; ++ch)
            {
                auto* out = main.getWritePointer (ch);
                out[i] = out[i] * wet + dryBuffer.getReadPointer (ch)[i] * dry;
            }
        }
    }

    for (int ch = 0; ch < totalOut; ++ch)
        outputLevels[(size_t) ch].push (main.getReadPointer (ch), numSamples);

    if (mode == AnalyserMode::post || mode == AnalyserMode::both)
        postAnalyser.pushBlock (main);
}

void NodoEqProcessor::switchToSlot (bool useSlotB)
{
    if (useSlotB == slotIsB)
        return;

    // Store what is on screen into the slot we are leaving.
    (slotIsB ? slotB : slotA) = apvts.copyState();
    slotIsB = useSlotB;

    auto& destination = slotIsB ? slotB : slotA;

    if (! destination.isValid())
        destination = apvts.copyState();   // B starts life as a copy of A

    apvts.replaceState (destination.createCopy());
}

void NodoEqProcessor::copyCurrentSlotToOther()
{
    (slotIsB ? slotA : slotB) = apvts.copyState();
}

void NodoEqProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    nodo::state::tagState (state, "Nodo EQ");

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void NodoEqProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr)
        return;

    auto tree = juce::ValueTree::fromXml (*xml);

    if (! tree.isValid() || ! tree.hasType (apvts.state.getType()))
        return;

    nodo::state::migrateIfNeeded (tree);
    apvts.replaceState (tree);

    engineBase.snapToTargets();
    engineOversampled.snapToTargets();
}

juce::AudioProcessorEditor* NodoEqProcessor::createEditor()
{
    return new NodoEqEditor (*this);
}
} // namespace nodo::eq

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new nodo::eq::NodoEqProcessor();
}
