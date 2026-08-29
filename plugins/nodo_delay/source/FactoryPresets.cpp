#include "FactoryPresets.h"
#include "DelayParameters.h"

namespace nodo::delay
{
namespace
{
    struct Spec
    {
        float timeLeftMs;
        float timeRightMs;
        float feedback;
        float mix;

        bool sync { false };
        NoteValue noteLeft  { NoteValue::eighthDotted };
        NoteValue noteRight { NoteValue::quarter };
        bool linkTimes { false };

        float cross { 0.0f };
        Routing routing { Routing::stereo };
        TimeMode timeMode { TimeMode::tape };

        float highpassHz { 20.0f };
        float lowpassHz { 20000.0f };
        float drive { 0.0f };
        float loFi { 0.0f };
        float modRateHz { 0.5f };
        float modDepth { 0.0f };

        float panLeft { -1.0f };
        float panRight { 1.0f };
        float width { 1.0f };
        bool invert { false };
    };

    /*  Anything from phase two goes in as a list of extra assignments rather
        than as more fields on the Spec. Four presets use the taps or the matrix
        and eleven do not, and giving all fifteen sixteen tap slots to fill in
        would bury the ones that matter.
    */
    using Extra = std::vector<std::pair<juce::String, float>>;

    FactoryPreset make (const char* category, const char* name,
                        const char* description, const Spec& spec, Extra extra = {})
    {
        FactoryPreset preset;
        // utf8(): the text is Spanish and juce::String would read the bytes as
        // ASCII. See the note on the helper.
        preset.category = utf8 (category);
        preset.name = utf8 (name);
        preset.description = utf8 (description);

        preset.assignments = {
            { ids::timeLeft,  spec.timeLeftMs },
            { ids::timeRight, spec.timeRightMs },
            { ids::sync,      spec.sync ? 1.0f : 0.0f },
            { ids::noteLeft,  (float) (int) spec.noteLeft },
            { ids::noteRight, (float) (int) spec.noteRight },
            { ids::linkTimes, spec.linkTimes ? 1.0f : 0.0f },
            { ids::feedback,  spec.feedback },
            { ids::cross,     spec.cross },
            { ids::invert,    spec.invert ? 1.0f : 0.0f },
            { ids::freeze,    0.0f },
            { ids::highpass,  spec.highpassHz },
            { ids::lowpass,   spec.lowpassHz },
            { ids::drive,     spec.drive },
            { ids::loFi,      spec.loFi },
            { ids::modRate,   spec.modRateHz },
            { ids::modDepth,  spec.modDepth },
            { ids::routing,   (float) (int) spec.routing },
            { ids::timeMode,  (float) (int) spec.timeMode },
            { ids::panLeft,   spec.panLeft },
            { ids::panRight,  spec.panRight },
            { ids::width,     spec.width },
            { ids::mix,       spec.mix }
        };

        for (auto& assignment : extra)
            preset.assignments.push_back (std::move (assignment));

        return preset;
    }
}

std::vector<FactoryPreset> buildFactoryPresets()
{
    std::vector<FactoryPreset> presets;

    presets.push_back (make ("Voz", "Slapback",
        "Una sola repetición corta, la de los discos de los cincuenta. Sin "
        "feedback: lo que hace el efecto es el tiempo, no la cola.",
        { 110.0f, 130.0f, 0.05f, 0.28f, false, NoteValue::eighth, NoteValue::quarter, false,
          0.0f, Routing::stereo, TimeMode::tape, 180.0f, 6000.0f, 0.15f, 0.0f, 0.4f, 0.12f }));

    presets.push_back (make ("Voz", "Eco de cinta",
        "El delay de cinta de toda la vida: filtrado por arriba y por abajo, "
        "saturando un poco y con wow. Cada repetición está más lejos que la "
        "anterior porque el color se acumula dentro del lazo.",
        { 340.0f, 380.0f, 0.42f, 0.30f, false, NoteValue::eighthDotted, NoteValue::quarter, false,
          0.15f, Routing::stereo, TimeMode::tape, 220.0f, 3400.0f, 0.35f, 0.10f, 0.55f, 0.35f }));

    presets.push_back (make ("Voz", "Un octavo con puntillo",
        "Sincronizado al tempo, el patrón que hace que una voz suene grande sin "
        "emborronar la letra. El filtro de graves lo aparta del cuerpo.",
        { 375.0f, 500.0f, 0.38f, 0.26f, true, NoteValue::eighthDotted, NoteValue::eighthDotted, true,
          0.35f, Routing::stereo, TimeMode::fade, 320.0f, 5000.0f, 0.0f, 0.0f, 0.5f, 0.08f }));

    presets.push_back (make ("Estéreo", "Ping-pong 1/8",
        "Rebota de lado a lado a corcheas. Ping-pong es un enrutado, no una "
        "cantidad: la señal entra en una línea sola y por eso la primera "
        "repetición cae a un lado.",
        { 250.0f, 250.0f, 0.45f, 0.32f, true, NoteValue::eighth, NoteValue::eighth, true,
          1.0f, Routing::pingPong, TimeMode::fade, 200.0f, 7000.0f, 0.0f, 0.0f, 0.5f, 0.0f }));

    presets.push_back (make ("Estéreo", "Patrón cruzado",
        "Dos tiempos distintos que se pasan la señal: el resultado no es una "
        "serie que decae, es un ritmo. Mira el dibujo de repeticiones.",
        { 300.0f, 450.0f, 0.5f, 0.30f, false, NoteValue::eighth, NoteValue::quarter, false,
          0.6f, Routing::stereo, TimeMode::tape, 150.0f, 6000.0f, 0.12f, 0.0f, 0.35f, 0.2f }));

    presets.push_back (make ("Estéreo", "Ancho y lejano",
        "Repeticiones oscuras y muy abiertas, para que quede sitio delante. "
        "Width por encima del 100 % y las dos líneas separadas.",
        { 420.0f, 610.0f, 0.55f, 0.24f, false, NoteValue::quarter, NoteValue::half, false,
          0.4f, Routing::stereo, TimeMode::tape, 300.0f, 2600.0f, 0.2f, 0.05f, 0.28f, 0.4f,
          -0.85f, 0.85f, 1.4f }));

    presets.push_back (make ("Efecto", "Cinta gastada",
        "Mucho wow, poco ancho de banda y bits de menos. Suena a una máquina "
        "que ya no debería estar funcionando, que es la idea.",
        { 480.0f, 520.0f, 0.6f, 0.35f, false, NoteValue::quarter, NoteValue::quarter, false,
          0.25f, Routing::stereo, TimeMode::tape, 400.0f, 1900.0f, 0.55f, 0.45f, 0.9f, 0.75f }));

    presets.push_back (make ("Efecto", "Casi realimentado",
        "Feedback al borde: se sostiene mucho tiempo sin llegar a irse. Un "
        "recortador suave dentro del lazo se encarga de que si te pasas acabe "
        "en un tono y no en un desastre.",
        { 600.0f, 900.0f, 0.92f, 0.30f, false, NoteValue::quarter, NoteValue::half, false,
          0.5f, Routing::stereo, TimeMode::tape, 250.0f, 3000.0f, 0.25f, 0.1f, 0.2f, 0.3f }));

    presets.push_back (make ("Efecto", "Doblaje corto",
        "Treinta milisegundos con wow: la repetición no se oye como repetición, "
        "se oye como una segunda toma. Sube el mix hasta que engorde.",
        { 28.0f, 36.0f, 0.0f, 0.40f, false, NoteValue::sixteenth, NoteValue::sixteenth, false,
          0.0f, Routing::stereo, TimeMode::tape, 120.0f, 9000.0f, 0.1f, 0.0f, 1.2f, 0.5f,
          -0.7f, 0.7f, 1.2f }));

    presets.push_back (make ("Instrumentos", "Guitarra rítmica",
        "Corcheas sincronizadas y muy filtradas, para que el delay empuje el "
        "ritmo sin taparlo.",
        { 250.0f, 250.0f, 0.3f, 0.22f, true, NoteValue::eighth, NoteValue::eighth, true,
          0.5f, Routing::stereo, TimeMode::fade, 350.0f, 3200.0f, 0.2f, 0.0f, 0.5f, 0.15f }));

    presets.push_back (make ("Instrumentos", "Sintetizador amplio",
        "Mono a la entrada, dos tiempos distintos a la salida: la forma más "
        "barata de abrir algo que venía en el centro.",
        { 190.0f, 285.0f, 0.5f, 0.35f, false, NoteValue::eighth, NoteValue::eighthDotted, false,
          0.3f, Routing::mono, TimeMode::tape, 180.0f, 8000.0f, 0.0f, 0.0f, 0.7f, 0.25f,
          -1.0f, 1.0f, 1.5f }));

    // ---- phase two ---------------------------------------------------------

    /** Every step on, at a level and a pan, in one go. */
    auto pattern = [] (std::initializer_list<std::pair<float, float>> steps)
    {
        Extra extra;
        auto index = 1;

        for (const auto& [level, pan] : steps)
        {
            extra.emplace_back (ids::tapOn (index), level > 0.0f ? 1.0f : 0.0f);
            extra.emplace_back (ids::tapLevel (index), juce::jmax (0.0f, level));
            extra.emplace_back (ids::tapPan (index), pan);
            ++index;
        }

        return extra;
    };

    {
        auto extra = pattern ({ { 1.0f, -0.9f }, { 0.0f, 0.0f }, { 0.55f, 0.6f }, { 0.0f, 0.0f },
                                { 0.8f, 0.9f },  { 0.0f, 0.0f }, { 0.4f, -0.5f }, { 0.65f, 0.2f } });
        extra.emplace_back (ids::multiTap, 1.0f);
        extra.emplace_back (ids::division, 8.0f);
        extra.emplace_back (ids::tapSpread, 1.0f);

        presets.push_back (make ("Patrones", "Corcheas con hueco",
            "Un compás dividido en ocho, con cuatro pasos puestos y cuatro vacíos. "
            "El hueco es lo que hace el ritmo: un delay que suena en todos los "
            "pasos no es un patrón, es una repetición.",
            { 500.0f, 500.0f, 0.4f, 0.32f, true, NoteValue::half, NoteValue::half, true,
              0.0f, Routing::mono, TimeMode::fade, 180.0f, 6000.0f, 0.1f, 0.0f, 0.5f, 0.0f },
            std::move (extra)));
    }

    {
        auto extra = pattern ({ { 0.35f, -0.8f }, { 0.5f, 0.5f }, { 0.7f, -0.3f }, { 1.0f, 0.9f } });
        extra.emplace_back (ids::multiTap, 1.0f);
        extra.emplace_back (ids::division, 4.0f);

        presets.push_back (make ("Patrones", "Crescendo cruzado",
            "Los cuatro pasos suben de nivel en vez de bajar, así que cada compás "
            "se acerca en lugar de alejarse. Cruzado en el estéreo para que además "
            "se mueva.",
            { 640.0f, 480.0f, 0.45f, 0.3f, false, NoteValue::quarter, NoteValue::quarter, false,
              0.4f, Routing::stereo, TimeMode::tape, 250.0f, 4500.0f, 0.2f, 0.05f, 0.3f, 0.15f },
            std::move (extra)));
    }

    {
        // A chorus is a very short delay whose time is being moved. Nothing else
        // about it is special, which is the nice thing about having a matrix.
        Extra extra {
            { ids::lfoRate (1), 0.6f },
            { ids::lfoShape (1), (float) (int) dsp::Lfo::Shape::sine },
            { ids::lfoPhase (1), 0.0f },
            { ids::lfoRate (2), 0.6f },
            { ids::lfoShape (2), (float) (int) dsp::Lfo::Shape::sine },
            { ids::lfoPhase (2), 90.0f },
            { ids::modSource (1), (float) (int) ModSource::lfo1 },
            { ids::modTarget (1), (float) (int) ModTarget::timeLeft },
            { ids::modAmount (1), 0.35f },
            { ids::modSource (2), (float) (int) ModSource::lfo2 },
            { ids::modTarget (2), (float) (int) ModTarget::timeRight },
            { ids::modAmount (2), 0.35f }
        };

        presets.push_back (make ("Modulación", "Coro",
            "Dos LFOs a la misma velocidad y a noventa grados uno del otro, cada "
            "uno sobre una línea. Eso es lo que hace que se mueva por la imagen y "
            "no hacia dentro y hacia fuera.",
            { 14.0f, 17.0f, 0.0f, 0.45f, false, NoteValue::sixteenth, NoteValue::sixteenth, false,
              0.0f, Routing::stereo, TimeMode::tape, 120.0f, 9000.0f, 0.0f, 0.0f, 0.5f, 0.0f,
              -0.8f, 0.8f, 1.2f },
            std::move (extra)));
    }

    {
        Extra extra {
            { ids::envAttack, 5.0f },
            { ids::envRelease, 350.0f },
            { ids::modSource (1), (float) (int) ModSource::envelope },
            { ids::modTarget (1), (float) (int) ModTarget::mix },
            { ids::modAmount (1), -0.7f }
        };

        presets.push_back (make ("Modulación", "Delay que se aparta",
            "El seguidor de envolvente baja la mezcla mientras entra señal y la "
            "deja subir en los huecos. Es la forma de poner un delay largo en una "
            "voz sin emborronar la letra, y no hace falta un sidechain para ello.",
            { 500.0f, 750.0f, 0.55f, 0.4f, false, NoteValue::quarter, NoteValue::half, false,
              0.35f, Routing::stereo, TimeMode::tape, 220.0f, 4000.0f, 0.15f, 0.0f, 0.4f, 0.2f },
            std::move (extra)));
    }

    return presets;
}
} // namespace nodo::delay
