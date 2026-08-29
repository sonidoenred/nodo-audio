#include "EqCurveComponent.h"

namespace nodo::eq
{
using namespace nodo::theme;

namespace
{
    const std::array<double, 28> gridFrequencies {
        20, 30, 40, 50, 60, 70, 80, 90,
        100, 200, 300, 400, 500, 600, 700, 800, 900,
        1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000,
        10000, 20000
    };

    bool isLabelledFrequency (double f) noexcept
    {
        return juce::approximatelyEqual (f, 100.0)  || juce::approximatelyEqual (f, 1000.0)
            || juce::approximatelyEqual (f, 10000.0) || juce::approximatelyEqual (f, 30.0)
            || juce::approximatelyEqual (f, 300.0)  || juce::approximatelyEqual (f, 3000.0);
    }

    juce::String gridLabel (double f)
    {
        return f >= 1000.0 ? juce::String (f / 1000.0, 0) + "k"
                           : juce::String (f, 0);
    }

    /** MIDI note number to frequency, equal temperament, A4 = 440 Hz. */
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
}

EqCurveComponent::EqCurveComponent (NodoEqProcessor& processorToUse)
    : processor (processorToUse),
      state (processorToUse.getState())
{
    setOpaque (true);
    setWantsKeyboardFocus (false);
    settings = processor.getBandSettings();
    liveSettings = settings;
    lastTimerMs = juce::Time::getMillisecondCounter();
    startTimerHz (30);
}

EqCurveComponent::~EqCurveComponent()
{
    stopTimer();
}

void EqCurveComponent::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounter();
    const auto elapsed = juce::jlimit (0.001f, 0.5f, (float) (now - lastTimerMs) / 1000.0f);
    lastTimerMs = now;

    settings = processor.getBandSettings();

    // A dynamic band is not where its gain knob says it is; it is wherever the
    // detector has just put it. Everything drawn uses the live value, so the
    // curve breathes with the material instead of describing a filter that is
    // not currently in place.
    liveSettings = settings;

    // Hasta que el motor no ha procesado una muestra, la ganancia viva no
    // significa nada: se dibuja lo ajustado, que es donde la banda esta.
    if (processor.hasProcessedAudio())
        for (int i = 0; i < numBands; ++i)
            if (liveSettings[(size_t) i].dynamic)
                liveSettings[(size_t) i].gainDb = processor.getCurrentGainDb (i);

    displaySampleRate = processor.getDisplaySampleRate();
    displayRangeDb = processor.getDisplayRangeDb();
    pianoRollVisible = processor.getPianoRollVisible();
    filterMode = processor.getFilterMode();

    const auto mode = processor.getAnalyserMode();

    if (mode == AnalyserMode::pre || mode == AnalyserMode::both)
        processor.getPreAnalyser().update (elapsed);

    if (mode == AnalyserMode::post || mode == AnalyserMode::both)
        processor.getPostAnalyser().update (elapsed);

    repaint();
}

//==============================================================================
float EqCurveComponent::frequencyToX (double hz) const noexcept
{
    const auto proportion = std::log (juce::jmax (minFrequency, hz) / minFrequency)
                          / std::log (maxFrequency / minFrequency);
    return (float) (proportion * (double) getWidth());
}

double EqCurveComponent::xToFrequency (float x) const noexcept
{
    const auto proportion = juce::jlimit (0.0, 1.0, (double) x / juce::jmax (1.0, (double) getWidth()));
    return minFrequency * std::pow (maxFrequency / minFrequency, proportion);
}

float EqCurveComponent::dbToY (double db) const noexcept
{
    const auto half = (double) getHeight() * 0.5;
    return (float) (half - db / (double) displayRangeDb * half);
}

double EqCurveComponent::yToDb (float y) const noexcept
{
    const auto half = (double) getHeight() * 0.5;
    return (half - (double) y) / half * (double) displayRangeDb;
}

float EqCurveComponent::analyserY (float rawDb, double frequency) const noexcept
{
    // Tilt the display so that pink-ish material reads as roughly level,
    // referenced to 1 kHz so the midrange stays where the eye expects it.
    const auto tilt = analyserSlopeDbPerOctave
                    * (float) std::log2 (juce::jmax (20.0, frequency) / 1000.0);
    const auto shown = juce::jlimit (analyserMinDb, analyserMaxDb, rawDb + tilt);
    const auto proportion = (shown - analyserMinDb) / (analyserMaxDb - analyserMinDb);
    return (float) getHeight() * (1.0f - proportion);
}

juce::Point<float> EqCurveComponent::nodePosition (int band) const
{
    /*  El nodo va donde el usuario lo ha puesto, no donde esta la banda en este
        instante. En una banda dinamica son cosas distintas: la ganancia viva se
        mueve con la señal, y un nodo que se mueve solo no se puede agarrar ni
        dice donde esta ajustada la banda. Lo que se mueve es la curva blanca.
    */
    const auto& s = drawnSettings (band);
    const auto db = usesGain (s.type) ? (double) s.gainDb : 0.0;
    return { frequencyToX ((double) s.frequency), dbToY (db) };
}

const BandSettings& EqCurveComponent::drawnSettings (int band) const
{
    /*  La banda donde vive: la ganancia ajustada, no la del instante. Para una
        banda quieta son la misma cosa; para una dinamica, esta es la posicion
        de reposo y el nodo va aqui.
    */
    auto& drawn = drawnBand;

    drawn = liveSettings[(size_t) band];
    drawn.gainDb = settings[(size_t) band].gainDb;

    return drawn;
}

const BandSettings& EqCurveComponent::dynTargetSettings (int band) const
{
    /*  Y donde llega cuando la dinamica termina su recorrido. La distancia
        entre las dos es lo que se pinta como area: todo lo que la banda puede
        llegar a hacer.
    */
    auto& drawn = dynTargetBand;

    drawn = liveSettings[(size_t) band];
    drawn.gainDb = juce::jlimit (-24.0f, 24.0f,
                                 settings[(size_t) band].gainDb + settings[(size_t) band].dynRangeDb);

    return drawn;
}

juce::Point<float> EqCurveComponent::dynHandlePosition (int band) const
{
    const auto& s = dynTargetSettings (band);
    const auto x = frequencyToX ((double) s.frequency);
    const auto y = dbToY ((double) s.gainDb);

    /*  Con el recorrido a cero el tirador cae justo debajo del nodo y no habria
        forma de cogerlo para abrirlo. Se separa diez pixeles, que es la unica
        licencia que se toma este dibujo: no dice un valor distinto del que hay,
        dice "tira de mi". En cuanto el recorrido vale algo, el tirador esta
        exactamente donde toca.
    */
    const auto nodeY = dbToY ((double) settings[(size_t) band].gainDb);

    return { x, std::abs (y - nodeY) < 10.0f ? nodeY + 10.0f : y };
}

int EqCurveComponent::dynHandleAt (juce::Point<float> position) const
{
    for (int i = 0; i < numBands; ++i)
    {
        const auto& s = settings[(size_t) i];

        if (! s.enabled || ! s.dynamic || ! usesGain (s.type))
            continue;

        if (dynHandlePosition (i).getDistanceFrom (position) < nodeRadius * 1.6f)
            return i;
    }

    return -1;
}

int EqCurveComponent::bandAt (juce::Point<float> position) const
{
    auto best = -1;
    auto bestDistance = nodeRadius * 2.4f;

    for (int i = 0; i < numBands; ++i)
    {
        if (! settings[(size_t) i].enabled)
            continue;

        const auto distance = nodePosition (i).getDistanceFrom (position);

        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = i;
        }
    }

    return best;
}

//==============================================================================
void EqCurveComponent::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    if (pianoRollVisible)
        drawPianoRoll (g);

    drawGrid (g);

    const auto mode = processor.getAnalyserMode();

    if (mode == AnalyserMode::pre || mode == AnalyserMode::both)
        drawAnalyser (g, processor.getPreAnalyser(), true);

    if (mode == AnalyserMode::post || mode == AnalyserMode::both)
        drawAnalyser (g, processor.getPostAnalyser(), false);

    drawResponse (g);
    drawEmptyHint (g);
    drawSplitLegend (g);
    drawNodes (g);
    drawCursorReadout (g);
}

void EqCurveComponent::drawPianoRoll (juce::Graphics& g) const
{
    const auto height = (float) getHeight();

    // One semitone is a constant width on a log axis. Below about four pixels
    // the keys turn into grey mush, so the roll simply does not draw.
    const auto semitoneWidth = (float) getWidth() / 119.5f;

    if (semitoneWidth < 4.0f)
        return;

    for (int note = 16; note <= 135; ++note)
    {
        const auto lower = noteToFrequency (note) * std::pow (2.0, -0.5 / 12.0);
        const auto upper = noteToFrequency (note) * std::pow (2.0,  0.5 / 12.0);

        const auto x1 = frequencyToX (lower);
        const auto x2 = frequencyToX (upper);

        if (x2 < 0.0f || x1 > (float) getWidth())
            continue;

        if (isBlackKey (note))
        {
            g.setColour (juce::Colours::black.withAlpha (0.28f));
            g.fillRect (juce::Rectangle<float> (x1, 0.0f, x2 - x1, height));
        }
        else if (note % 12 == 0)
        {
            // Mark every C so the octaves are countable at a glance.
            // La octava ya la dice el teclado de arriba, con su nombre escrito
            // en la tecla. Aqui solo hace falta que el Do se distinga.
            g.setColour (colours::accent().withAlpha (0.10f));
            g.fillRect (juce::Rectangle<float> (x1, 0.0f, x2 - x1, height));
        }
    }
}

void EqCurveComponent::drawGrid (juce::Graphics& g) const
{
    const auto height = (float) getHeight();

    g.setFont (fonts::regular (10.0f));

    for (auto f : gridFrequencies)
    {
        const auto x = frequencyToX (f);

        if (x < 1.0f || x > (float) getWidth() - 1.0f)
            continue;

        const auto labelled = isLabelledFrequency (f);
        g.setColour (labelled ? colours::gridStrong : colours::grid);
        g.drawVerticalLine ((int) x, 0.0f, height);

        if (labelled)
        {
            g.setColour (colours::textFaint);
            g.drawText (gridLabel (f),
                        juce::Rectangle<float> (x + 3.0f, height - 14.0f, 40.0f, 12.0f),
                        juce::Justification::centredLeft, false);
        }
    }

    const auto step = displayRangeDb <= 6.0f ? 3.0f
                    : displayRangeDb <= 12.0f ? 6.0f
                                              : 10.0f;

    for (float db = -displayRangeDb; db <= displayRangeDb + 0.01f; db += step)
    {
        const auto y = dbToY (db);
        const auto isZero = std::abs (db) < 0.01f;

        g.setColour (isZero ? colours::gridStrong : colours::grid);
        g.drawHorizontalLine ((int) y, 0.0f, (float) getWidth());

        // Skip the outermost values: their labels would be clipped by the top and
        // bottom edges of the display.
        const auto isEdge = std::abs (std::abs (db) - displayRangeDb) < 0.01f;

        if (! isZero && ! isEdge)
        {
            g.setColour (colours::textFaint);
            g.drawText (format::decibels (db),
                        juce::Rectangle<float> (4.0f, y - 12.0f, 52.0f, 12.0f),
                        juce::Justification::centredLeft, false);
        }
    }
}

void EqCurveComponent::drawAnalyser (juce::Graphics& g,
                                     nodo::dsp::SpectrumAnalyser& analyser,
                                     bool filled) const
{
    const auto& magnitudes = analyser.getMagnitudesDb();

    if (magnitudes.empty() || getWidth() <= 0)
        return;

    const auto width = getWidth();
    const auto height = (float) getHeight();

    /*  Catmull-Rom sobre los valores en dB de los bins vecinos. Interpolar en
        dB y no en amplitud es lo correcto aqui: la escala vertical es
        logaritmica, asi que una recta en dB es lo que el ojo lee como recta.
    */
    auto interpolatedDb = [&magnitudes] (float position)
    {
        const auto last = (int) magnitudes.size() - 1;
        const auto index = juce::jlimit (0, last, (int) std::floor (position));
        const auto t = juce::jlimit (0.0f, 1.0f, position - (float) index);

        const auto at = [&] (int i) { return magnitudes[(size_t) juce::jlimit (0, last, i)]; };

        const auto p0 = at (index - 1), p1 = at (index), p2 = at (index + 1), p3 = at (index + 2);

        return 0.5f * ((2.0f * p1)
                       + (-p0 + p2) * t
                       + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t * t
                       + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t * t * t);
    };

    juce::Path path;
    bool started = false;

    for (int x = 0; x < width; ++x)
    {
        const auto fLow = xToFrequency ((float) x);
        const auto fHigh = xToFrequency ((float) x + 1.0f);

        const auto binLow = analyser.getBinForFrequency ((float) fLow);
        const auto binHigh = analyser.getBinForFrequency ((float) fHigh);

        /*  Dos regimenes, y el reparto lo decide cuantos bins caen en el pixel.

            Arriba hay muchos bins por pixel: quedarse con el mas alto es lo
            correcto, porque asi un pico estrecho sobrevive a que el eje
            logaritmico lo aplaste. Abajo hay menos de un bin por pixel, y ahi
            el maximo dibuja peldaños de veinte pixeles de ancho — el escalon
            que se ve en los graves. Entre bin y bin hay que interpolar.
        */
        auto peakDb = -400.0f;

        if (binHigh - binLow >= 1.0f)
        {
            // Este empieza por debajo de cualquier valor real en vez de en el
            // suelo del display: si no, la inclinacion levanta un suelo
            // recortado y lo devuelve a la vista como una rampa en los agudos.
            auto found = false;

            for (int bin = 1; bin < nodo::dsp::SpectrumAnalyser::numBins; ++bin)
            {
                const auto binFreq = analyser.getBinFrequency (bin);

                if (binFreq < fLow)
                    continue;

                if (binFreq > fHigh && found)
                    break;

                peakDb = juce::jmax (peakDb, magnitudes[(size_t) bin]);
                found = true;

                if (binFreq > fHigh)
                    break;
            }
        }
        else
        {
            peakDb = interpolatedDb (0.5f * (binLow + binHigh));
        }

        const auto y = analyserY (peakDb, fLow);

        if (! started)
        {
            path.startNewSubPath ((float) x, y);
            started = true;
        }
        else
        {
            path.lineTo ((float) x, y);
        }
    }

    if (! started)
        return;

    if (filled)
    {
        auto fill = path;
        fill.lineTo ((float) width, height);
        fill.lineTo (0.0f, height);
        fill.closeSubPath();

        g.setColour (colours::analyserFill.withAlpha (0.85f));
        g.fillPath (fill);
    }
    else
    {
        g.setColour (colours::accentBright().withAlpha (0.55f));
        g.strokePath (path, juce::PathStrokeType (1.0f));
    }
}

juce::ColourGradient EqCurveComponent::bandColourGradient (float alpha) const
{
    /*  Un degradado horizontal con una parada en la frecuencia de cada banda.
        Entre dos bandas los colores se mezclan solos, que es exactamente lo que
        hace la curva: entre dos montañas la forma tampoco es de una sola.
    */
    juce::ColourGradient gradient (colours::accent().withAlpha (alpha), 0.0f, 0.0f,
                                   colours::accent().withAlpha (alpha), (float) getWidth(), 0.0f,
                                   false);

    struct Stop { float proportion; juce::Colour colour; };
    std::array<Stop, numBands> stops {};
    auto count = 0;

    for (int i = 0; i < numBands; ++i)
    {
        const auto& s = liveSettings[(size_t) i];

        if (! s.enabled || ! usesGain (s.type))
            continue;

        stops[(size_t) count++] = { juce::jlimit (0.0f, 1.0f,
                                                  frequencyToX ((double) s.frequency)
                                                    / juce::jmax (1.0f, (float) getWidth())),
                                    colours::forBand (i).brighter (0.15f).withAlpha (alpha) };
    }

    if (count == 0)
        return gradient;

    std::sort (stops.begin(), stops.begin() + count,
               [] (const Stop& a, const Stop& b) { return a.proportion < b.proportion; });

    // Los extremos toman el color de la banda mas cercana, para que el relleno
    // no vuelva al morado en los bordes del grafico.
    gradient.point1 = { 0.0f, 0.0f };
    gradient.point2 = { (float) getWidth(), 0.0f };
    gradient.clearColours();
    gradient.addColour (0.0, stops[0].colour);

    for (int i = 0; i < count; ++i)
        gradient.addColour (juce::jlimit (0.001, 0.999, (double) stops[(size_t) i].proportion),
                            stops[(size_t) i].colour);

    gradient.addColour (1.0, stops[(size_t) (count - 1)].colour);

    return gradient;
}

void EqCurveComponent::drawResponse (juce::Graphics& g) const
{
    const auto width = getWidth();

    if (width <= 0)
        return;

    const auto soloed = processor.getSoloBand();

    // Individual band contributions, faint, so it is obvious which band is
    // responsible for which part of the shape.
    for (int i = 0; i < numBands; ++i)
    {
        const auto& s = liveSettings[(size_t) i];

        if (! s.enabled)
            continue;

        /*  La forma de la banda se dibuja con la ganancia ajustada, no con la
            del instante: asi la curva de la banda pasa por su nodo y se queda
            quieta mientras suena la musica. Lo que se mueve es la curva blanca
            compuesta, que si usa la ganancia viva.
        */
        const auto& drawn = drawnSettings (i);

        juce::Path bandPath;

        for (int x = 0; x < width; x += 2)
        {
            const auto f = xToFrequency ((float) x);
            const auto db = juce::Decibels::gainToDecibels (
                EqBand::magnitudeForFrequency (drawn, f, displaySampleRate, filterMode), -120.0);

            const auto y = dbToY (db);

            if (x == 0)
                bandPath.startNewSubPath ((float) x, y);
            else
                bandPath.lineTo ((float) x, y);
        }

        const auto isActive = i == selectedBand || i == hoveredBand || i == soloed;

        /*  Una banda dinamica son dos formas: donde vive y hasta donde viaja.
            El area entre las dos es todo lo que la banda puede llegar a hacer,
            y la curva blanca se mueve por dentro de ella. Es lo que convierte
            "la banda esta en +7 y ahora mismo hace otra cosa" en una sola
            imagen en vez de en tres cosas sueltas.
        */
        if (settings[(size_t) i].dynamic && usesGain (drawn.type)
            && std::abs (settings[(size_t) i].dynRangeDb) > 0.01f)
        {
            const auto& target = dynTargetSettings (i);

            auto targetDbAt = [&] (int x)
            {
                const auto f = xToFrequency ((float) x);
                return juce::Decibels::gainToDecibels (
                    EqBand::magnitudeForFrequency (target, f, displaySampleRate, filterMode), -120.0);
            };

            // El area se cierra sola: la curva de reposo de ida y la del destino
            // de vuelta.
            auto between = bandPath;

            for (int x = width - 1; x >= 0; x -= 2)
                between.lineTo ((float) x, dbToY (targetDbAt (x)));

            between.closeSubPath();

            g.setColour (colours::forBand (i).withAlpha (isActive ? 0.20f : 0.12f));
            g.fillPath (between);

            // Y la forma del destino en discontinuo: es hasta donde llega, no
            // donde esta.
            juce::Path targetPath;

            for (int x = 0; x < width; x += 2)
            {
                const auto y = dbToY (targetDbAt (x));

                if (x == 0)
                    targetPath.startNewSubPath ((float) x, y);
                else
                    targetPath.lineTo ((float) x, y);
            }

            juce::Path dashed;
            const float dashes[] = { 5.0f, 4.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, targetPath, dashes, 2);

            g.setColour (colours::forBand (i).withAlpha (isActive ? 0.7f : 0.35f));
            g.fillPath (dashed);
        }

        g.setColour (colours::forBand (i).withAlpha (isActive ? 0.85f : 0.42f));
        g.strokePath (bandPath, juce::PathStrokeType (isActive ? 1.5f : 1.1f));
    }

    // Composite curve. While a band is soloed the chain is not what you are
    // hearing, so it steps back rather than pretending otherwise.
    const auto alpha = soloed >= 0 ? 0.35f : 0.92f;

    auto compositePath = [&] (EqEngine::Component component)
    {
        juce::Path curve;

        for (int x = 0; x < width; ++x)
        {
            const auto f = xToFrequency ((float) x);
            const auto db = EqEngine::magnitudeDbAt (liveSettings, f, displaySampleRate,
                                                     filterMode, component);
            const auto y = dbToY (db);

            if (x == 0)
                curve.startNewSubPath ((float) x, y);
            else
                curve.lineTo ((float) x, y);
        }

        return curve;
    };

    // The area between the curve and the zero line, filled in the brand accent.
    // This is what stops an EQ curve reading as an oscilloscope trace: the eye
    // sees a shape it can judge rather than a line it has to trace.
    auto fillTowardsZero = [&] (const juce::Path& source)
    {
        const auto zeroY = dbToY (0.0);

        auto filled = source;
        filled.lineTo ((float) width, zeroY);
        filled.lineTo (0.0f, zeroY);
        filled.closeSubPath();

        /*  El relleno toma el color de la banda que manda en cada zona: rojo
            debajo de la banda 1, ambar debajo de la 2, y una transicion entre
            las dos. Un solo color para toda la curva obliga a mirar el numero
            del nodo para saber de quien es cada montaña.
        */
        juce::Graphics::ScopedSaveState saved (g);
        g.reduceClipRegion (filled);

        g.setGradientFill (bandColourGradient (alpha * 0.34f));
        g.fillRect (getLocalBounds());

        /*  Y se apaga hacia arriba y hacia abajo desde la linea de cero, que es
            de donde crece la forma. Dos degradados en vez de uno porque el
            relleno sale de la linea en las dos direcciones.
        */
        const auto height = (float) getHeight();

        juce::ColourGradient up (colours::background.withAlpha (0.0f), 0.0f, zeroY,
                                 colours::background.withAlpha (0.45f), 0.0f, 0.0f, false);
        g.setGradientFill (up);
        g.fillRect (juce::Rectangle<float> (0.0f, 0.0f, (float) width, zeroY));

        juce::ColourGradient down (colours::background.withAlpha (0.0f), 0.0f, zeroY,
                                   colours::background.withAlpha (0.45f), 0.0f, height, false);
        g.setGradientFill (down);
        g.fillRect (juce::Rectangle<float> (0.0f, zeroY, (float) width, height - zeroY));
    };

    if (! EqEngine::hasSplitBands (liveSettings))
    {
        const auto composite = compositePath (EqEngine::Component::combined);

        fillTowardsZero (composite);

        g.setColour (colours::curve.withAlpha (alpha));
        g.strokePath (composite, juce::PathStrokeType (1.9f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        return;
    }

    // Once bands stop applying to the whole image there is no single curve to
    // draw, so the display splits: solid for the first output component, dashed
    // for the second.
    // With the curve split there are two shapes, so only the first is filled:
    // two overlapping washes would just be mud.
    const auto first = compositePath (EqEngine::Component::first);
    fillTowardsZero (first);

    g.setColour (colours::curve.withAlpha (alpha));
    g.strokePath (first, juce::PathStrokeType (1.9f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    const auto second = compositePath (EqEngine::Component::second);
    juce::Path dashed;
    const float dashes[] = { 6.0f, 4.0f };
    juce::PathStrokeType (1.7f).createDashedStroke (dashed, second, dashes, 2);

    g.setColour (colours::curve.withAlpha (alpha * 0.8f));
    g.fillPath (dashed);
}

void EqCurveComponent::drawNodes (juce::Graphics& g) const
{
    const auto soloed = processor.getSoloBand();

    for (int i = 0; i < numBands; ++i)
    {
        const auto& s = liveSettings[(size_t) i];

        if (! s.enabled)
            continue;

        const auto centre = nodePosition (i);
        const auto colour = colours::forBand (i);
        const auto isSelected = i == selectedBand;
        const auto radius = isSelected ? nodeRadius + 1.5f : nodeRadius;

        g.setColour (colours::background.withAlpha (0.8f));
        g.fillEllipse (juce::Rectangle<float> (radius * 2.0f + 4.0f, radius * 2.0f + 4.0f)
                         .withCentre (centre));

        g.setColour (colour.withAlpha (isSelected ? 1.0f : 0.85f));
        g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre),
                       isSelected ? 2.4f : 1.8f);

        g.setColour (colour.withAlpha (i == hoveredBand || isSelected ? 0.5f : 0.22f));
        g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));

        g.setColour (colours::text);
        g.setFont (fonts::bold (i >= 9 ? 8.5f : 9.5f));
        g.drawText (juce::String (i + 1),
                    juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre),
                    juce::Justification::centred, false);

        // L, R, M or S above the node when the band is not acting on the whole
        // stereo image. Nothing at all for a plain stereo band, which is most of
        // them, so the display stays quiet until it has something to say.
        auto badge = channelModeBadge (s.channel);

        if (settings[(size_t) i].dynamic)
            badge = badge.isEmpty() ? juce::String ("DYN") : badge + " DYN";

        if (badge.isNotEmpty())
        {
            g.setColour (colour);
            g.setFont (fonts::bold (9.5f));
            g.drawText (badge,
                        juce::Rectangle<float> (46.0f, 11.0f)
                          .withCentre ({ centre.x, centre.y - radius - 8.0f }),
                        juce::Justification::centred, false);
        }

        /*  El tirador del recorrido: mas pequeno que el nodo y hueco, porque no
            es otra banda, es hasta donde llega esta. Se arrastra igual.
        */
        if (settings[(size_t) i].dynamic && usesGain (s.type))
        {
            const auto handle = dynHandlePosition (i);
            const auto handleRadius = nodeRadius * 0.62f;

            g.setColour (colours::background.withAlpha (0.8f));
            g.fillEllipse (juce::Rectangle<float> (handleRadius * 2.0f + 3.0f,
                                                   handleRadius * 2.0f + 3.0f).withCentre (handle));

            g.setColour (colour.withAlpha (i == draggingDynHandle ? 1.0f : 0.8f));
            g.drawEllipse (juce::Rectangle<float> (handleRadius * 2.0f, handleRadius * 2.0f)
                             .withCentre (handle), 1.6f);
        }

        // A soloed band gets a ring so it is obvious why the audio changed.
        if (i == soloed)
        {
            g.setColour (colours::warning);
            g.drawEllipse (juce::Rectangle<float> (radius * 2.0f + 8.0f, radius * 2.0f + 8.0f)
                             .withCentre (centre), 1.4f);
        }
    }
}

void EqCurveComponent::drawEmptyHint (juce::Graphics& g) const
{
    for (const auto& s : settings)
        if (s.enabled)
            return;

    // The instruction belongs where the gesture happens, not in a caption at the
    // bottom of the window that nobody reads.
    // Lifted clear of the 0 dB line, which otherwise runs straight through the
    // second line of text.
    auto area = getLocalBounds().toFloat().withSizeKeepingCentre (420.0f, 52.0f)
                                          .translated (0.0f, -46.0f);

    g.setFont (fonts::regular (14.0f));
    g.setColour (colours::textDim);
    g.drawText ("Double-click anywhere to add a band",
                area.removeFromTop (22.0f), juce::Justification::centred, false);

    g.setFont (fonts::regular (12.0f));
    g.setColour (colours::textFaint);
    g.drawText ("or double-click a key on the keyboard to land on that note",
                area.removeFromTop (18.0f), juce::Justification::centred, false);
}

void EqCurveComponent::drawSplitLegend (juce::Graphics& g) const
{
    if (! EqEngine::hasSplitBands (settings))
        return;

    // Name the two curves after whichever families are actually in use. Someone
    // mixing left/right and mid/side bands in one instance gets both names, and
    // knows why.
    auto usesLeftRight = false, usesMidSide = false;

    for (const auto& s : settings)
    {
        if (! s.enabled)
            continue;

        if (s.channel == ChannelMode::left || s.channel == ChannelMode::right)
            usesLeftRight = true;
        else if (isMidSide (s.channel))
            usesMidSide = true;
    }

    const auto firstName = usesLeftRight && usesMidSide ? "L / M"
                         : usesMidSide                  ? "M"
                                                        : "L";
    const auto secondName = usesLeftRight && usesMidSide ? "R / S"
                          : usesMidSide                  ? "S"
                                                         : "R";

    auto area = getLocalBounds().reduced (10, 8).removeFromTop (14).removeFromLeft (170);
    area.removeFromLeft (44);   // clear of the dB labels

    g.setFont (fonts::bold (10.5f));

    const auto y = (float) area.getCentreY();
    auto x = (float) area.getX();

    g.setColour (colours::curve.withAlpha (0.9f));
    g.fillRect (juce::Rectangle<float> (x, y - 1.0f, 14.0f, 2.0f));
    g.drawText (firstName, juce::Rectangle<float> (x + 18.0f, y - 7.0f, 44.0f, 14.0f),
                juce::Justification::centredLeft, false);

    x += 66.0f;

    g.setColour (colours::curve.withAlpha (0.7f));

    for (int dash = 0; dash < 3; ++dash)
        g.fillRect (juce::Rectangle<float> (x + (float) dash * 6.0f, y - 1.0f, 3.5f, 2.0f));

    g.drawText (secondName, juce::Rectangle<float> (x + 22.0f, y - 7.0f, 44.0f, 14.0f),
                juce::Justification::centredLeft, false);
}

void EqCurveComponent::drawCursorReadout (juce::Graphics& g) const
{
    if (! mouseIsOver || draggingBand >= 0)
        return;

    const auto frequency = xToFrequency (lastMousePosition.x);
    const auto label = format::frequency ((float) frequency)
                     + "   " + format::noteName ((float) frequency)
                     + "   " + format::decibels ((float) yToDb (lastMousePosition.y));

    g.setFont (fonts::regular (10.5f));
    g.setColour (colours::textDim);
    g.drawText (label,
                getLocalBounds().reduced (8, 6).removeFromTop (14),
                juce::Justification::topRight, false);
}

//==============================================================================
void EqCurveComponent::setParameter (const juce::String& id, float value)
{
    if (auto* parameter = state.getParameter (id))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

float EqCurveComponent::getParameter (const juce::String& id) const
{
    if (auto* raw = state.getRawParameterValue (id))
        return raw->load();

    return 0.0f;
}

void EqCurveComponent::setSelectedBand (int band)
{
    if (selectedBand == band)
        return;

    selectedBand = band;

    if (onBandSelected != nullptr)
        onBandSelected (band);

    repaint();
}

void EqCurveComponent::toggleSolo (int band)
{
    processor.setSoloBand (processor.getSoloBand() == band ? -1 : band);
    repaint();
}

void EqCurveComponent::beginDrag (int band)
{
    draggingBand = band;

    for (auto id : { ids::bandFreq (band), ids::bandGain (band) })
        if (auto* parameter = state.getParameter (id))
            parameter->beginChangeGesture();
}

void EqCurveComponent::endDrag()
{
    if (draggingDynHandle >= 0)
    {
        if (auto* parameter = state.getParameter (ids::bandDynRange (draggingDynHandle)))
            parameter->endChangeGesture();

        draggingDynHandle = -1;
    }

    if (draggingBand < 0)
        return;

    for (auto id : { ids::bandFreq (draggingBand), ids::bandGain (draggingBand) })
        if (auto* parameter = state.getParameter (id))
            parameter->endChangeGesture();

    draggingBand = -1;
}

int EqCurveComponent::addBand (double frequency, double gainDb, FilterType type)
{
    for (int i = 0; i < numBands; ++i)
    {
        if (settings[(size_t) i].enabled)
            continue;

        setParameter (ids::bandType (i), (float) (int) type);
        setParameter (ids::bandFreq (i), (float) juce::jlimit (minFrequency, maxFrequency, frequency));
        setParameter (ids::bandGain (i), (float) juce::jlimit (-24.0, 24.0, gainDb));
        setParameter (ids::bandQ (i), 0.707f);
        setParameter (ids::bandEnabled (i), 1.0f);

        settings[(size_t) i] = readBand (state, i);
        setSelectedBand (i);
        return i;
    }

    return -1;
}

void EqCurveComponent::addBandAt (juce::Point<float> position)
{
    addBand (xToFrequency (position.x), yToDb (position.y), FilterType::bell);
}

void EqCurveComponent::showBandMenu (int band)
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());

    const auto& s = settings[(size_t) band];

    const auto names = filterTypeNames();

    for (int i = 0; i < names.size(); ++i)
        menu.addItem (100 + i, names[i], true, i == (int) s.type);

    if (isCut (s.type))
    {
        juce::PopupMenu slopes;
        const auto slopeList = slopeNames();

        for (int i = 0; i < slopeList.size(); ++i)
            slopes.addItem (200 + i, slopeList[i], true, i == s.slopeStages - 1);

        menu.addSeparator();
        menu.addSubMenu ("Slope", slopes);
    }

    juce::PopupMenu channels;
    const auto channelList = channelModeNames();

    for (int i = 0; i < channelList.size(); ++i)
        channels.addItem (400 + i, channelList[i], true, i == (int) s.channel);

    menu.addSeparator();
    menu.addSubMenu ("Stereo placement", channels);

    menu.addSeparator();
    menu.addItem (310, "Dynamic", usesGain (s.type), s.dynamic);
    menu.addItem (300, "Solo band", true, processor.getSoloBand() == band);
    menu.addItem (301, "Invert gain", usesGain (s.type));
    menu.addItem (302, "Reset gain", usesGain (s.type));
    menu.addItem (303, "Remove band");

    menu.showMenuAsync (juce::PopupMenu::Options().withParentComponent (getTopLevelComponent()),
                        [this, band] (int result)
                        {
                            if (result >= 100 && result < 200)
                                setParameter (ids::bandType (band), (float) (result - 100));
                            else if (result >= 200 && result < 300)
                                setParameter (ids::bandSlope (band), (float) (result - 200));
                            else if (result >= 400 && result < 500)
                                setParameter (ids::bandChannel (band), (float) (result - 400));
                            else if (result == 310)
                                setParameter (ids::bandDynamic (band),
                                              settings[(size_t) band].dynamic ? 0.0f : 1.0f);
                            else if (result == 300)
                                toggleSolo (band);
                            else if (result == 301)
                                setParameter (ids::bandGain (band), -getParameter (ids::bandGain (band)));
                            else if (result == 302)
                                setParameter (ids::bandGain (band), 0.0f);
                            else if (result == 303)
                            {
                                setParameter (ids::bandEnabled (band), 0.0f);

                                if (processor.getSoloBand() == band)
                                    processor.setSoloBand (-1);

                                setSelectedBand (-1);
                            }
                        });
}

//==============================================================================
void EqCurveComponent::mouseDown (const juce::MouseEvent& event)
{
    /*  El tirador del recorrido se mira antes que los nodos: cuando el recorrido
        es pequeno los dos circulos casi se tocan, y el de arriba es el que ya
        estaba ahi. Si ganara el nodo no habria forma de coger el otro.
    */
    if (! event.mods.isPopupMenu() && ! event.mods.isAltDown())
    {
        const auto handleBand = dynHandleAt (event.position);

        if (handleBand >= 0)
        {
            setSelectedBand (handleBand);
            draggingDynHandle = handleBand;
            dragOffset = dynHandlePosition (handleBand) - event.position;

            if (auto* parameter = state.getParameter (ids::bandDynRange (handleBand)))
                parameter->beginChangeGesture();

            return;
        }
    }

    const auto band = bandAt (event.position);

    if (band < 0)
    {
        setSelectedBand (-1);
        return;
    }

    setSelectedBand (band);

    if (event.mods.isPopupMenu())
    {
        showBandMenu (band);
        return;
    }

    // Alt-click solos, the same shortcut people already use elsewhere.
    if (event.mods.isAltDown())
    {
        toggleSolo (band);
        return;
    }

    dragOffset = nodePosition (band) - event.position;
    beginDrag (band);
}

void EqCurveComponent::mouseDrag (const juce::MouseEvent& event)
{
    lastMousePosition = event.position;

    if (draggingDynHandle >= 0)
    {
        // El tirador solo se mueve en vertical: la frecuencia es de la banda, y
        // arrastrarlo de lado moveria las dos cosas a la vez.
        const auto band = draggingDynHandle;
        const auto wanted = yToDb (event.position.y + dragOffset.y);
        const auto range = juce::jlimit (-48.0, 48.0,
                                         wanted - (double) settings[(size_t) band].gainDb);

        setParameter (ids::bandDynRange (band), (float) range);
        settings[(size_t) band] = readBand (state, band);
        repaint();
        return;
    }

    if (draggingBand < 0)
        return;

    const auto target = event.position + dragOffset;
    const auto& s = settings[(size_t) draggingBand];

    setParameter (ids::bandFreq (draggingBand),
                  (float) juce::jlimit (minFrequency, maxFrequency, xToFrequency (target.x)));

    if (usesGain (s.type))
        setParameter (ids::bandGain (draggingBand),
                      (float) juce::jlimit (-24.0, 24.0, yToDb (target.y)));

    settings[(size_t) draggingBand] = readBand (state, draggingBand);
    repaint();
}

void EqCurveComponent::mouseUp (const juce::MouseEvent&)
{
    endDrag();
}

void EqCurveComponent::mouseMove (const juce::MouseEvent& event)
{
    lastMousePosition = event.position;
    mouseIsOver = true;

    const auto band = bandAt (event.position);

    if (band != hoveredBand)
    {
        hoveredBand = band;
        setMouseCursor (band >= 0 ? juce::MouseCursor::DraggingHandCursor
                                  : juce::MouseCursor::NormalCursor);
    }

    repaint();
}

void EqCurveComponent::mouseExit (const juce::MouseEvent&)
{
    mouseIsOver = false;
    hoveredBand = -1;
    repaint();
}

void EqCurveComponent::mouseDoubleClick (const juce::MouseEvent& event)
{
    const auto band = bandAt (event.position);

    if (band >= 0)
    {
        setParameter (ids::bandEnabled (band), 0.0f);

        if (processor.getSoloBand() == band)
            processor.setSoloBand (-1);

        setSelectedBand (-1);
    }
    else
    {
        addBandAt (event.position);
    }
}

void EqCurveComponent::mouseWheelMove (const juce::MouseEvent& event,
                                       const juce::MouseWheelDetails& wheel)
{
    const auto band = draggingBand >= 0 ? draggingBand
                    : hoveredBand >= 0  ? hoveredBand
                                        : selectedBand;

    if (band < 0)
        return;

    juce::ignoreUnused (event);

    if (isCut (settings[(size_t) band].type))
    {
        // Cuts have no Q; the wheel steps through the slope instead.
        const auto current = settings[(size_t) band].slopeStages - 1;
        const auto next = juce::jlimit (0, 7, current + (wheel.deltaY > 0.0f ? 1 : -1));
        setParameter (ids::bandSlope (band), (float) next);
    }
    else
    {
        // Multiplicative so the feel is the same at Q 0.2 and Q 12.
        const auto current = getParameter (ids::bandQ (band));
        const auto factor = std::pow (2.0f, wheel.deltaY * (wheel.isReversed ? -2.0f : 2.0f));
        setParameter (ids::bandQ (band), juce::jlimit (0.1f, 18.0f, current * factor));
    }

    settings[(size_t) band] = readBand (state, band);
    repaint();
}
} // namespace nodo::eq
