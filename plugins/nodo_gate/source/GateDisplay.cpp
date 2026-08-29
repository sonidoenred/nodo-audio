#include "GateDisplay.h"
#include "GateEngine.h"

namespace nodo::gate
{
using namespace nodo::theme;

namespace
{
    constexpr float curveMinDb = -90.0f;
    constexpr float curveMaxDb = 0.0f;
    constexpr float reductionScaleDb = 60.0f;
}

GateDisplay::GateDisplay (NodoGateProcessor& processorToUse)
    : processor (processorToUse)
{
    levelHistory.fill (-400.0f);
    reductionHistory.fill (0.0f);
    openHistory.fill (false);

    setOpaque (false);
    startTimerHz (30);
}

GateDisplay::~GateDisplay()
{
    stopTimer();
}

void GateDisplay::timerCallback()
{
    /*  The detector's own reading, not the meter's. On a gate these are
        different questions: the meter says how loud the track is, the detector
        says what the gate is deciding on — which may be an external sidechain,
        and which is filtered.
    */
    levelHistory[(size_t) writePos] = processor.getDetectorLevelDb();
    reductionHistory[(size_t) writePos] = processor.getGainReductionDb();
    openHistory[(size_t) writePos] = processor.isGateOpen();

    writePos = (writePos + 1) % historyPoints;

    repaint();
}

void GateDisplay::resized()
{
    auto bounds = getLocalBounds().toFloat();

    // The curve is square: a transfer curve drawn on a rectangle makes the 1:1
    // line look like something other than 45 degrees, and the whole point of it
    // is reading slopes at a glance.
    const auto curveSize = juce::jlimit (140.0f, 260.0f, bounds.getHeight() - 24.0f);

    curveArea = bounds.removeFromRight (curveSize + 24.0f)
                      .withSizeKeepingCentre (curveSize, curveSize);
    historyArea = bounds.reduced (2.0f, 12.0f);
}

float GateDisplay::levelToY (float db) const
{
    const auto proportion = (topDb - db) / (topDb - bottomDb);
    return historyArea.getY() + proportion * historyArea.getHeight();
}

float GateDisplay::yToLevel (float y) const
{
    const auto proportion = (y - historyArea.getY()) / juce::jmax (1.0f, historyArea.getHeight());
    return topDb - proportion * (topDb - bottomDb);
}

void GateDisplay::drawGrid (juce::Graphics& g) const
{
    g.setFont (fonts::regular (9.5f));

    for (auto db : { 0.0f, -18.0f, -36.0f, -54.0f, -72.0f })
    {
        const auto y = levelToY (db);

        g.setColour (juce::approximatelyEqual (db, 0.0f) ? colours::gridStrong : colours::grid);
        g.drawHorizontalLine ((int) y, historyArea.getX(), historyArea.getRight());

        g.setColour (colours::textFaint);
        g.drawText (juce::String ((int) db), historyArea.getX() + 4.0f, y + 1.0f, 32.0f, 11.0f,
                    juce::Justification::topLeft, false);
    }
}

void GateDisplay::drawHistory (juce::Graphics& g) const
{
    const auto settings = const_cast<NodoGateProcessor&> (processor).getSettings();
    const auto step = historyArea.getWidth() / (float) (historyPoints - 1);

    // Detector level, as a filled shape. Faint on purpose: it is context for
    // the reduction, not the subject.
    juce::Path levelPath;
    levelPath.startNewSubPath (historyArea.getX(), historyArea.getBottom());

    for (int i = 0; i < historyPoints; ++i)
    {
        const auto index = (writePos + i) % historyPoints;
        const auto db = juce::jlimit (bottomDb, topDb, levelHistory[(size_t) index]);
        levelPath.lineTo (historyArea.getX() + (float) i * step, levelToY (db));
    }

    levelPath.lineTo (historyArea.getRight(), historyArea.getBottom());
    levelPath.closeSubPath();

    g.setColour (colours::accent().withAlpha (0.30f));
    g.fillPath (levelPath);

    /*  A strip along the bottom showing when the gate was actually open. The
        reduction curve already implies it, but only indirectly: with a short
        range the gain barely moves and you cannot see the decision being made.
        This is the decision.
    */
    const auto stripHeight = 4.0f;
    const auto stripY = historyArea.getBottom() - stripHeight;

    for (int i = 0; i < historyPoints; ++i)
    {
        const auto index = (writePos + i) % historyPoints;

        if (! openHistory[(size_t) index])
            continue;

        g.setColour (colours::accentBright().withAlpha (0.8f));
        g.fillRect (historyArea.getX() + (float) i * step, stripY, step + 0.7f, stripHeight);
    }

    // Reduction, hanging from the top.
    const auto reductionScale = historyArea.getHeight() / reductionScaleDb;

    juce::Path reductionPath;
    reductionPath.startNewSubPath (historyArea.getX(), historyArea.getY());

    for (int i = 0; i < historyPoints; ++i)
    {
        const auto index = (writePos + i) % historyPoints;
        const auto value = juce::jlimit (-reductionScaleDb, 0.0f, reductionHistory[(size_t) index]);
        reductionPath.lineTo (historyArea.getX() + (float) i * step,
                              historyArea.getY() - value * reductionScale);
    }

    reductionPath.lineTo (historyArea.getRight(), historyArea.getY());
    reductionPath.closeSubPath();

    g.setColour (colours::danger.withAlpha (0.20f));
    g.fillPath (reductionPath);
    g.setColour (colours::danger.withAlpha (0.7f));
    g.strokePath (reductionPath, juce::PathStrokeType (1.0f));

    g.setColour (colours::textFaint);
    g.setFont (fonts::bold (8.5f));
    g.drawText ("GR", historyArea.getRight() - 24.0f, historyArea.getY() + 2.0f, 22.0f, 10.0f,
                juce::Justification::centredRight, false);

    /*  Two lines, not one: the level the gate opens at, and the lower level it
        has to fall back to before it will close again. The band between them is
        the hysteresis, and shading it is the quickest way to explain a control
        most people have never knowingly used.
    */
    const auto openY = levelToY (settings.thresholdDb);
    const auto closeY = levelToY (settings.thresholdDb - settings.hysteresisDb);

    if (settings.hysteresisDb > 0.05f)
    {
        g.setColour (colours::warning.withAlpha (0.10f));
        g.fillRect (historyArea.getX(), openY, historyArea.getWidth(), closeY - openY);
    }

    g.setColour (hoveringThreshold || draggingThreshold ? colours::warning
                                                        : colours::warning.withAlpha (0.55f));
    const float dashes[] { 4.0f, 3.0f };
    g.drawDashedLine ({ historyArea.getX(), openY, historyArea.getRight(), openY },
                      dashes, 2, 1.0f);

    if (settings.hysteresisDb > 0.05f)
    {
        g.setColour (colours::warning.withAlpha (0.30f));
        g.drawDashedLine ({ historyArea.getX(), closeY, historyArea.getRight(), closeY },
                          dashes, 2, 1.0f);
    }

    g.setColour (hoveringThreshold || draggingThreshold ? colours::warning
                                                        : colours::warning.withAlpha (0.7f));
    g.setFont (fonts::mono (10.0f));
    g.drawText (format::decibels (settings.thresholdDb),
                historyArea.getRight() - 62.0f, openY - 12.0f, 58.0f, 11.0f,
                juce::Justification::centredRight, false);
}

void GateDisplay::drawCurve (juce::Graphics& g) const
{
    auto settings = const_cast<NodoGateProcessor&> (processor).getSettings();

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

    g.setColour (colours::grid);

    for (auto db = -72.0f; db < curveMaxDb; db += 18.0f)
    {
        g.drawVerticalLine ((int) toX (db), curveArea.getY(), curveArea.getBottom());
        g.drawHorizontalLine ((int) toY (db), curveArea.getX(), curveArea.getRight());
    }

    g.setColour (colours::gridStrong);
    g.drawLine (toX (curveMinDb), toY (curveMinDb), toX (curveMaxDb), toY (curveMaxDb), 1.0f);

    juce::Path path;
    const auto steps = 160;

    for (int i = 0; i <= steps; ++i)
    {
        const auto inputDb = curveMinDb + (curveMaxDb - curveMinDb) * (float) i / (float) steps;
        const auto outputDb = GateEngine::staticOutputDb (inputDb, settings);
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
        const auto outputDb = GateEngine::staticOutputDb (detectorDb, settings);
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

void GateDisplay::paint (juce::Graphics& g)
{
    g.setColour (colours::background);
    g.fillRect (getLocalBounds());

    drawGrid (g);
    drawHistory (g);
    drawCurve (g);
}

void GateDisplay::setThresholdFromY (float y)
{
    if (auto* parameter = processor.getState().getParameter (ids::threshold))
    {
        const auto db = juce::jlimit (-90.0f, 0.0f, yToLevel (y));
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (db));
    }
}

void GateDisplay::mouseDown (const juce::MouseEvent& event)
{
    if (! historyArea.contains (event.position))
        return;

    draggingThreshold = true;

    if (auto* parameter = processor.getState().getParameter (ids::threshold))
        parameter->beginChangeGesture();

    setThresholdFromY (event.position.y);
}

void GateDisplay::mouseDrag (const juce::MouseEvent& event)
{
    if (draggingThreshold)
        setThresholdFromY (event.position.y);
}

void GateDisplay::mouseUp (const juce::MouseEvent&)
{
    if (! draggingThreshold)
        return;

    draggingThreshold = false;

    if (auto* parameter = processor.getState().getParameter (ids::threshold))
        parameter->endChangeGesture();
}

void GateDisplay::mouseMove (const juce::MouseEvent& event)
{
    const auto settings = processor.getSettings();
    const auto near = std::abs (event.position.y - levelToY (settings.thresholdDb)) < 6.0f
                   && historyArea.contains (event.position);

    if (near != hoveringThreshold)
    {
        hoveringThreshold = near;
        repaint();
    }
}

void GateDisplay::mouseExit (const juce::MouseEvent&)
{
    if (! hoveringThreshold)
        return;

    hoveringThreshold = false;
    repaint();
}
} // namespace nodo::gate
