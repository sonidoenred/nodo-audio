#include "ProblemReport.h"

namespace nodo
{
juce::String buildProblemReport (const juce::AudioProcessor& processor,
                                 const juce::String& version)
{
    juce::StringArray lines;

    const juce::String wrapper (juce::AudioProcessor::getWrapperTypeDescription (processor.wrapperType));

    lines.add (processor.getName() + " " + version
               + (wrapper.isNotEmpty() ? " (" + wrapper + ")" : juce::String()));

    /*  En la aplicacion suelta no hay anfitrion que nombrar, y JUCE responde
        "Unknown", que en un informe parece que algo ha fallado. Lo que importa
        saber es que no habia DAW, y eso ya lo dice la linea de arriba.
    */
    if (processor.wrapperType != juce::AudioProcessor::wrapperType_Standalone)
    {
        const juce::PluginHostType host;
        const juce::String hostName (host.getHostDescription());
        lines.add ("Host: " + (hostName.isNotEmpty() ? hostName : juce::String ("unknown")));
    }

    auto system = juce::SystemStats::getOperatingSystemName();
    const auto cpu = juce::SystemStats::getCpuModel().trim();

    if (cpu.isNotEmpty())
        system += ", " + cpu;

    system += ", " + juce::String (juce::SystemStats::getNumCpus()) + " cores";
    system += ", " + juce::String (juce::SystemStats::getMemorySizeInMegabytes()) + " MB";

    lines.add ("System: " + system);

    /*  La frecuencia de muestreo sale a cero si el anfitrion todavia no ha
        preparado el plugin. Decirlo asi es mas util que escribir "0 Hz", que
        parece un fallo del informe y no el estado real.
    */
    const auto rate = processor.getSampleRate();

    juce::String audio = rate > 0.0 ? juce::String (juce::roundToInt (rate)) + " Hz"
                                    : juce::String ("not started");

    audio += ", " + juce::String (processor.getBlockSize()) + " sample blocks";
    audio += ", " + juce::String (processor.getTotalNumInputChannels()) + " in / "
                  + juce::String (processor.getTotalNumOutputChannels()) + " out";
    audio += ", " + juce::String (processor.getLatencySamples()) + " samples latency";

    lines.add ("Audio: " + audio);
    lines.add ("Date: " + juce::Time::getCurrentTime().toString (true, true, false, true));

    lines.add ("");
    lines.add ("What happened:");
    lines.add ("What I expected:");
    lines.add ("How to reproduce it:");

    return lines.joinIntoString ("\n");
}
} // namespace nodo
