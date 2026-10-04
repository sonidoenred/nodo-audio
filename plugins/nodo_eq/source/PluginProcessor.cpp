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
    deltaValue        = apvts.getRawParameterValue (ids::delta);
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

    deltaRamp.reset (hostSampleRate, 0.01);
    deltaRamp.setCurrentAndTargetValue (deltaValue != nullptr && deltaValue->load() > 0.5f ? 1.0f : 0.0f);

    /*  Reservado para la latencia mas larga que puede declarar el plugin, que
        es la del sobremuestreo. Se pide de sobra —64 muestras— porque el
        semibanda de JUCE no promete un numero y esto se dimensiona una sola vez.
    */
    dryDelay.prepare ((int) numChannels, (int) maxBlock, 64);

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

    dryDelay.reset();
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

    /*  La seca se retrasa lo mismo que declara el plugin, y no lo que mide el
        sobremuestreador. Son casi lo mismo —el semibanda tiene latencia
        fraccionaria y esto es el redondeo— pero el que importa es el declarado:
        es el que el anfitrion usa para alinear esta pista con las demas.
    */
    dryDelay.setDelay (latency);
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

    /*  La copia seca, retrasada lo mismo que la salida del plugin. La usan dos
        cosas: el fundido del bypass y el delta.

        Se hace en todos los bloques aunque no se use en este, porque la linea
        de retardo tiene que ver todas las muestras. Si solo se alimentara
        cuando hace falta, el primer bloque despues de encender el delta leeria
        un hueco de silencio de hace veinte muestras.

        Retrasarla tambien arregla algo que estaba mal desde el principio: con
        el sobremuestreo encendido, el bypass devolvia la senal seca sin
        retrasar mientras el plugin seguia declarando latencia, asi que la pista
        en bypass sonaba adelantada respecto a las demas.
    */
    dryBuffer.setSize (totalOut, numSamples, false, false, true);

    for (int ch = 0; ch < totalOut; ++ch)
        dryBuffer.copyFrom (ch, 0, main, ch, 0, numSamples);

    dryDelay.process (dryBuffer, dryBuffer, numSamples);

    const auto bypassRequested = bypassValue != nullptr && bypassValue->load() > 0.5f;
    bypassRamp.setTargetValue (bypassRequested ? 0.0f : 1.0f);
    const auto needsCrossfade = bypassRamp.isSmoothing() || bypassRequested;

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

    /*  Delta: la senal procesada menos la seca, o sea solo lo que el
        ecualizador esta haciendo. Va despues de la ganancia de salida a
        proposito, porque lo que se quiere oir es la diferencia que sale del
        plugin, incluida la ganancia que el propio plugin aplica.

        Se cruza con una rampa en vez de conmutar: la diferencia entre la senal
        y la senal menos ella misma es enorme, y sin rampa el boton sonaria a
        chasquido cada vez.
    */
    const auto deltaRequested = deltaValue != nullptr && deltaValue->load() > 0.5f;
    deltaRamp.setTargetValue (deltaRequested ? 1.0f : 0.0f);

    if (deltaRequested || deltaRamp.isSmoothing())
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const auto mix = deltaRamp.getNextValue();

            for (int ch = 0; ch < totalOut; ++ch)
                main.getWritePointer (ch)[i] -= mix * dryBuffer.getReadPointer (ch)[i];
        }
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

void NodoEqProcessor::sortBandsByFrequency()
{
    /*  Con veinticuatro bandas y la costumbre de crearlas con doble clic donde
        haga falta, los numeros de los nodos acaban sin ningun orden: la 7 en el
        grave y la 2 en el aire. El numero es lo unico que identifica a una
        banda en el teclado, en el panel y en la automatizacion del anfitrion,
        asi que poder reordenarlos de grave a agudo de una vez vale la pena.
    */
    nodo::eq::sortBandsByFrequency (apvts);

    // El solo apuntaba a un numero de banda, y ese numero ya es otra banda.
    setSoloBand (-1);
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
