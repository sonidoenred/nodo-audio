#include "FactoryPresets.h"
#include "CompParameters.h"

namespace nodo::comp
{
namespace
{
    struct PresetSpec
    {
        float thresholdDb;
        float ratio;
        float kneeDb;
        float attackMs;
        float releaseMs;
        CompStyle style;
        Detection detection;
        float mix { 1.0f };
        float link { 1.0f };
        float lookaheadMs { 0.0f };
        float scHighpassHz { 20.0f };
        bool  autoGain { true };
    };

    FactoryPreset make (const char* category,
                        const char* name,
                        const char* description,
                        const PresetSpec& spec)
    {
        FactoryPreset preset;
        // utf8(): the text is Spanish and juce::String would read the bytes as
        // ASCII. See the note on the helper.
        preset.category = utf8 (category);
        preset.name = utf8 (name);
        preset.description = utf8 (description);

        preset.assignments = {
            { ids::threshold,  spec.thresholdDb },
            { ids::ratio,      spec.ratio },
            { ids::knee,       spec.kneeDb },
            { ids::attack,     spec.attackMs },
            { ids::release,    spec.releaseMs },
            { ids::style,      (float) (int) spec.style },
            { ids::detection,  (float) (int) spec.detection },
            { ids::mix,        spec.mix },
            { ids::stereoLink, spec.link },
            { ids::lookahead,  spec.lookaheadMs },
            { ids::scHighpass, spec.scHighpassHz },
            { ids::autoGain,   spec.autoGain ? 1.0f : 0.0f }
        };

        return preset;
    }
}

std::vector<FactoryPreset> buildFactoryPresets()
{
    std::vector<FactoryPreset> presets;

    // ---- Voz ---------------------------------------------------------------
    presets.push_back (make ("Voz", "Voz hablada",
        "Nivelado parejo para locución y podcast. Sube el threshold hasta ver 3-6 dB.",
        { -20.0f, 3.0f, 8.0f, 12.0f, 180.0f, CompStyle::vocal, Detection::rms }));

    presets.push_back (make ("Voz", "Voz cantada",
        "Dos manos: agarra las sílabas fuertes sin aplastar el aire.",
        { -18.0f, 3.5f, 10.0f, 8.0f, 140.0f, CompStyle::vocal, Detection::peak }));

    presets.push_back (make ("Voz", "Voz agresiva",
        "Para rap y voces empujadas. Ratio alto y ataque corto.",
        { -16.0f, 6.0f, 4.0f, 3.0f, 90.0f, CompStyle::punch, Detection::peak }));

    presets.push_back (make ("Voz", "Voz suave (opto)",
        "Lento y transparente, al estilo de un compresor óptico.",
        { -22.0f, 3.0f, 12.0f, 20.0f, 300.0f, CompStyle::opto, Detection::rms }));

    // ---- Batería -----------------------------------------------------------
    presets.push_back (make ("Batería", "Caja con pegada",
        "Deja pasar el golpe y comprime la cola. Ataque lento a propósito.",
        { -18.0f, 4.0f, 2.0f, 15.0f, 120.0f, CompStyle::punch, Detection::peak }));

    presets.push_back (make ("Batería", "Bombo controlado",
        "Filtro de sidechain a 60 Hz para que el subgrave no dispare todo.",
        { -16.0f, 4.0f, 3.0f, 10.0f, 100.0f, CompStyle::punch, Detection::peak,
          1.0f, 1.0f, 0.0f, 60.0f }));

    presets.push_back (make ("Batería", "Overheads",
        "Junta los platos sin comerse los transitorios.",
        { -22.0f, 2.5f, 8.0f, 20.0f, 250.0f, CompStyle::clean, Detection::rms }));

    presets.push_back (make ("Batería", "Room aplastada",
        "Efecto: ratio alto, ataque instantáneo, para pistas de ambiente.",
        { -30.0f, 12.0f, 1.0f, 0.5f, 200.0f, CompStyle::punch, Detection::peak }));

    presets.push_back (make ("Batería", "Paralela 40 %",
        "Comprime fuerte y mézclalo por debajo. Sube el mix hasta que respire.",
        { -30.0f, 10.0f, 4.0f, 2.0f, 150.0f, CompStyle::punch, Detection::peak, 0.4f }));

    // ---- Bajo --------------------------------------------------------------
    presets.push_back (make ("Bajo", "Bajo parejo",
        "Nivela nota a nota. RMS para que las notas graves no pesen de más.",
        { -20.0f, 4.0f, 8.0f, 15.0f, 160.0f, CompStyle::opto, Detection::rms }));

    presets.push_back (make ("Bajo", "Bajo con dedos",
        "Controla el ataque de los dedos sin quitar el cuerpo.",
        { -18.0f, 3.5f, 6.0f, 8.0f, 120.0f, CompStyle::clean, Detection::peak }));

    presets.push_back (make ("Bajo", "Sub firme",
        "Sidechain filtrado a 40 Hz: comprime por el cuerpo, no por el sub.",
        { -20.0f, 5.0f, 6.0f, 12.0f, 140.0f, CompStyle::clean, Detection::rms,
          1.0f, 1.0f, 0.0f, 40.0f }));

    // ---- Bus y máster ------------------------------------------------------
    presets.push_back (make ("Bus", "Glue de mezcla",
        "2:1, ataque medio y release largo. 1-2 dB y para.",
        { -16.0f, 2.0f, 12.0f, 30.0f, 400.0f, CompStyle::bus, Detection::rms }));

    presets.push_back (make ("Bus", "Bus de batería",
        "Pegada en el conjunto. Sube el mix si se aplasta.",
        { -18.0f, 4.0f, 6.0f, 10.0f, 180.0f, CompStyle::bus, Detection::peak, 0.75f }));

    presets.push_back (make ("Bus", "Máster transparente",
        "Lookahead corto y ratio bajo, para que no se note.",
        { -12.0f, 1.8f, 14.0f, 25.0f, 350.0f, CompStyle::clean, Detection::rms,
          1.0f, 1.0f, 3.0f }));

    presets.push_back (make ("Bus", "Bus paralelo",
        "Compresión fuerte al 30 % de mezcla, para densidad sin perder golpe.",
        { -28.0f, 8.0f, 6.0f, 5.0f, 200.0f, CompStyle::bus, Detection::peak, 0.3f }));

    // ---- Instrumentos ------------------------------------------------------
    presets.push_back (make ("Instrumentos", "Guitarra acústica",
        "Sujeta los rasgueos y deja el brillo donde estaba.",
        { -20.0f, 3.0f, 8.0f, 12.0f, 200.0f, CompStyle::clean, Detection::peak }));

    presets.push_back (make ("Instrumentos", "Guitarra eléctrica",
        "Poco trabajo: la distorsión ya comprime por su cuenta.",
        { -16.0f, 2.5f, 10.0f, 20.0f, 250.0f, CompStyle::opto, Detection::rms }));

    presets.push_back (make ("Instrumentos", "Piano",
        "Release largo para no oír la respiración entre acordes.",
        { -22.0f, 2.5f, 12.0f, 25.0f, 500.0f, CompStyle::opto, Detection::rms }));

    presets.push_back (make ("Instrumentos", "Sintes anchos",
        "Link al 50 %: cada lado respira por su cuenta y la imagen se abre.",
        { -20.0f, 3.0f, 10.0f, 15.0f, 250.0f, CompStyle::clean, Detection::rms,
          1.0f, 0.5f }));

    // ---- Efecto ------------------------------------------------------------
    presets.push_back (make ("Efecto", "Ducking (sidechain)",
        "Pon Sidechain en External y enruta la voz. La música se aparta sola.",
        { -30.0f, 8.0f, 6.0f, 5.0f, 300.0f, CompStyle::clean, Detection::rms }));

    presets.push_back (make ("Efecto", "Bombeo",
        "Release corto y ratio alto: el efecto de bombeo, a propósito.",
        { -28.0f, 10.0f, 2.0f, 1.0f, 60.0f, CompStyle::punch, Detection::peak }));

    presets.push_back (make ("Efecto", "Limitador de picos",
        "Ratio al máximo con lookahead. Un limitador de emergencia, no de máster.",
        { -6.0f, 20.0f, 0.0f, 0.1f, 60.0f, CompStyle::clean, Detection::peak,
          1.0f, 1.0f, 5.0f }));

    return presets;
}
} // namespace nodo::comp
