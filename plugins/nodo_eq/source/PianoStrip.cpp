#include "PianoStrip.h"

namespace nodo::eq
{
using namespace nodo::theme;

namespace
{
    constexpr double minFrequency = 20.0;
    constexpr double maxFrequency = 20000.0;

    double noteToFrequency (int midiNote) noexcept
    {
        return 440.0 * std::pow (2.0, ((double) midiNote - 69.0) / 12.0);
    }

    bool isBlackKey (int midiNote) noexcept
    {
        switch (((midiNote % 12) + 12) % 12)
        {
            case 1: case 3: case 6: case 8: case 10: return true;
            default: return false;
        }
    }

    juce::String noteName (int midiNote)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#",
                                       "G", "G#", "A", "A#", "B" };
        return juce::String (names[((midiNote % 12) + 12) % 12]) + juce::String (midiNote / 12 - 1);
    }
}

PianoStrip::PianoStrip (NodoEqProcessor& processorToUse)
    : processor (processorToUse)
{
    setOpaque (true);
    settings = processor.getBandSettings();
    startTimerHz (12);
}

PianoStrip::~PianoStrip()
{
    stopTimer();
}

void PianoStrip::timerCallback()
{
    /*  Solo repinta cuando algo que se ve ha cambiado. El teclado no tiene nada
        animado: los puntos se mueven cuando se mueve una banda, y punto.
    */
    const auto latest = processor.getBandSettings();

    for (int i = 0; i < numBands; ++i)
    {
        if (latest[(size_t) i].enabled != settings[(size_t) i].enabled
            || ! juce::approximatelyEqual (latest[(size_t) i].frequency,
                                           settings[(size_t) i].frequency))
        {
            settings = latest;
            repaint();
            return;
        }
    }

    settings = latest;
}

void PianoStrip::setSelectedBand (int band)
{
    if (selectedBand == band)
        return;

    selectedBand = band;
    repaint();
}

//==============================================================================
float PianoStrip::frequencyToX (double hz) const noexcept
{
    const auto proportion = std::log (juce::jmax (minFrequency, hz) / minFrequency)
                          / std::log (maxFrequency / minFrequency);
    return (float) (proportion * (double) getWidth());
}

double PianoStrip::xToFrequency (float x) const noexcept
{
    const auto proportion = juce::jlimit (0.0, 1.0, (double) x / juce::jmax (1.0, (double) getWidth()));
    return minFrequency * std::pow (maxFrequency / minFrequency, proportion);
}

juce::Rectangle<float> PianoStrip::whiteKeyBounds (int note) const
{
    /*  Las blancas se tocan entre ellas y las negras van encima, como en un
        piano: siete teclas por octava y no doce. Cada blanca llega hasta la
        mitad del camino a la blanca vecina, asi que sigue conteniendo su
        frecuencia — que es para lo que sirve este teclado — y ademas parece un
        teclado. Dibujar doce teclas iguales alineadas con los semitonos era
        exacto y parecia una valla.

        La mitad del camino en un eje logaritmico es la media geometrica, que es
        justo el punto medio en pixeles.
    */
    auto previousNatural = note - 1;
    while (previousNatural > 0 && isBlackKey (previousNatural)) --previousNatural;

    auto nextNatural = note + 1;
    while (nextNatural < 127 && isBlackKey (nextNatural)) ++nextNatural;

    const auto lower = std::sqrt (noteToFrequency (previousNatural) * noteToFrequency (note));
    const auto upper = std::sqrt (noteToFrequency (note) * noteToFrequency (nextNatural));

    return { frequencyToX (lower), 0.0f,
             juce::jmax (1.0f, frequencyToX (upper) - frequencyToX (lower)),
             (float) getHeight() };
}

juce::Rectangle<float> PianoStrip::blackKeyBounds (int note) const
{
    // Centrada en su propio semitono y mas estrecha que el, para que las dos
    // blancas de al lado sigan siendo cogibles por debajo.
    const auto lower = noteToFrequency (note) * std::pow (2.0, -0.30 / 12.0);
    const auto upper = noteToFrequency (note) * std::pow (2.0,  0.30 / 12.0);

    return { frequencyToX (lower), 0.0f,
             juce::jmax (1.0f, frequencyToX (upper) - frequencyToX (lower)),
             (float) getHeight() * 0.62f };
}

int PianoStrip::noteAt (juce::Point<float> position) const
{
    // Las negras primero: estan dibujadas encima y son las que hay que poder
    // acertar, igual que en un teclado real.
    for (int note = lowestNote; note <= highestNote; ++note)
        if (isBlackKey (note) && blackKeyBounds (note).contains (position))
            return note;

    for (int note = lowestNote; note <= highestNote; ++note)
        if (! isBlackKey (note) && whiteKeyBounds (note).contains (position))
            return note;

    return -1;
}

int PianoStrip::bandOnNote (int note) const
{
    const auto lower = noteToFrequency (note) * std::pow (2.0, -0.5 / 12.0);
    const auto upper = noteToFrequency (note) * std::pow (2.0,  0.5 / 12.0);

    for (int i = 0; i < numBands; ++i)
    {
        const auto& s = settings[(size_t) i];

        if (s.enabled && (double) s.frequency >= lower && (double) s.frequency < upper)
            return i;
    }

    return -1;
}

//==============================================================================
void PianoStrip::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);

    const auto height = (float) getHeight();

    // ---- teclas blancas ---------------------------------------------------
    for (int note = lowestNote; note <= highestNote; ++note)
    {
        if (isBlackKey (note))
            continue;

        const auto bounds = whiteKeyBounds (note);

        if (bounds.getRight() < 0.0f || bounds.getX() > (float) getWidth())
            continue;

        const auto band = bandOnNote (note);

        auto colour = colours::text.withAlpha (0.82f);

        if (band >= 0)
            colour = colours::forBand (band).withAlpha (band == selectedBand ? 0.95f : 0.75f);
        else if (note == hoveredNote)
            colour = colours::accentBright().withAlpha (0.8f);

        g.setColour (colour);
        g.fillRect (bounds.reduced (0.5f, 0.0f));

        // La linea entre teclas: sin ella siete blancas seguidas son una mancha.
        g.setColour (colours::background.withAlpha (0.55f));
        g.drawVerticalLine ((int) bounds.getX(), 0.0f, (float) getHeight());

        // El Do lleva su octava escrita cuando la tecla da para tanto: es lo
        // unico que hace falta para saber donde estas.
        // Once y no trece: una tecla de Do mide una octava y media de semitono,
        // que a la anchura por defecto son doce pixeles y pico. Con trece no se
        // veia ni una sola octava escrita.
        if (note % 12 == 0 && bounds.getWidth() > 11.0f)
        {
            g.setColour (colours::background.withAlpha (0.75f));
            g.setFont (fonts::bold (7.5f));
            g.drawText (noteName (note),
                        bounds.withTrimmedTop (height * 0.55f),
                        juce::Justification::centred, false);
        }
    }

    // ---- teclas negras ----------------------------------------------------
    for (int note = lowestNote; note <= highestNote; ++note)
    {
        if (! isBlackKey (note))
            continue;

        const auto bounds = blackKeyBounds (note);

        if (bounds.getRight() < 0.0f || bounds.getX() > (float) getWidth())
            continue;

        const auto band = bandOnNote (note);

        auto colour = colours::background;

        if (band >= 0)
            colour = colours::forBand (band).darker (0.45f);
        else if (note == hoveredNote)
            colour = colours::accent();

        g.setColour (colour);
        g.fillRect (bounds);
    }

    // ---- el punto de cada banda -------------------------------------------
    /*  El punto va en la x exacta de la banda y no en el centro de la tecla:
        una banda a 392 Hz y otra a 400 estan en la misma nota pero no en el
        mismo sitio, y el teclado no tiene por que mentir sobre eso.
    */
    for (int i = 0; i < numBands; ++i)
    {
        const auto& s = settings[(size_t) i];

        if (! s.enabled)
            continue;

        const auto x = frequencyToX ((double) s.frequency);

        if (x < -4.0f || x > (float) getWidth() + 4.0f)
            continue;

        const auto radius = i == selectedBand ? 3.4f : 2.6f;
        const auto centre = juce::Point<float> (x, height - radius - 1.5f);

        g.setColour (colours::background.withAlpha (0.85f));
        g.fillEllipse (juce::Rectangle<float> (radius * 2.0f + 2.0f, radius * 2.0f + 2.0f)
                         .withCentre (centre));

        g.setColour (colours::forBand (i));
        g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));
    }

    // ---- el nombre de la nota bajo el raton --------------------------------
    if (hoveredNote >= 0)
    {
        const auto bounds = whiteKeyBounds (hoveredNote);
        const auto label = juce::Rectangle<float> (58.0f, 13.0f)
                             .withCentre ({ bounds.getCentreX(), height * 0.5f });

        g.setColour (colours::panelRaised.withAlpha (0.95f));
        g.fillRect (label);

        g.setColour (colours::gridStrong);
        g.drawRect (label, 1.0f);

        g.setColour (colours::text);
        g.setFont (fonts::bold (9.5f));
        g.drawText (noteName (hoveredNote) + "  "
                      + juce::String (juce::roundToInt (noteToFrequency (hoveredNote))) + " Hz",
                    label, juce::Justification::centred, false);
    }

    g.setColour (colours::gridStrong);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

//==============================================================================
void PianoStrip::mouseMove (const juce::MouseEvent& event)
{
    const auto note = noteAt (event.position);

    if (note != hoveredNote)
    {
        hoveredNote = note;
        repaint();
    }
}

void PianoStrip::mouseExit (const juce::MouseEvent&)
{
    if (hoveredNote < 0)
        return;

    hoveredNote = -1;
    repaint();
}

void PianoStrip::mouseDown (const juce::MouseEvent& event)
{
    const auto note = noteAt (event.position);

    if (note < 0)
        return;

    const auto band = bandOnNote (note);

    if (band >= 0 && onBandClicked != nullptr)
        onBandClicked (band);
}

void PianoStrip::mouseDoubleClick (const juce::MouseEvent& event)
{
    const auto note = noteAt (event.position);

    if (note < 0 || onNoteDoubleClicked == nullptr)
        return;

    /*  Sobre una tecla que ya tiene banda no se crea una segunda encima: eso es
        lo que hace un doble clic accidental, y deja dos bandas identicas que
        nadie ve porque estan una debajo de la otra.
    */
    if (bandOnNote (note) >= 0)
        return;

    onNoteDoubleClicked (noteToFrequency (note));
}
} // namespace nodo::eq
