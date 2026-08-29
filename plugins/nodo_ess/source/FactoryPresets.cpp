#include "FactoryPresets.h"
#include "EssParameters.h"

namespace nodo::ess
{
namespace
{
    struct Spec
    {
        float frequencyHz;
        float thresholdDb;
        float rangeDb;
        float attackMs;
        float releaseMs;
        Mode mode;
        Detection detection { Detection::adaptive };
        float kneeDb { 4.0f };
        float detectorTopHz { 20000.0f };
        Channel channel { Channel::stereo };
    };

    FactoryPreset make (const char* category, const char* name,
                        const char* description, const Spec& spec)
    {
        FactoryPreset preset;
        // utf8(): the text is Spanish and juce::String would read the bytes as
        // ASCII. See the note on the helper.
        preset.category = utf8 (category);
        preset.name = utf8 (name);
        preset.description = utf8 (description);

        preset.assignments = {
            { ids::frequency, spec.frequencyHz },
            { ids::threshold, spec.thresholdDb },
            { ids::range,     spec.rangeDb },
            { ids::attack,    spec.attackMs },
            { ids::release,   spec.releaseMs },
            { ids::mode,      (float) (int) spec.mode },
            { ids::detection, (float) (int) spec.detection },
            { ids::knee,        spec.kneeDb },
            { ids::detectorTop, spec.detectorTopHz },
            { ids::channel,     (float) (int) spec.channel }
        };

        return preset;
    }
}

std::vector<FactoryPreset> buildFactoryPresets()
{
    std::vector<FactoryPreset> presets;

    presets.push_back (make ("Voz", "Locución",
        "Punto de partida para podcast y voz hablada. Umbral adaptativo, así que "
        "sirve igual en una toma floja que en una fuerte.",
        { 6500.0f, -24.0f, 8.0f, 1.0f, 60.0f, Mode::split }));

    presets.push_back (make ("Voz", "Voz cantada",
        "Rango corto y release algo más largo, para no perseguir cada sílaba.",
        { 7000.0f, -22.0f, 6.0f, 1.5f, 90.0f, Mode::split }));

    presets.push_back (make ("Voz", "Voz brillante",
        "Para tomas con mucho aire o micro de condensador cercano. Corte más bajo.",
        { 5500.0f, -26.0f, 10.0f, 1.0f, 70.0f, Mode::split }));

    presets.push_back (make ("Voz", "Voz oscura",
        "Sibilancia alta y estrecha: el corte sube para no tocar el cuerpo.",
        { 8500.0f, -22.0f, 8.0f, 1.0f, 60.0f, Mode::split }));

    presets.push_back (make ("Voz", "Sibilancia dura",
        "Cuando las eses cortan. Rango amplio; vigila el modo Listen difference "
        "para no acabar con ceceo.",
        { 6000.0f, -28.0f, 16.0f, 0.5f, 50.0f, Mode::split }));

    presets.push_back (make ("Voz", "Voz sobre pista",
        "La voz va en el centro, así que solo se trata el mid: la reverb y los "
        "dobles de los lados se quedan como están. El detector se cierra a 12 kHz "
        "para que los platos no disparen las eses.",
        { 6500.0f, -24.0f, 8.0f, 1.0f, 60.0f, Mode::split, Detection::adaptive, 4.0f,
          12000.0f, Channel::mid }));

    presets.push_back (make ("Voz", "Transparente",
        "Modo wideband: baja todo un instante en vez de tocar solo los agudos. "
        "Conserva el timbre, a cambio de que la voz se agache un poco.",
        { 6500.0f, -22.0f, 5.0f, 2.0f, 80.0f, Mode::wideband }));

    presets.push_back (make ("Instrumentos", "Charles y platos",
        "Doma el filo de los platos en un bus de batería.",
        { 8000.0f, -20.0f, 6.0f, 2.0f, 100.0f, Mode::split }));

    presets.push_back (make ("Instrumentos", "Acústica con púa",
        "El ruido de púa vive donde la sibilancia. Rango corto.",
        { 5000.0f, -22.0f, 5.0f, 2.0f, 90.0f, Mode::split }));

    presets.push_back (make ("Bus", "Bus de voces",
        "Para coros y dobles. Umbral fijo, porque el bus ya viene nivelado.",
        { 6800.0f, -26.0f, 6.0f, 2.0f, 120.0f, Mode::split, Detection::absolute, 4.0f,
          13000.0f }));

    presets.push_back (make ("Bus", "Máster suave",
        "Un toque en el máster cuando toda la mezcla pica arriba. Rango de 3 dB y "
        "el detector cerrado a 11 kHz, para que lo que lo dispare sean las eses y "
        "no los platos.",
        { 7500.0f, -18.0f, 3.0f, 3.0f, 150.0f, Mode::wideband, Detection::absolute, 8.0f,
          11000.0f }));

    return presets;
}
} // namespace nodo::ess
