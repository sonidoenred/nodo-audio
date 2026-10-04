#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace nodo::dsp
{
/** Retardo entero de unas pocas muestras, sin asignar memoria despues de
    prepare().

    Existe por la latencia del sobremuestreo. Cuando el sobremuestreo esta
    encendido la salida del plugin sale L muestras despues que la entrada, y el
    anfitrion lo compensa retrasando todo lo demas. Dentro del plugin, en
    cambio, cualquier cosa que quiera comparar la senal procesada con la seca
    —el delta, o el fundido del bypass— tiene que retrasar la seca esas mismas L
    muestras, o estara restando dos cosas que no ocurrieron a la vez.

    El retardo es entero porque el que declara el plugin tambien lo es: el
    semibanda IIR tiene una latencia fraccionaria y redondearla es lo que ya
    hace setLatencySamples. Usar aqui el mismo numero redondeado mantiene las
    dos cosas de acuerdo, que es lo que importa.
*/
class SampleDelay
{
public:
    void prepare (int numChannels, int maxBlockSize, int maxDelaySamples)
    {
        channels = juce::jmax (1, numChannels);
        capacity = juce::jmax (2, maxBlockSize + maxDelaySamples + 1);

        buffer.setSize (channels, capacity, false, true, true);
        reset();
    }

    void reset() noexcept
    {
        buffer.clear();
        writePos = 0;
    }

    /** El retardo se recorta a lo que cabe: pedir mas de lo reservado seria
        leer memoria de otro, y es mejor un retardo corto que eso.
    */
    void setDelay (int samples) noexcept
    {
        delaySamples = juce::jlimit (0, juce::jmax (0, capacity - 1), samples);
    }

    int getDelay() const noexcept { return delaySamples; }

    /** Hilo de audio. Guarda el bloque y devuelve en `out` la version retrasada.
        `in` y `out` pueden ser el mismo buffer.
    */
    void process (const juce::AudioBuffer<float>& in,
                  juce::AudioBuffer<float>& out,
                  int numSamples) noexcept
    {
        const auto numCh = juce::jmin (channels, juce::jmin (in.getNumChannels(),
                                                             out.getNumChannels()));

        if (numCh <= 0 || numSamples <= 0 || numSamples > capacity)
            return;

        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* ring = buffer.getWritePointer (ch);
            const auto* source = in.getReadPointer (ch);
            auto* destination = out.getWritePointer (ch);

            auto write = writePos;

            for (int i = 0; i < numSamples; ++i)
            {
                ring[write] = source[i];

                // Se lee despues de escribir, asi que un retardo de cero
                // devuelve la misma muestra que acaba de entrar.
                auto read = write - delaySamples;
                if (read < 0)
                    read += capacity;

                destination[i] = ring[read];

                if (++write >= capacity)
                    write = 0;
            }
        }

        writePos = (writePos + numSamples) % capacity;
    }

private:
    juce::AudioBuffer<float> buffer;
    int channels { 2 };
    int capacity { 2 };
    int writePos { 0 };
    int delaySamples { 0 };
};
} // namespace nodo::dsp
