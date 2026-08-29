#include "EssDisplay.h"

namespace nodo::ess
{
using namespace nodo::theme;

EssDisplay::EssDisplay (NodoEssProcessor& processorToUse)
    : processor (processorToUse)
{
    detectorHistory.fill (-400.0f);
    thresholdHistory.fill (-24.0f);
    reductionHistory.fill (0.0f);

    setOpaque (false);
    startTimerHz (30);
}

EssDisplay::~EssDisplay()
{
    stopTimer();
}

void EssDisplay::timerCallback()
{
    detectorHistory[(size_t) writePos] = processor.getDetectorDb();
    thresholdHistory[(size_t) writePos] = processor.getEffectiveThresholdDb();
    reductionHistory[(size_t) writePos] = processor.getGainReductionDb();

    writePos = (writePos + 1) % historyPoints;

    repaint();
}

void EssDisplay::resized()
{
    auto bounds = getLocalBounds().toFloat();

    readoutArea = bounds.removeFromRight (juce::jlimit (120.0f, 190.0f, bounds.getWidth() * 0.22f));
    historyArea = bounds.reduced (2.0f, 10.0f);
}

float EssDisplay::levelToY (float db) const
{
    const auto proportion = (topDb - db) / (topDb - bottomDb);
    return historyArea.getY() + proportion * historyArea.getHeight();
}

float EssDisplay::yToLevel (float y) const
{
    const auto proportion = (y - historyArea.getY()) / juce::jmax (1.0f, historyArea.getHeight());
    return topDb - proportion * (topDb - bottomDb);
}

void EssDisplay::paint (juce::Graphics& g)
{
    g.setColour (colours::background);
    g.fillRect (getLocalBounds());

    const auto settings = processor.getSettings();
    const auto step = historyArea.getWidth() / (float) (historyPoints - 1);

    g.setFont (fonts::regular (9.5f));

    for (auto db : { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f })
    {
        const auto y = levelToY (db);

        g.setColour (juce::approximatelyEqual (db, 0.0f) ? colours::gridStrong : colours::grid);
        g.drawHorizontalLine ((int) y, historyArea.getX(), historyArea.getRight());

        g.setColour (colours::textFaint);
        g.drawText (juce::String ((int) db), historyArea.getX() + 4.0f, y + 1.0f, 30.0f, 11.0f,
                    juce::Justification::topLeft, false);
    }

    // The band's level, filled.
    juce::Path level;
    level.startNewSubPath (historyArea.getX(), historyArea.getBottom());

    for (int i = 0; i < historyPoints; ++i)
    {
        const auto index = (writePos + i) % historyPoints;
        const auto db = juce::jlimit (bottomDb, topDb, detectorHistory[(size_t) index]);
        level.lineTo (historyArea.getX() + (float) i * step, levelToY (db));
    }

    level.lineTo (historyArea.getRight(), historyArea.getBottom());
    level.closeSubPath();

    g.setColour (colours::accent().withAlpha (0.35f));
    g.fillPath (level);

    // The threshold, as a line that moves when the detector is adaptive.
    juce::Path threshold;
    auto started = false;

    for (int i = 0; i < historyPoints; ++i)
    {
        const auto index = (writePos + i) % historyPoints;
        const auto y = levelToY (juce::jlimit (bottomDb, topDb, thresholdHistory[(size_t) index]));
        const auto x = historyArea.getX() + (float) i * step;

        if (! started) { threshold.startNewSubPath (x, y); started = true; }
        else            threshold.lineTo (x, y);
    }

    g.setColour (dragging ? colours::warning : colours::warning.withAlpha (0.8f));
    g.strokePath (threshold, juce::PathStrokeType (1.4f));

    // Reduction, hanging from the top.
    const auto reductionScale = historyArea.getHeight() / 24.0f;

    juce::Path reduction;
    reduction.startNewSubPath (historyArea.getX(), historyArea.getY());

    for (int i = 0; i < historyPoints; ++i)
    {
        const auto index = (writePos + i) % historyPoints;
        const auto value = juce::jlimit (-24.0f, 0.0f, reductionHistory[(size_t) index]);
        reduction.lineTo (historyArea.getX() + (float) i * step,
                          historyArea.getY() - value * reductionScale);
    }

    reduction.lineTo (historyArea.getRight(), historyArea.getY());
    reduction.closeSubPath();

    g.setColour (colours::danger.withAlpha (0.22f));
    g.fillPath (reduction);
    g.setColour (colours::danger.withAlpha (0.7f));
    g.strokePath (reduction, juce::PathStrokeType (1.0f));

    g.setColour (colours::textFaint);
    g.setFont (fonts::bold (8.5f));
    g.drawText ("GR", historyArea.getRight() - 24.0f, historyArea.getY() + 2.0f, 22.0f, 10.0f,
                juce::Justification::centredRight, false);

    // Readout panel.
    auto panel = readoutArea.reduced (10.0f, 12.0f);

    g.setColour (colours::panel);
    g.fillRoundedRectangle (panel, metrics::cornerRadius);

    auto inner = panel.reduced (12.0f, 10.0f);

    auto row = [&g, &inner] (const juce::String& caption, const juce::String& value,
                             float valueSize, juce::Colour colour)
    {
        auto line = inner.removeFromTop (valueSize + 8.0f);

        g.setFont (fonts::regular (10.5f));
        g.setColour (colours::textDim);
        g.drawText (caption, line, juce::Justification::centredLeft, false);

        g.setFont (fonts::mono (valueSize));
        g.setColour (colour);
        g.drawText (value, line, juce::Justification::centredRight, false);
    };

    g.setFont (fonts::bold (8.5f));
    g.setColour (colours::textFaint);
    g.drawText ("SIBILANCE", inner.removeFromTop (12.0f), juce::Justification::centredLeft, false);
    inner.removeFromTop (4.0f);

    const auto reductionNow = processor.getGainReductionDb();

    row ("Reduction", juce::String (reductionNow, 1) + " dB", 16.0f,
         reductionNow < -0.05f ? colours::danger : colours::textDim);

    row ("Band", format::decibels (processor.getDetectorDb()), 12.0f, colours::text);
    row ("Threshold", format::decibels (processor.getEffectiveThresholdDb()), 12.0f, colours::warning);

    // What the detector is actually watching, which is now two numbers rather
    // than one: without seeing the top it is easy to leave it open and wonder
    // why a cymbal is triggering the de-esser.
    const auto watched = format::frequency (settings.frequencyHz) + " - "
                       + (settings.detectorTopHz >= 19500.0f
                              ? juce::String ("top")
                              : format::frequency (settings.detectorTopHz));

    row ("Watching", watched, 11.0f, colours::textDim);

    if (settings.channel != Channel::stereo)
        row ("Channel", settings.channel == Channel::mid ? "Mid" : "Side", 11.0f, colours::accentBright());

    inner.removeFromTop (6.0f);

    g.setFont (fonts::regular (9.5f));
    g.setColour (colours::textFaint);
    g.drawFittedText (settings.detection == Detection::adaptive
                          ? "Adaptive: the knob sets how far above the band's own level the threshold sits."
                          : "Absolute: the threshold is a fixed level.",
                      inner.toNearestInt(), juce::Justification::topLeft, 4);
}

void EssDisplay::setThresholdFromY (float y)
{
    if (auto* parameter = processor.getState().getParameter (ids::threshold))
    {
        const auto level = juce::jlimit (-60.0f, 0.0f, yToLevel (y));

        /*  In adaptive mode the knob is a distance above the band's running
            level, so putting the line at a place on screen means asking for the
            distance between that place and the level. Computed from the
            reference rather than from the line's current position: otherwise two
            drags to the same place would give two different answers, because the
            second would be measured against the result of the first.
        */
        const auto wanted = processor.getSettings().detection == Detection::adaptive
                          ? level - processor.getAdaptiveReferenceDb()
                          : level;

        parameter->setValueNotifyingHost (parameter->convertTo0to1 (juce::jlimit (-60.0f, 0.0f, wanted)));
    }
}

void EssDisplay::mouseDown (const juce::MouseEvent& event)
{
    if (! historyArea.contains (event.position))
        return;

    dragging = true;

    if (auto* parameter = processor.getState().getParameter (ids::threshold))
        parameter->beginChangeGesture();

    setThresholdFromY (event.position.y);
}

void EssDisplay::mouseDrag (const juce::MouseEvent& event)
{
    if (dragging)
        setThresholdFromY (event.position.y);
}

void EssDisplay::mouseUp (const juce::MouseEvent&)
{
    if (! dragging)
        return;

    dragging = false;

    if (auto* parameter = processor.getState().getParameter (ids::threshold))
        parameter->endChangeGesture();
}
} // namespace nodo::ess
