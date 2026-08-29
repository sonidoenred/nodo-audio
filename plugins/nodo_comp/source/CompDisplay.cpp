#include "CompDisplay.h"

namespace nodo::comp
{
using namespace nodo::theme;

namespace
{
    constexpr float curveMinDb = -60.0f;
    constexpr float curveMaxDb = 6.0f;

    float levelDbFromMagnitude (float magnitude)
    {
        return 20.0f * std::log10 (juce::jmax (1.0e-6f, magnitude));
    }
}

CompDisplay::CompDisplay (NodoCompProcessor& processorToUse)
    : processor (processorToUse)
{
    levelHistory.fill (-400.0f);
    reductionHistory.fill (0.0f);

    setOpaque (false);
    startTimerHz (30);
}

CompDisplay::~CompDisplay()
{
    stopTimer();
}

void CompDisplay::timerCallback()
{
    const auto level = juce::jmax (processor.getMeterLevel (0), processor.getMeterLevel (1));

    levelHistory[(size_t) writePos] = levelDbFromMagnitude (level);
    reductionHistory[(size_t) writePos] = processor.getGainReductionDb();

    writePos = (writePos + 1) % historyPoints;

    repaint();
}

void CompDisplay::resized()
{
    auto bounds = getLocalBounds().toFloat();

    // The curve is square: a transfer curve drawn on a rectangle makes a 1:1
    // line look like something other than 45 degrees, and the whole point of it
    // is reading slopes at a glance.
    const auto curveSize = juce::jlimit (140.0f, 260.0f, bounds.getHeight() - 24.0f);

    curveArea = bounds.removeFromRight (curveSize + 24.0f)
                      .withSizeKeepingCentre (curveSize, curveSize);
    historyArea = bounds.reduced (2.0f, 12.0f);
}

float CompDisplay::levelToY (float db) const
{
    const auto proportion = (topDb - db) / (topDb - bottomDb);
    return historyArea.getY() + proportion * historyArea.getHeight();
}

float CompDisplay::yToLevel (float y) const
{
    const auto proportion = (y - historyArea.getY()) / juce::jmax (1.0f, historyArea.getHeight());
    return topDb - proportion * (topDb - bottomDb);
}

void CompDisplay::drawGrid (juce::Graphics& g) const
{
    g.setFont (fonts::regular (9.5f));

    for (auto db : { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f })
    {
        const auto y = levelToY (db);

        g.setColour (juce::approximatelyEqual (db, 0.0f) ? colours::gridStrong : colours::grid);
        g.drawHorizontalLine ((int) y, historyArea.getX(), historyArea.getRight());

        g.setColour (colours::textFaint);
        g.drawText (juce::String ((int) db), historyArea.getX() + 4.0f, y + 1.0f, 30.0f, 11.0f,
                    juce::Justification::topLeft, false);
    }
}

void CompDisplay::drawHistory (juce::Graphics& g) const
{
    const auto settings = const_cast<NodoCompProcessor&> (processor).getSettings();
    const auto width = historyArea.getWidth();
    const auto step = width / (float) (historyPoints - 1);

    // Input level, as a filled shape. Faint on purpose: it is context for the
    // reduction, not the subject.
    juce::Path levelPath;
    levelPath.startNewSubPath (historyArea.getX(), historyArea.getBottom());

    for (int i = 0; i < historyPoints; ++i)
    {
        const auto index = (writePos + i) % historyPoints;
        const auto db = levelHistory[(size_t) index];
        const auto x = historyArea.getX() + (float) i * step;

        levelPath.lineTo (x, levelToY (juce::jlimit (bottomDb, topDb, db)));
    }

    levelPath.lineTo (historyArea.getRight(), historyArea.getBottom());
    levelPath.closeSubPath();

    g.setColour (colours::analyserFill.withAlpha (0.85f));
    g.fillPath (levelPath);

    // Gain reduction, hanging from the top. Down means quieter, which is the
    // direction the gain is actually moving.
    juce::Path reductionPath;
    reductionPath.startNewSubPath (historyArea.getX(), historyArea.getY());

    const auto reductionScale = historyArea.getHeight() / 24.0f;   // 24 dB fills the display

    for (int i = 0; i < historyPoints; ++i)
    {
        const auto index = (writePos + i) % historyPoints;
        const auto reduction = juce::jlimit (-24.0f, 0.0f, reductionHistory[(size_t) index]);
        const auto x = historyArea.getX() + (float) i * step;

        reductionPath.lineTo (x, historyArea.getY() - reduction * reductionScale);
    }

    reductionPath.lineTo (historyArea.getRight(), historyArea.getY());
    reductionPath.closeSubPath();

    g.setColour (colours::accent().withAlpha (0.30f));
    g.fillPath (reductionPath);

    // Dimmer than it wants to be: at rest this path is a straight line across
    // the top, and a bright line that means "nothing is happening" is the
    // brightest thing on an idle screen.
    g.setColour (colours::accentBright().withAlpha (0.55f));
    g.strokePath (reductionPath, juce::PathStrokeType (1.0f));

    // Scale for the reduction, so the shape hanging from the top is readable as
    // a number of dB rather than as a shape.
    g.setFont (fonts::regular (9.0f));

    for (auto reduction : { -6.0f, -12.0f, -18.0f })
    {
        const auto y = historyArea.getY() - reduction * reductionScale;

        g.setColour (colours::grid.withAlpha (0.6f));
        g.drawHorizontalLine ((int) y, historyArea.getRight() - 26.0f, historyArea.getRight());

        g.setColour (colours::textFaint);
        g.drawText (juce::String ((int) reduction),
                    historyArea.getRight() - 60.0f, y - 5.0f, 30.0f, 11.0f,
                    juce::Justification::centredRight, false);
    }

    g.setColour (colours::textFaint);
    g.setFont (fonts::bold (8.5f));
    g.drawText ("GR", historyArea.getRight() - 24.0f, historyArea.getY() + 2.0f, 22.0f, 10.0f,
                juce::Justification::centredRight, false);

    // Threshold line, draggable.
    const auto thresholdY = levelToY (settings.thresholdDb);

    g.setColour (hoveringThreshold || draggingThreshold ? colours::warning
                                                        : colours::warning.withAlpha (0.55f));
    const float dashes[] { 4.0f, 3.0f };
    g.drawDashedLine ({ historyArea.getX(), thresholdY, historyArea.getRight(), thresholdY },
                      dashes, 2, 1.0f);

    g.setFont (fonts::mono (10.0f));
    g.drawText (format::decibels (settings.thresholdDb),
                historyArea.getRight() - 62.0f, thresholdY - 12.0f, 58.0f, 11.0f,
                juce::Justification::centredRight, false);
}

void CompDisplay::drawCurve (juce::Graphics& g) const
{
    auto settings = const_cast<NodoCompProcessor&> (processor).getSettings();

    g.setColour (colours::panel);
    g.fillRoundedRectangle (curveArea, metrics::cornerRadius);

    auto toX = [this] (float db)
    {
        return curveArea.getX() + (db - curveMinDb) / (curveMaxDb - curveMinDb) * curveArea.getWidth();
    };

    auto toY = [this] (float db)
    {
        return curveArea.getBottom() - (db - curveMinDb) / (curveMaxDb - curveMinDb) * curveArea.getHeight();
    };

    // Reference grid every 12 dB, plus the 1:1 diagonal.
    g.setColour (colours::grid);

    for (auto db = -48.0f; db < curveMaxDb; db += 12.0f)
    {
        g.drawVerticalLine ((int) toX (db), curveArea.getY(), curveArea.getBottom());
        g.drawHorizontalLine ((int) toY (db), curveArea.getX(), curveArea.getRight());
    }

    g.setColour (colours::gridStrong);
    g.drawLine (toX (curveMinDb), toY (curveMinDb), toX (curveMaxDb), toY (curveMaxDb), 1.0f);

    // The curve itself, including makeup and mix: what the plugin will actually
    // do, not what the ratio alone suggests.
    juce::Path path;
    const auto steps = 120;

    for (int i = 0; i <= steps; ++i)
    {
        const auto inputDb = curveMinDb + (curveMaxDb - curveMinDb) * (float) i / (float) steps;
        const auto outputDb = CompEngine::staticOutputDb (inputDb, settings);
        const auto x = toX (inputDb);
        const auto y = toY (juce::jlimit (curveMinDb, curveMaxDb, outputDb));

        if (i == 0)
            path.startNewSubPath (x, y);
        else
            path.lineTo (x, y);
    }

    g.setColour (colours::curve);
    g.strokePath (path, juce::PathStrokeType (1.8f));

    // Where the signal is sitting right now.
    const auto detectorDb = processor.getDetectorLevelDb();

    if (detectorDb > curveMinDb)
    {
        const auto outputDb = CompEngine::staticOutputDb (detectorDb, settings);
        const auto x = toX (juce::jlimit (curveMinDb, curveMaxDb, detectorDb));
        const auto y = toY (juce::jlimit (curveMinDb, curveMaxDb, outputDb));

        g.setColour (colours::accentBright());
        g.fillEllipse (x - 3.5f, y - 3.5f, 7.0f, 7.0f);
    }

    g.setColour (colours::textFaint);
    g.setFont (fonts::regular (9.5f));
    g.drawText ("IN", curveArea.getRight() - 24.0f, curveArea.getBottom() - 13.0f, 20.0f, 11.0f,
                juce::Justification::centredRight, false);
    g.drawText ("OUT", curveArea.getX() + 3.0f, curveArea.getY() + 2.0f, 28.0f, 11.0f,
                juce::Justification::centredLeft, false);

    g.setColour (colours::grid);
    g.drawRoundedRectangle (curveArea, metrics::cornerRadius, 1.0f);
}

void CompDisplay::paint (juce::Graphics& g)
{
    g.setColour (colours::background);
    g.fillRect (getLocalBounds());

    drawGrid (g);
    drawHistory (g);
    drawCurve (g);
}

void CompDisplay::setThresholdFromY (float y)
{
    if (auto* parameter = processor.getState().getParameter (ids::threshold))
    {
        const auto db = juce::jlimit (-60.0f, 0.0f, yToLevel (y));
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (db));
    }
}

void CompDisplay::mouseDown (const juce::MouseEvent& event)
{
    if (! historyArea.contains (event.position))
        return;

    draggingThreshold = true;

    if (auto* parameter = processor.getState().getParameter (ids::threshold))
        parameter->beginChangeGesture();

    setThresholdFromY (event.position.y);
}

void CompDisplay::mouseDrag (const juce::MouseEvent& event)
{
    if (draggingThreshold)
        setThresholdFromY (event.position.y);
}

void CompDisplay::mouseUp (const juce::MouseEvent&)
{
    if (! draggingThreshold)
        return;

    draggingThreshold = false;

    if (auto* parameter = processor.getState().getParameter (ids::threshold))
        parameter->endChangeGesture();
}

void CompDisplay::mouseMove (const juce::MouseEvent& event)
{
    const auto thresholdY = levelToY (processor.getSettings().thresholdDb);
    const auto near = std::abs (event.position.y - thresholdY) < 6.0f
                   && historyArea.contains (event.position);

    if (near != hoveringThreshold)
    {
        hoveringThreshold = near;
        setMouseCursor (near ? juce::MouseCursor::UpDownResizeCursor
                             : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void CompDisplay::mouseExit (const juce::MouseEvent&)
{
    if (hoveringThreshold)
    {
        hoveringThreshold = false;
        repaint();
    }
}
} // namespace nodo::comp
