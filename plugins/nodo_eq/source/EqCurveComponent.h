#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <nodo_ui/nodo_ui.h>

#include "PluginProcessor.h"

namespace nodo::eq
{
/** The interactive display: grid, optional piano roll, spectrum analyser,
    composite response curve and one draggable node per active band.

    This is the part of the plugin people judge it by, so the interaction rules
    are deliberately the ones everyone already knows from Pro-Q:

      - drag a node          move frequency and gain
      - mouse wheel          change Q of the node under the cursor
      - double click empty   add a band there
      - double click a node  switch that band off
      - right click a node   filter type, slope, solo, invert gain, delete

    Lo que no hace, y hacia: engancharse al pico mas cercano del analizador
    cuando pasas el raton por encima. El pico se calculaba con el espectro vivo,
    asi que el punto bailaba con la musica en vez de seguir al raton — se movia
    solo estando el raton quieto. Para caer sobre una nota exacta esta el
    teclado de arriba, que no se mueve.
*/
class EqCurveComponent : public juce::Component,
                         private juce::Timer
{
public:
    explicit EqCurveComponent (NodoEqProcessor& processorToUse);
    ~EqCurveComponent() override;

    /** Fired when the selected band changes, so the panel below can follow. */
    std::function<void (int)> onBandSelected;

    /** Lo pide el menu del fondo. Reordenar bandas toca los parametros de las
        veinticuatro, asi que lo hace el procesador y no el dibujo.
    */
    std::function<void()> onSortBandsRequested;

    void setSelectedBand (int band);
    int  getSelectedBand() const noexcept { return selectedBand; }

    /** Crea una banda en una frecuencia. Publica porque el teclado de arriba
        crea bandas en la nota que se pulsa, y quien sabe que bandas hay libres
        es esta clase.

        @returns el indice de la banda creada, o -1 si no quedaba ninguna.
    */
    int addBand (double frequency, double gainDb, FilterType type);

    void paint (juce::Graphics&) override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;

    float frequencyToX (double hz) const noexcept;
    double xToFrequency (float x) const noexcept;
    float dbToY (double db) const noexcept;
    double yToDb (float y) const noexcept;

    /** Where a raw analyser magnitude lands vertically, tilt included. */
    float analyserY (float rawDb, double frequency) const noexcept;

    int bandAt (juce::Point<float> position) const;
    juce::Point<float> nodePosition (int band) const;

    /** La banda tal y como se dibuja: la viva, pero con la ganancia ajustada en
        vez de la del momento. Para una banda quieta son la misma cosa.
    */
    const BandSettings& drawnSettings (int band) const;

    /** Donde llega la banda cuando la dinamica agota su recorrido. */
    const BandSettings& dynTargetSettings (int band) const;

    juce::Point<float> dynHandlePosition (int band) const;
    int dynHandleAt (juce::Point<float> position) const;

    void drawGrid (juce::Graphics&) const;
    void drawPianoRoll (juce::Graphics&) const;
    void drawAnalyser (juce::Graphics&, nodo::dsp::SpectrumAnalyser&, bool filled) const;
    void drawResponse (juce::Graphics&) const;

    /** Degradado horizontal con el color de cada banda en su frecuencia. */
    juce::ColourGradient bandColourGradient (float alpha) const;
    void drawNodes (juce::Graphics&) const;
    void drawEmptyHint (juce::Graphics&) const;
    void drawSplitLegend (juce::Graphics&) const;
    void drawCursorReadout (juce::Graphics&) const;


    void addBandAt (juce::Point<float> position);
    void showBandMenu (int band);
    void showBackgroundMenu();
    void beginDrag (int band);
    void endDrag();
    void toggleSolo (int band);

    void setParameter (const juce::String& id, float value);
    float getParameter (const juce::String& id) const;

    NodoEqProcessor& processor;
    juce::AudioProcessorValueTreeState& state;

    std::array<BandSettings, numBands> settings;
    std::array<BandSettings, numBands> liveSettings;   // gains replaced by what is happening now

    /** Espacio de trabajo de drawnSettings(), para no reservar nada mientras se
        pinta. Mutable porque dibujar es const y esto no es estado, es un apaño
        de una linea.
    */
    mutable BandSettings drawnBand;
    mutable BandSettings dynTargetBand;
    double displaySampleRate { 44100.0 };
    float displayRangeDb { 12.0f };
    FilterMode filterMode { FilterMode::analogMatched };
    bool pianoRollVisible { false };

    int selectedBand { -1 };
    int hoveredBand { -1 };
    int draggingBand { -1 };
    int draggingDynHandle { -1 };
    juce::Point<float> dragOffset;
    juce::Point<float> lastMousePosition;
    bool mouseIsOver { false };
    juce::uint32 lastTimerMs { 0 };


    static constexpr double minFrequency = 20.0;
    static constexpr double maxFrequency = 20000.0;
    static constexpr float  nodeRadius = 7.0f;
    static constexpr float  analyserSlopeDbPerOctave = 4.5f;
    static constexpr float  analyserMinDb = -100.0f;
    static constexpr float  analyserMaxDb = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqCurveComponent)
};
} // namespace nodo::eq
