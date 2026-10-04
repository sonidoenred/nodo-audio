#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace nodo
{
/** Arma el texto que el usuario copia cuando algo no le funciona.

    Todo lo que hay aqui es lo que siempre acaba preguntandose por correo, y que
    casi nadie manda a la primera: que version, en que anfitrion, en que sistema
    y con que ajustes de audio. Sin eso, la mitad de los fallos no se pueden
    reproducir y la otra mitad se reproducen mal.

    El plugin no lo envia a ningun sitio. Lo escribe, el usuario lo lee, y si
    quiere lo pega en el formulario de la web. Un plugin de audio que abre
    conexiones por su cuenta es algo que la gente audita con un cortafuegos y
    publica en un foro, y con razon: nadie ha pedido que su ecualizador hable
    con internet.

    Por eso tampoco lleva nada que identifique a nadie: ni rutas, ni nombre de
    usuario, ni que presets tiene. Modelo de maquina, sistema, anfitrion y
    ajustes de audio, que es lo unico que sirve para reproducir un fallo.
*/
juce::String buildProblemReport (const juce::AudioProcessor& processor,
                                 const juce::String& version);
} // namespace nodo
