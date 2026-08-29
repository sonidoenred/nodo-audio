#include "FactoryPresets.h"
#include "LimitParameters.h"

namespace nodo::limit
{
namespace
{
    struct Spec
    {
        float gainDb;
        float ceilingDb;
        float lookaheadMs;
        float releaseMs;
        LimitStyle style;
        bool truePeak { true };
        float transientLink { 1.0f };
        float releaseLink { 1.0f };
        DitherMode dither { DitherMode::off };
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
            { ids::inputGain,     spec.gainDb },
            { ids::ceiling,       spec.ceilingDb },
            { ids::lookahead,     spec.lookaheadMs },
            { ids::release,       spec.releaseMs },
            { ids::style,         (float) (int) spec.style },
            { ids::truePeak,      spec.truePeak ? 1.0f : 0.0f },
            { ids::transientLink, spec.transientLink },
            { ids::releaseLink,   spec.releaseLink },
            { ids::dither,        (float) (int) spec.dither }
        };

        return preset;
    }
}

std::vector<FactoryPreset> buildFactoryPresets()
{
    std::vector<FactoryPreset> presets;

    // ---- Máster ------------------------------------------------------------
    presets.push_back (make ("Máster", "Streaming -1 dBTP",
        "Techo a -1 dBTP con true peak: lo que piden las plataformas. Sube GAIN "
        "hasta que el integrado marque el objetivo de la plataforma.",
        { 0.0f, -1.0f, 5.0f, 250.0f, LimitStyle::transparent }));

    presets.push_back (make ("Máster", "Máster transparente",
        "Lookahead largo y release lento. Para 1-2 dB de reducción, no más.",
        { 0.0f, -0.3f, 12.0f, 400.0f, LimitStyle::transparent }));

    presets.push_back (make ("Máster", "Máster denso",
        "Rápido y compacto. Vigila el medidor de reducción: por encima de 4 dB "
        "empieza a oírse.",
        { 3.0f, -0.5f, 3.0f, 120.0f, LimitStyle::modern }));

    presets.push_back (make ("Máster", "Club / alta energía",
        "Reducción profunda con release corto. Para pistas de baile, no para acústico.",
        { 6.0f, -0.5f, 2.0f, 80.0f, LimitStyle::modern }));

    presets.push_back (make ("Máster", "Para CD (16 bits)",
        "Techo a -0.3 dBTP con dither de 16 bits. Solo como último plugin de la cadena.",
        { 0.0f, -0.3f, 8.0f, 300.0f, LimitStyle::transparent, true, 1.0f, 1.0f,
          DitherMode::sixteenBit }));

    // ---- Bus ---------------------------------------------------------------
    presets.push_back (make ("Bus", "Bus de batería",
        "Estilo Punchy: la ventana de suavizado es corta y el golpe pasa.",
        { 3.0f, -1.0f, 1.5f, 100.0f, LimitStyle::punchy }));

    presets.push_back (make ("Bus", "Bus de mezcla",
        "Dos etapas de release para sujetar la sección sin bombear.",
        { 2.0f, -1.0f, 6.0f, 250.0f, LimitStyle::bus }));

    presets.push_back (make ("Bus", "Estéreo suelto",
        "Link al 50 % en el release: cada lado respira por su cuenta y la imagen se abre.",
        { 2.0f, -1.0f, 5.0f, 200.0f, LimitStyle::transparent, true, 1.0f, 0.5f }));

    // ---- Seguridad ---------------------------------------------------------
    presets.push_back (make ("Seguridad", "Red de seguridad",
        "Apenas trabaja. Para poner al final de una cadena y que nada se escape.",
        { 0.0f, -0.1f, 8.0f, 400.0f, LimitStyle::safe }));

    presets.push_back (make ("Seguridad", "Protección de directo",
        "Sin true peak para no añadir latencia, techo bajo. Para monitorización en vivo.",
        { 0.0f, -3.0f, 1.0f, 150.0f, LimitStyle::safe, false }));

    presets.push_back (make ("Seguridad", "Pista individual",
        "Para domar picos sueltos de una toma antes del compresor.",
        { 0.0f, -2.0f, 3.0f, 120.0f, LimitStyle::punchy }));

    return presets;
}
} // namespace nodo::limit
