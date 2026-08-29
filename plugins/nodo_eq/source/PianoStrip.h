#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <nodo_ui/nodo_ui.h>

#include "PluginProcessor.h"

namespace nodo::eq
{
/** Un teclado de verdad encima del grafico, alineado con su eje de frecuencia.

    El sombreado de notas que ya habia dice donde cae cada semitono, pero no se
    puede tocar y hay que contar octavas para saber en cual estas. Un teclado
    dibujado responde a las dos cosas a la vez: se lee de un vistazo porque
    todo el mundo sabe leer un teclado, y se puede pulsar.

      - doble clic en una tecla   crea una banda en esa nota exacta
      - clic en una tecla         selecciona la banda que ya este en esa nota
      - cada banda deja un punto  de su color sobre la tecla en la que cae

    Va arriba y no abajo. Abajo esta el eje de frecuencias con sus cifras, y
    dos reglas pegadas compiten; arriba la parte alta del grafico casi siempre
    esta vacia. Que ademas no sea donde lo pone FabFilter viene bien, pero la
    razon es la de arriba.
*/
class PianoStrip : public juce::Component,
                   private juce::Timer
{
public:
    explicit PianoStrip (NodoEqProcessor&);
    ~PianoStrip() override;

    /** Crear una banda en una frecuencia, y seleccionar una banda existente.
        Las dos las resuelve el editor contra la curva, que es quien sabe de
        bandas.
    */
    std::function<void (double)> onNoteDoubleClicked;
    std::function<void (int)> onBandClicked;

    void setSelectedBand (int band);

    void paint (juce::Graphics&) override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    /** El rango dibujado, en numeros de nota MIDI. Do0 a Si9: por debajo el
        teclado son teclas de un pixel y por encima ya no hay nada musical que
        colocar.
    */
    static constexpr int lowestNote = 24;
    static constexpr int highestNote = 119;

private:
    void timerCallback() override;

    float frequencyToX (double hz) const noexcept;
    double xToFrequency (float x) const noexcept;

    /** La tecla bajo un punto, o -1. Las negras ganan a las blancas porque
        estan dibujadas encima, igual que en un piano.
    */
    int noteAt (juce::Point<float> position) const;

    juce::Rectangle<float> whiteKeyBounds (int note) const;
    juce::Rectangle<float> blackKeyBounds (int note) const;

    /** La banda cuya frecuencia cae dentro de una nota, o -1. */
    int bandOnNote (int note) const;

    NodoEqProcessor& processor;

    std::array<BandSettings, numBands> settings;
    int selectedBand { -1 };
    int hoveredNote { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoStrip)
};
} // namespace nodo::eq
