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

void NodoMeter::setCaption (juce::String newCaption)
{
    caption = std::move (newCaption);
    repaint();
}

void NodoMeter::timerCallback()
{
    if (levelSource == nullptr)
        return;

    for (int ch = 0; ch < channels; ++ch)
    {
        const auto db = juce::Decibels::gainToDecibels (levelSource (ch), floorDb);

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

    repaint();
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
