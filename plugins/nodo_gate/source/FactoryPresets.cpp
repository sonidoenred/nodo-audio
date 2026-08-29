#include "FactoryPresets.h"
#include "GateParameters.h"

namespace nodo::gate
{
namespace
{
    struct Spec
    {
        float thresholdDb;
        float ratio;
        float rangeDb;
        float attackMs;
        float holdMs;
        float releaseMs;
        GateStyle style;

        float kneeDb { 3.0f };
        float hysteresisDb { 3.0f };
        float lookaheadMs { 0.0f };
        float scHighpassHz { 20.0f };
        float scLowpassHz { 20000.0f };
        Direction direction { Direction::gate };
        ChannelMode channelMode { ChannelMode::linked };
        SidechainSource sidechain { SidechainSource::internal };
        float mix { 1.0f };
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
            { ids::threshold,   spec.thresholdDb },
            { ids::ratio,       spec.ratio },
            { ids::range,       spec.rangeDb },
            { ids::attack,      spec.attackMs },
            { ids::hold,        spec.holdMs },
            { ids::release,     spec.releaseMs },
            { ids::knee,        spec.kneeDb },
            { ids::hysteresis,  spec.hysteresisDb },
            { ids::lookahead,   spec.lookaheadMs },
            { ids::mix,         spec.mix },
            { ids::style,       (float) (int) spec.style },
            { ids::direction,   (float) (int) spec.direction },
            { ids::channelMode, (float) (int) spec.channelMode },
            { ids::trigger,     0.0f },
            { ids::scSource,    (float) (int) spec.sidechain },
            { ids::scHighpass,  spec.scHighpassHz },
            { ids::scLowpass,   spec.scLowpassHz },
            { ids::scListen,    0.0f }
        };

        return preset;
    }
}

std::vector<FactoryPreset> buildFactoryPresets()
{
    std::vector<FactoryPreset> presets;

    presets.push_back (make ("Batería", "Caja",
        "Punto de partida para caja con derrame de charles. Lookahead corto para "
        "no comerse el ataque, y el filtro de sidechain quita los agudos del "
        "charles de la decisión.",
        { -32.0f, 200.0f, 45.0f, 0.1f, 30.0f, 120.0f, GateStyle::percussive,
          1.0f, 4.0f, 1.0f, 120.0f, 6000.0f }));

    presets.push_back (make ("Batería", "Bombo",
        "Sidechain filtrado a la zona del golpe para que la caja no lo abra.",
        { -30.0f, 200.0f, 50.0f, 0.1f, 40.0f, 150.0f, GateStyle::percussive,
          1.0f, 5.0f, 1.0f, 30.0f, 250.0f }));

    presets.push_back (make ("Batería", "Toms",
        "Rango moderado y release largo: un tom con la puerta cerrada del todo "
        "suena a cinta parada.",
        { -38.0f, 200.0f, 24.0f, 0.5f, 60.0f, 400.0f, GateStyle::percussive,
          2.0f, 5.0f, 1.0f, 60.0f, 3000.0f }));

    presets.push_back (make ("Batería", "Ambiente controlado",
        "Para pistas de sala. Recorta la cola entre golpes sin dejar de sonar a "
        "sala; el rango corto es lo que lo hace creíble.",
        { -34.0f, 4.0f, 12.0f, 2.0f, 80.0f, 300.0f, GateStyle::smooth,
          6.0f, 4.0f, 0.0f }));

    presets.push_back (make ("Voz", "Locución",
        "Quita el aire de la sala entre frases sin cortar respiraciones. "
        "Histéresis amplia para que no castañetee en las colas.",
        { -45.0f, 3.0f, 14.0f, 5.0f, 120.0f, 400.0f, GateStyle::vocal,
          8.0f, 6.0f, 0.0f, 90.0f }));

    presets.push_back (make ("Voz", "Voz cantada",
        "Expansor suave, no puerta: baja el ruido de fondo unos dB y deja pasar "
        "todo lo demás tal cual.",
        { -50.0f, 2.0f, 8.0f, 10.0f, 150.0f, 600.0f, GateStyle::smooth,
          10.0f, 6.0f, 0.0f }));

    presets.push_back (make ("Instrumentos", "Ampli de guitarra",
        "El ruido de un ampli con ganancia vive muy por debajo de las notas, así "
        "que la puerta puede ser decidida sin tocar el sustain.",
        { -48.0f, 200.0f, 40.0f, 1.0f, 60.0f, 250.0f, GateStyle::classic,
          3.0f, 6.0f, 0.0f, 80.0f }));

    presets.push_back (make ("Instrumentos", "Bajo DI",
        "Umbral bajo y release largo para no cortar el decay de la cuerda.",
        { -55.0f, 6.0f, 18.0f, 3.0f, 100.0f, 500.0f, GateStyle::smooth,
          6.0f, 5.0f, 0.0f }));

    presets.push_back (make ("Bus", "Bus de batería",
        "El más suave de todos sobre un bus entero: cualquier movimiento duro se "
        "oye. Dos etapas de release.",
        { -40.0f, 2.5f, 8.0f, 5.0f, 120.0f, 500.0f, GateStyle::bus,
          10.0f, 6.0f, 0.0f }));

    presets.push_back (make ("Ducking", "Locución sobre música",
        "Ducking con sidechain externo: manda la voz a la entrada de sidechain y "
        "la música se aparta 8 dB mientras habla. Release largo para que vuelva "
        "sin que se note.",
        { -30.0f, 4.0f, 8.0f, 15.0f, 200.0f, 800.0f, GateStyle::smooth,
          8.0f, 3.0f, 0.0f, 100.0f, 6000.0f, Direction::ducking,
          ChannelMode::linked, SidechainSource::external }));

    presets.push_back (make ("Ducking", "Bombo contra bajo",
        "El clásico de club: el bombo al sidechain, el bajo se aparta en cada "
        "golpe. Release corto para que respire con el tempo.",
        { -24.0f, 8.0f, 6.0f, 1.0f, 20.0f, 120.0f, GateStyle::clean,
          4.0f, 2.0f, 0.0f, 20.0f, 300.0f, Direction::ducking,
          ChannelMode::linked, SidechainSource::external }));

    return presets;
}
} // namespace nodo::gate
