#include "LimitDisplay.h"

namespace nodo::limit
{
using namespace nodo::theme;

namespace
{
    float levelDbFromMagnitude (float magnitude)
    {
        return 20.0f * std::log10 (juce::jmax (1.0e-6f, magnitude));
    }

    juce::String lufsText (float value)
    {
        return value <= -150.0f ? juce::String ("--.-")
                                : juce::String (value, 1);
    }
}

LimitDisplay::LimitDisplay (NodoLimitProcessor& processorToUse)
    : processor (processorToUse)
{
    inputHistory.fill (-400.0f);
    outputHistory.fill (-400.0f);
    reductionHistory.fill (0.0f);

    setOpaque (false);
    startTimerHz (30);
}

LimitDisplay::~LimitDisplay()
{
    stopTimer();
}

void LimitDisplay::timerCallback()
{
    const auto input = juce::jmax (processor.getMeterLevel (0), processor.getMeterLevel (1));
    const auto output = juce::jmax (processor.getMeterLevel (2), processor.getMeterLevel (3));

    inputHistory[(size_t) writePos] = levelDbFromMagnitude (input);
    outputHistory[(size_t) writePos] = levelDbFromMagnitude (output);
    reductionHistory[(size_t) writePos] = processor.getGainReductionDb();

    // Peak hold on the output meter: falls a decibel every frame once it has
    // had a moment to be read.
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto level = 20.0f * std::log10 (juce::jmax (1.0e-6f, processor.getMeterLevel (2 + ch)));
        auto& held = heldPeakDb[(size_t) ch];

        held = level > held ? level : held - 0.6f;
    }

    writePos = (writePos + 1) % historyPoints;

    repaint();
}

void LimitDisplay::resized()
{
    auto bounds = getLocalBounds().toFloat();

    auto right = bounds.removeFromRight (juce::jlimit (180.0f, 260.0f, bounds.getWidth() * 0.26f));

    // The loudness box is only as tall as what it has to say; the rest of the
    // column goes to a meter, which is what the space was crying out for.
    loudnessArea = right.removeFromTop (196.0f);
    meterArea = right;

    historyArea = bounds.reduced (2.0f, 10.0f);
}

float LimitDisplay::levelToY (float db) const
{
    const auto proportion = (topDb - db) / (topDb - bottomDb);
    return historyArea.getY() + proportion * historyArea.getHeight();
}

void LimitDisplay::drawHistory (juce::Graphics& g) const
{
    const auto settings = const_cast<NodoLimitProcessor&> (processor).getSettings();
    const auto step = historyArea.getWidth() / (float) (historyPoints - 1);

    g.setFont (fonts::regular (9.5f));

    for (auto db : { 0.0f, -12.0f, -24.0f, -36.0f })
    {
        const auto y = levelToY (db);

        g.setColour (juce::approximatelyEqual (db, 0.0f) ? colours::gridStrong : colours::grid);
        g.drawHorizontalLine ((int) y, historyArea.getX(), historyArea.getRight());

        g.setColour (colours::textFaint);
        g.drawText (juce::String ((int) db), historyArea.getX() + 4.0f, y + 1.0f, 30.0f, 11.0f,
                    juce::Justification::topLeft, false);
    }

    auto buildPath = [this, step] (const std::array<float, historyPoints>& history)
    {
        juce::Path path;
        path.startNewSubPath (historyArea.getX(), historyArea.getBottom());

        for (int i = 0; i < historyPoints; ++i)
        {
            const auto index = (writePos + i) % historyPoints;
            const auto db = juce::jlimit (bottomDb, topDb, history[(size_t) index]);
            path.lineTo (historyArea.getX() + (float) i * step, levelToY (db));
        }

        path.lineTo (historyArea.getRight(), historyArea.getBottom());
        path.closeSubPath();

        return path;
    };

    // Input behind, output in front: the gap between them is the work.
    g.setColour (colours::analyserFill.withAlpha (0.9f));
    g.fillPath (buildPath (inputHistory));

    g.setColour (colours::accent().withAlpha (0.45f));
    g.fillPath (buildPath (outputHistory));

    // The ceiling.
    const auto ceilingY = levelToY (settings.ceilingDb);
    g.setColour (colours::warning.withAlpha (0.75f));
    const float dashes[] { 4.0f, 3.0f };
    g.drawDashedLine ({ historyArea.getX(), ceilingY, historyArea.getRight(), ceilingY },
                      dashes, 2, 1.0f);

    // Gain reduction, hanging from the top, on its own scale.
    const auto reductionScale = historyArea.getHeight() / 18.0f;

    juce::Path reduction;
    reduction.startNewSubPath (historyArea.getX(), historyArea.getY());

    for (int i = 0; i < historyPoints; ++i)
    {
        const auto index = (writePos + i) % historyPoints;
        const auto value = juce::jlimit (-18.0f, 0.0f, reductionHistory[(size_t) index]);
        reduction.lineTo (historyArea.getX() + (float) i * step,
                          historyArea.getY() - value * reductionScale);
    }

    reduction.lineTo (historyArea.getRight(), historyArea.getY());
    reduction.closeSubPath();

    g.setColour (colours::danger.withAlpha (0.22f));
    g.fillPath (reduction);
    g.setColour (colours::danger.withAlpha (0.65f));
    g.strokePath (reduction, juce::PathStrokeType (1.0f));

    g.setColour (colours::textFaint);
    g.setFont (fonts::bold (8.5f));
    g.drawText ("GR", historyArea.getRight() - 24.0f, historyArea.getY() + 2.0f, 22.0f, 10.0f,
                juce::Justification::centredRight, false);

    for (auto value : { -6.0f, -12.0f })
    {
        const auto y = historyArea.getY() - value * reductionScale;

        g.setColour (colours::grid.withAlpha (0.6f));
        g.drawHorizontalLine ((int) y, historyArea.getRight() - 24.0f, historyArea.getRight());

        g.setColour (colours::textFaint);
        g.setFont (fonts::regular (9.0f));
        g.drawText (juce::String ((int) value), historyArea.getRight() - 58.0f, y - 5.0f, 30.0f, 11.0f,
                    juce::Justification::centredRight, false);
    }
}

void LimitDisplay::drawLoudness (juce::Graphics& g) const
{
    auto area = loudnessArea.reduced (10.0f, 12.0f);

    g.setColour (colours::panel);
    g.fillRoundedRectangle (area, metrics::cornerRadius);

    auto inner = area.reduced (12.0f, 10.0f);

    g.setFont (fonts::bold (8.5f));
    g.setColour (colours::textFaint);
    g.drawText ("LOUDNESS", inner.removeFromTop (12.0f), juce::Justification::centredLeft, false);

    inner.removeFromTop (4.0f);

    auto row = [&g, &inner] (const juce::String& caption, const juce::String& value,
                             float captionSize, float valueSize, juce::Colour colour)
    {
        auto line = inner.removeFromTop (valueSize + 6.0f);

        g.setFont (fonts::regular (captionSize));
        g.setColour (colours::textDim);
        g.drawText (caption, line, juce::Justification::centredLeft, false);

        g.setFont (fonts::mono (valueSize));
        g.setColour (colour);
        g.drawText (value, line, juce::Justification::centredRight, false);
    };

    row ("Integrated", lufsText (processor.getIntegratedLufs()) + " LUFS", 11.0f, 17.0f, colours::text);
    inner.removeFromTop (2.0f);
    row ("Short term", lufsText (processor.getShortTermLufs()), 10.5f, 12.5f, colours::textDim);
    row ("Momentary", lufsText (processor.getMomentaryLufs()), 10.5f, 12.5f, colours::textDim);

    inner.removeFromTop (8.0f);

    const auto peak = processor.getOutputPeakDb();
    const auto ceiling = const_cast<NodoLimitProcessor&> (processor).getSettings().ceilingDb;

    // Over the ceiling is impossible by construction, so this readout is really
    // asking "did the thing I believe about this plugin hold". Coloured for the
    // moment it does not.
    row ("True peak", peak <= -150.0f ? juce::String ("--.-")
                                      : juce::String (peak, 2) + " dB",
         10.5f, 13.0f,
         peak > ceiling + 0.01f ? colours::danger : colours::text);

    // Distance to the target, which is the number someone about to upload
    // actually wants. Green-ish when it is close enough to stop worrying.
    const auto settings = const_cast<NodoLimitProcessor&> (processor).getSettings();

    if (settings.target != LoudnessTarget::off)
    {
        const auto distance = const_cast<NodoLimitProcessor&> (processor).getDistanceToTargetLu();
        const auto integrated = processor.getIntegratedLufs();

        inner.removeFromTop (2.0f);

        if (integrated <= -150.0f)
        {
            row ("To target", "--.-", 10.5f, 13.0f, colours::textDim);
        }
        else
        {
            const auto close = std::abs (distance) <= 0.5f;
            row ("To target",
                 (distance > 0.0f ? "+" : "") + juce::String (distance, 1) + " dB",
                 10.5f, 13.0f,
                 close ? colours::accentBright() : colours::warning);
        }
    }

    // The safety clipper's counter. It should read zero for ever; showing it is
    // how that stops being a promise and starts being visible.
    const auto clips = processor.getSafetyClipCount();
    row ("Safety clips", juce::String (clips), 10.0f, 11.0f,
         clips > 0 ? colours::danger : colours::textFaint);

    inner.removeFromTop (1.0f);

    g.setFont (fonts::regular (9.5f));
    g.setColour (colours::textFaint);
    g.drawText ("click to reset", inner.removeFromTop (12.0f),
                juce::Justification::centredRight, false);
}

void LimitDisplay::drawOutputMeter (juce::Graphics& g) const
{
    auto area = meterArea.reduced (10.0f, 4.0f);

    g.setColour (colours::panel);
    g.fillRoundedRectangle (area, metrics::cornerRadius);

    auto inner = area.reduced (12.0f, 10.0f);

    g.setFont (fonts::bold (8.5f));
    g.setColour (colours::textFaint);
    g.drawText ("OUTPUT", inner.removeFromTop (12.0f), juce::Justification::centredLeft, false);
    inner.removeFromTop (4.0f);

    // Scale down the right hand side.
    auto scale = inner.removeFromRight (26.0f);
    const auto ceiling = const_cast<NodoLimitProcessor&> (processor).getSettings().ceilingDb;

    auto toY = [&inner] (float db)
    {
        const auto proportion = juce::jlimit (0.0f, 1.0f, (0.0f - db) / 45.0f);
        return inner.getY() + proportion * inner.getHeight();
    };

    g.setFont (fonts::regular (9.0f));

    for (auto db : { 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f })
    {
        const auto y = toY (db);

        g.setColour (colours::grid);
        g.drawHorizontalLine ((int) y, inner.getX(), inner.getRight());

        g.setColour (colours::textFaint);
        g.drawText (juce::String ((int) db), scale.getX() + 2.0f, y - 5.0f,
                    scale.getWidth(), 10.0f, juce::Justification::centredLeft, false);
    }

    const auto barWidth = (inner.getWidth() - 4.0f) * 0.5f;

    for (int ch = 0; ch < 2; ++ch)
    {
        auto bar = inner.withWidth (barWidth).translated ((barWidth + 4.0f) * (float) ch, 0.0f);

        g.setColour (colours::meterTrack);
        g.fillRect (bar);

        const auto level = 20.0f * std::log10 (juce::jmax (1.0e-6f, processor.getMeterLevel (2 + ch)));
        const auto y = toY (level);

        if (level > -45.0f)
        {
            auto filled = bar.withTop (y);
            g.setColour (level > ceiling + 0.05f ? colours::meterOver
                       : level > ceiling - 6.0f  ? colours::meterHot
                                                 : colours::meterNormal());
            g.fillRect (filled);
        }

        const auto held = heldPeakDb[(size_t) ch];

        if (held > -45.0f)
        {
            g.setColour (held > ceiling + 0.05f ? colours::meterOver : colours::text);
            g.fillRect (bar.withY (toY (held)).withHeight (1.5f));
        }
    }

    // The ceiling, so the bar has something to be under.
    g.setColour (colours::warning.withAlpha (0.7f));
    const float dashes[] { 3.0f, 3.0f };
    const auto ceilingY = toY (ceiling);
    g.drawDashedLine ({ inner.getX(), ceilingY, inner.getRight(), ceilingY }, dashes, 2, 1.0f);
}

void LimitDisplay::paint (juce::Graphics& g)
{
    g.setColour (colours::background);
    g.fillRect (getLocalBounds());

    drawHistory (g);
    drawLoudness (g);
    drawOutputMeter (g);
}

void LimitDisplay::mouseDown (const juce::MouseEvent& event)
{
    if (loudnessArea.contains (event.position) || meterArea.contains (event.position))
    {
        processor.requestMeterReset();
        heldPeakDb.fill (-100.0f);
    }
}
} // namespace nodo::limit
