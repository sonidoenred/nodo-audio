#include "FactoryPresets.h"
#include "VerbParameters.h"

namespace nodo::verb
{
namespace
{
    struct Spec
    {
        float decaySeconds;
        float size;
        float mix;
        float predelayMs;

        float decayLow { 1.3f };
        float decayLowFreq { 250.0f };
        float decayHigh { 0.5f };
        float decayHighFreq { 4000.0f };

        float diffusion { 0.7f };
        float motion { 0.25f };
        float width { 1.0f };

        float preLowCut { 100.0f };
        float preHighCut { 12000.0f };

        float postLowGain { 0.0f };
        float postHighGain { 0.0f };
        float duck { 0.0f };
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
            { ids::decay,         spec.decaySeconds },
            { ids::size,          spec.size },
            { ids::mix,           spec.mix },
            { ids::predelay,      spec.predelayMs },
            { ids::decayLow,      spec.decayLow },
            { ids::decayLowFreq,  spec.decayLowFreq },
            { ids::decayHigh,     spec.decayHigh },
            { ids::decayHighFreq, spec.decayHighFreq },
            { ids::diffusion,     spec.diffusion },
            { ids::motion,        spec.motion },
            { ids::width,         spec.width },
            { ids::freeze,        0.0f },
            { ids::preLowCut,     spec.preLowCut },
            { ids::preHighCut,    spec.preHighCut },
            { ids::postLowGain,   spec.postLowGain },
            { ids::postHighGain,  spec.postHighGain },
            { ids::duckAmount,    spec.duck }
        };

        return preset;
    }
}

std::vector<FactoryPreset> buildFactoryPresets()
{
    std::vector<FactoryPreset> presets;

    presets.push_back (make ("Salas", "Sala pequeña",
        "Medio segundo y poco más. No suena a reverb, suena a que la toma se hizo "
        "en algún sitio, que es para lo que sirve una sala pequeña.",
        { 0.6f, 0.45f, 0.22f, 8.0f, 1.1f, 200.0f, 0.45f, 4500.0f, 0.75f, 0.2f, 1.0f,
          140.0f, 9000.0f }));

    presets.push_back (make ("Salas", "Sala media",
        "El punto de partida honesto para casi todo: un segundo y medio, con los "
        "agudos cayendo a la mitad de rápido que el cuerpo.",
        { 1.5f, 1.0f, 0.26f, 20.0f, 1.2f, 250.0f, 0.5f, 4000.0f, 0.75f, 0.25f, 1.1f }));

    presets.push_back (make ("Salas", "Sala de conciertos",
        "Tres segundos y medio y un predelay de cuarenta milisegundos, que es lo "
        "que separa la fuente de la sala en vez de meterla dentro.",
        { 3.5f, 1.8f, 0.28f, 42.0f, 1.5f, 220.0f, 0.4f, 3500.0f, 0.85f, 0.3f, 1.25f,
          120.0f, 8000.0f }));

    presets.push_back (make ("Salas", "Catedral",
        "Ocho segundos con los graves colgando el doble que el resto. El corte de "
        "graves de entrada está alto a propósito: sin él esto es barro.",
        { 8.0f, 2.6f, 0.3f, 60.0f, 1.9f, 300.0f, 0.3f, 2800.0f, 0.9f, 0.35f, 1.3f,
          180.0f, 6000.0f }));

    presets.push_back (make ("Placas", "Placa clásica",
        "Densa, brillante y sin predelay, como una placa de verdad: difusión alta, "
        "tamaño pequeño y los agudos aguantando casi tanto como el cuerpo.",
        { 2.2f, 0.5f, 0.3f, 0.0f, 0.9f, 300.0f, 0.85f, 6000.0f, 1.0f, 0.15f, 1.15f,
          160.0f, 14000.0f }));

    presets.push_back (make ("Placas", "Placa oscura",
        "La misma placa con una manta encima. Los agudos caen a una cuarta parte "
        "de la velocidad del cuerpo y el corte de entrada hace el resto.",
        { 2.6f, 0.55f, 0.28f, 10.0f, 1.0f, 250.0f, 0.22f, 2500.0f, 1.0f, 0.2f, 1.1f,
          150.0f, 5000.0f }));

    presets.push_back (make ("Voz", "Voz con ducking",
        "Cola larga que se aparta mientras se canta y vuelve en los huecos. Es la "
        "forma de poner dos segundos y medio en una voz sin perder la letra.",
        { 2.5f, 1.2f, 0.35f, 30.0f, 1.1f, 250.0f, 0.45f, 4000.0f, 0.8f, 0.25f, 1.2f,
          180.0f, 8000.0f, 0.0f, -2.0f, 0.6f }));

    presets.push_back (make ("Voz", "Ambiente corto",
        "Trescientos milisegundos, casi todo mezcla seca. Para que una voz deje de "
        "estar pegada al micro sin que se note que hay una reverb puesta.",
        { 0.35f, 0.35f, 0.18f, 5.0f, 1.0f, 200.0f, 0.55f, 6000.0f, 0.7f, 0.15f, 1.0f,
          200.0f, 10000.0f }));

    presets.push_back (make ("Efecto", "Cola infinita",
        "Veinte segundos y movimiento alto. Con el botón de freeze encima se queda "
        "donde esté para siempre, exactamente para siempre: cada línea devuelve lo "
        "que le dieron, ni un poco más ni un poco menos.",
        { 20.0f, 3.0f, 0.4f, 0.0f, 1.4f, 250.0f, 0.7f, 5000.0f, 0.9f, 0.6f, 1.4f,
          150.0f, 9000.0f }));

    presets.push_back (make ("Efecto", "Sin graves",
        "Los graves caen ocho veces más rápido que el cuerpo. Suena a una cola que "
        "flota por encima de la mezcla en vez de apoyarse en ella, y es un ajuste "
        "que en una reverb normal habría que hacer con un ecualizador y no queda "
        "igual: aquí no se quita el grave, se le hace durar menos.",
        { 3.0f, 1.4f, 0.3f, 25.0f, 0.15f, 400.0f, 0.6f, 5000.0f, 0.85f, 0.3f, 1.2f,
          80.0f, 10000.0f }));

    presets.push_back (make ("Bus", "Cola de batería",
        "Corta, difusa y con los graves recortados a la entrada, para que la sala "
        "del bombo no se coma la mezcla.",
        { 1.1f, 0.7f, 0.2f, 12.0f, 0.6f, 300.0f, 0.5f, 5000.0f, 0.9f, 0.2f, 1.15f,
          250.0f, 8000.0f }));

    return presets;
}
} // namespace nodo::verb
