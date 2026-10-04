namespace nodo
{
using namespace nodo::theme;

NodoMeter::NodoMeter()
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (24);
}

NodoMeter::~NodoMeter()
{
    stopTimer();
}

void NodoMeter::setSource (std::function<float (int)> source, int numChannels)
{
    levelSource = std::move (source);
    channels = juce::jlimit (1, maxChannels, numChannels);
}

void NodoMeter::setRmsSource (std::function<float (int)> source)
{
    rmsSource = std::move (source);
}

void NodoMeter::setCaption (juce::String newCaption)
{
    caption = std::move (newCaption);
    repaint();
}

void NodoMeter::timerCallback()
{
    if (levelSource == nullptr)
        return;

    auto loudest = floorDb;

    for (int ch = 0; ch < channels; ++ch)
    {
        const auto db = juce::Decibels::gainToDecibels (levelSource (ch), floorDb);
        loudest = juce::jmax (loudest, db);

        // The bar itself falls smoothly; the peak marker holds for about a
        // second and then drops, which is what makes a transient readable.
        levelDb[(size_t) ch] = db > levelDb[(size_t) ch]
                             ? db
                             : juce::jmax (db, levelDb[(size_t) ch] - 1.4f);

        if (db >= peakDb[(size_t) ch])
        {
            peakDb[(size_t) ch] = db;
            peakAge[(size_t) ch] = 0;
        }
        else if (++peakAge[(size_t) ch] > 24)
        {
            peakDb[(size_t) ch] = juce::jmax (db, peakDb[(size_t) ch] - 1.4f);
        }
    }

    /*  El numero de pico se queda quieto mucho mas tiempo que la marca de la
        barra. Un numero que cambia veinticuatro veces por segundo no se puede
        leer: lo que uno quiere saber es a cuanto ha llegado esto, no a cuanto
        esta ahora mismo.
    */
    if (loudest >= shownPeakDb)
    {
        shownPeakDb = loudest;
        shownPeakAge = 0;
    }
    else if (++shownPeakAge > 72)       // unos tres segundos a 24 fps
    {
        shownPeakDb = juce::jmax (loudest, shownPeakDb - 3.0f);
    }

    if (rmsSource != nullptr)
    {
        auto sum = 0.0f;

        for (int ch = 0; ch < channels; ++ch)
        {
            const auto value = rmsSource (ch);
            sum += value * value;
        }

        const auto db = juce::Decibels::gainToDecibels (std::sqrt (sum / (float) channels), floorDb);

        // El RMS ya viene integrado del seguidor; aqui solo se le quita el
        // temblor de la ultima cifra.
        shownRmsDb = shownRmsDb * 0.8f + db * 0.2f;
    }

    repaint();
}

juce::String NodoMeter::formatDb (float db)
{
    /*  "-inf" y no una raya: ocupa casi lo mismo que un numero de verdad, asi
        que la etiqueta de al lado no baila segun haya senal o no.
    */
    if (db <= floorDb + 0.5f)
        return "-inf";

    return juce::String (db, 1);
}

void NodoMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    if (caption.isNotEmpty())
    {
        g.setFont (fonts::bold (8.5f));
        g.setColour (colours::textFaint);
        g.drawText (caption, bounds.removeFromLeft (24.0f),
                    juce::Justification::centredLeft, false);
    }

    /*  Las cifras solo aparecen si queda barra de sobra. Con la ventana en su
        tamano minimo la cabecera se queda sin sitio, y una barra de veinte
        pixeles no dice nada mientras que la barra larga sin numeros sigue
        diciendo lo que siempre ha dicho.
    */
    const auto showNumbers = rmsSource != nullptr && bounds.getWidth() > 120.0f;

    if (showNumbers)
    {
        auto numbers = bounds.removeFromRight (44.0f);
        bounds.removeFromRight (6.0f);

        const auto lineHeight = juce::jmin (11.0f, numbers.getHeight() * 0.5f);
        auto stack = numbers.withSizeKeepingCentre (numbers.getWidth(), lineHeight * 2.0f);

        /*  La cifra ocupa siempre el mismo hueco aunque mida menos, y la
            etiqueta va pegada a su izquierda. Si la etiqueta se alineara al
            borde de la columna, "pk" y su numero acabarian separados por un
            palmo de hueco cada vez que el valor fuera corto, y al cambiar de
            -9.5 a -10.4 saltaria de sitio.
        */
        auto drawLine = [&g, &stack, lineHeight] (const juce::String& label,
                                                  const juce::String& value,
                                                  juce::Colour valueColour)
        {
            auto row = stack.removeFromTop (lineHeight);
            auto valueBox = row.removeFromRight (26.0f);

            g.setFont (fonts::regular (9.0f));
            g.setColour (valueColour);
            g.drawText (value, valueBox, juce::Justification::centredRight, false);

            g.setFont (fonts::bold (7.0f));
            g.setColour (colours::textFaint);
            g.drawText (label, row.withTrimmedRight (3.0f),
                        juce::Justification::centredRight, false);
        };

        drawLine ("pk", formatDb (shownPeakDb),
                  shownPeakDb > -1.0f ? colours::meterOver : colours::text);
        drawLine ("rms", formatDb (shownRmsDb), colours::textDim);
    }

    const auto barHeight = 3.0f;
    const auto gap = 2.0f;
    const auto totalHeight = (float) channels * barHeight + (float) (channels - 1) * gap;
    auto area = bounds.withSizeKeepingCentre (bounds.getWidth(), totalHeight);

    auto positionFor = [&area] (float db)
    {
        return area.getWidth() * juce::jlimit (0.0f, 1.0f, (db - floorDb) / -floorDb);
    };

    for (int ch = 0; ch < channels; ++ch)
    {
        auto row = area.removeFromTop (barHeight);
        area.removeFromTop (gap);

        g.setColour (colours::meterTrack);
        g.fillRoundedRectangle (row, 1.5f);

        const auto db = levelDb[(size_t) ch];

        if (db > floorDb + 0.5f)
        {
            const auto colour = db > -1.0f ? colours::meterOver
                              : db > -6.0f ? colours::meterHot
                                           : colours::meterNormal();

            g.setColour (colour);
            g.fillRoundedRectangle (row.withWidth (positionFor (db)), 1.5f);
        }

        const auto peak = peakDb[(size_t) ch];

        if (peak > floorDb + 0.5f)
        {
            g.setColour (peak > -1.0f ? colours::meterOver : colours::text.withAlpha (0.7f));
            g.fillRect (juce::Rectangle<float> (row.getX() + positionFor (peak) - 1.0f,
                                                row.getY(), 1.5f, row.getHeight()));
        }
    }
}
} // namespace nodo
