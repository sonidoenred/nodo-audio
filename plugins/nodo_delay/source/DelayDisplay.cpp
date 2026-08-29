#include "DelayDisplay.h"

namespace nodo::delay
{
using namespace nodo::theme;

namespace
{
    constexpr float minHz = 20.0f;
    constexpr float maxHz = 20000.0f;

    float frequencyToProportion (float hz)
    {
        return std::log (juce::jmax (1.0f, hz) / minHz) / std::log (maxHz / minHz);
    }

    float responseDb (const DelaySettings& settings, float hz, int generations)
    {
        auto db = 0.0f;

        if (settings.highpassHz > 20.5f)
        {
            const auto c = dsp::BiquadCoefficients::highPass (96000.0, settings.highpassHz, 0.707);
            db += 20.0f * std::log10 ((float) juce::jmax (1.0e-6, c.magnitudeAt (hz, 96000.0)));
        }

        if (settings.lowpassHz < 19500.0f)
        {
            const auto c = dsp::BiquadCoefficients::lowPass (96000.0, settings.lowpassHz, 0.707);
            db += 20.0f * std::log10 ((float) juce::jmax (1.0e-6, c.magnitudeAt (hz, 96000.0)));
        }

        return db * (float) generations;
    }
}

DelayDisplay::DelayDisplay (NodoDelayProcessor& processorToUse)
    : processor (processorToUse)
{
    setOpaque (false);
    startTimerHz (20);
}

DelayDisplay::~DelayDisplay()
{
    stopTimer();
}

void DelayDisplay::timerCallback()
{
    const auto settings = processor.getSettings();

    /*  The window follows the pattern rather than being fixed. A 60 ms slapback
        and a four second wash are both delays, and a fixed axis would draw one
        of them as a single bar at the far left.
    */
    const auto longest = juce::jmax (resolvedTimeMs (settings, 0), resolvedTimeMs (settings, 1));
    const auto wanted = juce::jlimit (200.0f, 5000.0f, longest * 4.5f);

    // Eased rather than snapped, so dragging the time knob does not make the
    // whole picture jump about while you are trying to read it.
    horizonMs += (wanted - horizonMs) * 0.25f;

    taps = buildTapPattern (settings, horizonMs, 96);

    // The step lane only takes room when there is a pattern to show, so a plain
    // delay gets the whole picture to itself.
    if (settings.multiTap != showingSteps)
    {
        showingSteps = settings.multiTap;
        resized();
    }

    repaint();
}

void DelayDisplay::resized()
{
    auto bounds = getLocalBounds().toFloat();

    const auto filterWidth = juce::jlimit (170.0f, 300.0f, bounds.getWidth() * 0.3f);

    filterArea = bounds.removeFromRight (filterWidth + 20.0f).reduced (10.0f, 12.0f);

    auto left = bounds.reduced (2.0f, 12.0f);

    /*  The step editor gets its own lane rather than being drawn on top of the
        repeats. They are two different x axes — one pattern against several
        seconds of decay — and putting them in the same rectangle would mean
        one of the two was lying about where things are.
    */
    if (showingSteps)
        stepArea = left.removeFromBottom (left.getHeight() * 0.36f).withTrimmedTop (8.0f);
    else
        stepArea = {};

    patternArea = left;
}

int DelayDisplay::stepAt (juce::Point<float> position) const
{
    const auto settings = const_cast<NodoDelayProcessor&> (processor).getSettings();

    if (! settings.multiTap || ! stepArea.contains (position))
        return -1;

    const auto steps = activeSteps (settings);
    const auto column = (int) ((position.x - stepArea.getX()) / stepArea.getWidth() * (float) steps);

    return juce::jlimit (0, steps - 1, column);
}

void DelayDisplay::setStepFromPoint (int step, juce::Point<float> position, bool startGesture)
{
    auto& state = processor.getState();

    auto* levelParameter = state.getParameter (ids::tapLevel (step + 1));
    auto* panParameter = state.getParameter (ids::tapPan (step + 1));

    if (levelParameter == nullptr || panParameter == nullptr)
        return;

    if (startGesture)
    {
        levelParameter->beginChangeGesture();
        panParameter->beginChangeGesture();
        gestureOpen = true;
    }

    const auto settings = processor.getSettings();
    const auto steps = activeSteps (settings);
    const auto columnWidth = stepArea.getWidth() / (float) steps;
    const auto columnLeft = stepArea.getX() + columnWidth * (float) step;

    /*  One gesture sets both: how far up you are is the level, how far across
        the column you are is the pan. A bar leaning left is panned left, which
        is a thing you can read without a legend.
    */
    const auto level = juce::jlimit (0.0f, 1.0f,
                                     1.0f - (position.y - stepArea.getY())
                                         / juce::jmax (1.0f, stepArea.getHeight()));

    const auto pan = juce::jlimit (-1.0f, 1.0f,
                                   (position.x - columnLeft) / juce::jmax (1.0f, columnWidth) * 2.0f - 1.0f);

    levelParameter->setValueNotifyingHost (levelParameter->convertTo0to1 (level));
    panParameter->setValueNotifyingHost (panParameter->convertTo0to1 (pan));
}

void DelayDisplay::endStepGesture()
{
    if (! gestureOpen || draggingStep < 0)
    {
        gestureOpen = false;
        draggingStep = -1;
        return;
    }

    auto& state = processor.getState();

    if (auto* levelParameter = state.getParameter (ids::tapLevel (draggingStep + 1)))
        levelParameter->endChangeGesture();

    if (auto* panParameter = state.getParameter (ids::tapPan (draggingStep + 1)))
        panParameter->endChangeGesture();

    gestureOpen = false;
    draggingStep = -1;
}

void DelayDisplay::mouseDown (const juce::MouseEvent& event)
{
    const auto step = stepAt (event.position);

    if (step < 0)
        return;

    auto& state = processor.getState();
    auto* onParameter = state.getParameter (ids::tapOn (step + 1));

    if (onParameter == nullptr)
        return;

    const auto wasOn = onParameter->getValue() > 0.5f;

    // A modifier-click removes a step. Dragging it to zero would do the same
    // thing to the sound, but the pattern would then be a row of empty columns
    // you cannot tell from switched-off ones.
    if (event.mods.isRightButtonDown() || event.mods.isAltDown())
    {
        onParameter->beginChangeGesture();
        onParameter->setValueNotifyingHost (0.0f);
        onParameter->endChangeGesture();
        return;
    }

    if (! wasOn)
    {
        onParameter->beginChangeGesture();
        onParameter->setValueNotifyingHost (1.0f);
        onParameter->endChangeGesture();
    }

    draggingStep = step;
    setStepFromPoint (step, event.position, true);
}

void DelayDisplay::mouseDrag (const juce::MouseEvent& event)
{
    if (draggingStep >= 0)
        setStepFromPoint (draggingStep, event.position, false);
}

void DelayDisplay::mouseUp (const juce::MouseEvent&)
{
    endStepGesture();
}

void DelayDisplay::mouseMove (const juce::MouseEvent& event)
{
    const auto step = stepAt (event.position);

    if (step != hoveredStep)
    {
        hoveredStep = step;
        repaint();
    }
}

void DelayDisplay::mouseExit (const juce::MouseEvent&)
{
    if (hoveredStep < 0)
        return;

    hoveredStep = -1;
    repaint();
}

void DelayDisplay::drawSteps (juce::Graphics& g) const
{
    const auto settings = const_cast<NodoDelayProcessor&> (processor).getSettings();

    if (! settings.multiTap)
        return;

    const auto steps = activeSteps (settings);
    const auto columnWidth = stepArea.getWidth() / (float) steps;

    g.setColour (colours::panel);
    g.fillRoundedRectangle (stepArea, metrics::cornerRadius);

    for (int step = 0; step < steps; ++step)
    {
        const auto& tap = settings.taps[(size_t) step];
        auto column = juce::Rectangle<float> (stepArea.getX() + columnWidth * (float) step,
                                              stepArea.getY(), columnWidth, stepArea.getHeight());

        // Every fourth column a shade lighter, so a sixteen step pattern can be
        // counted in beats without a ruler.
        if (step % 4 == 0 && step > 0)
        {
            g.setColour (colours::grid);
            g.drawVerticalLine ((int) column.getX(), column.getY(), column.getBottom());
        }

        if (step == hoveredStep || step == draggingStep)
        {
            g.setColour (colours::panelRaised.withAlpha (0.6f));
            g.fillRect (column.reduced (1.0f, 0.0f));
        }

        if (! tap.on)
            continue;

        const auto inner = column.reduced (juce::jmax (1.5f, columnWidth * 0.12f), 3.0f)
                                 .withTrimmedTop (11.0f);
        const auto height = juce::jmax (2.0f, tap.level * inner.getHeight());

        // Where the bar sits across its column is the pan: the picture is the
        // stereo field, so a tap on the left is drawn on the left.
        const auto spread = juce::jlimit (0.0f, 1.5f, settings.tapSpread);
        const auto pan = juce::jlimit (-1.0f, 1.0f, tap.pan * spread);
        // Capped rather than a fraction of the column: with four steps a
        // proportional bar is eighty pixels wide and its lean — which is the
        // pan, and the whole point of drawing it this way — stops reading.
        const auto barWidth = juce::jlimit (3.0f, 22.0f, inner.getWidth() * 0.45f);
        const auto centre = inner.getCentreX() + pan * (inner.getWidth() - barWidth) * 0.5f;

        g.setColour (colours::accent().withAlpha (0.9f));
        g.fillRoundedRectangle (centre - barWidth * 0.5f, inner.getBottom() - height,
                                barWidth, height, 1.5f);
    }

    g.setColour (colours::textFaint);
    g.setFont (fonts::bold (8.0f));
    g.drawText ("PATTERN", stepArea.getX() + 5.0f, stepArea.getY() + 2.0f, 60.0f, 10.0f,
                juce::Justification::centredLeft, false);

    g.setColour (colours::grid);
    g.drawRoundedRectangle (stepArea, metrics::cornerRadius, 1.0f);
}

void DelayDisplay::drawPattern (juce::Graphics& g) const
{
    const auto settings = const_cast<NodoDelayProcessor&> (processor).getSettings();
    const auto centre = patternArea.getCentreY();
    const auto halfHeight = patternArea.getHeight() * 0.5f - 12.0f;

    auto toX = [this] (float ms)
    {
        return patternArea.getX() + juce::jlimit (0.0f, 1.0f, ms / juce::jmax (1.0f, horizonMs))
                                        * patternArea.getWidth();
    };

    // Time grid: a line every whole number of the shorter delay, so the pattern
    // can be read against its own pulse rather than against arbitrary
    // milliseconds.
    const auto pulse = juce::jmax (1.0f, juce::jmin (resolvedTimeMs (settings, 0),
                                                     resolvedTimeMs (settings, 1)));

    g.setColour (colours::grid);

    for (auto t = pulse; t < horizonMs; t += pulse)
        g.drawVerticalLine ((int) toX (t), patternArea.getY(), patternArea.getBottom());

    g.setColour (colours::gridStrong);
    g.drawHorizontalLine ((int) centre, patternArea.getX(), patternArea.getRight());

    // Every repeat. Up is the left of the picture, down is the right, which
    // means a ping-pong reads as a zigzag without having to be told.
    for (const auto& tap : taps)
    {
        const auto x = toX (tap.timeMs);

        auto bar = [&] (float amplitude, bool upwards)
        {
            const auto magnitude = juce::jlimit (0.0f, 1.0f, std::abs (amplitude));

            if (magnitude < 0.002f)
                return;

            // Cube root rather than dB: a logarithmic axis would give the
            // quietest repeats visual weight they do not have, and a linear one
            // hides everything past the third.
            const auto height = std::cbrt (magnitude) * halfHeight;

            g.setColour (colours::accent().withAlpha (juce::jlimit (0.25f, 1.0f, 0.35f + magnitude)));
            g.fillRect (x - 1.0f, upwards ? centre - height : centre, 2.0f, height);
        };

        bar (tap.left, true);
        bar (tap.right, false);
    }

    g.setColour (colours::textFaint);
    g.setFont (fonts::regular (9.5f));
    g.drawText ("L", patternArea.getX() + 3.0f, patternArea.getY(), 14.0f, 12.0f,
                juce::Justification::centredLeft, false);
    g.drawText ("R", patternArea.getX() + 3.0f, patternArea.getBottom() - 12.0f, 14.0f, 12.0f,
                juce::Justification::centredLeft, false);
    g.drawText (juce::String (juce::roundToInt (horizonMs)) + " ms",
                patternArea.getRight() - 60.0f, patternArea.getBottom() - 12.0f, 58.0f, 12.0f,
                juce::Justification::centredRight, false);

    // What the two lines are actually running at, which is not the knob when
    // the delay is synced.
    g.setFont (fonts::mono (10.0f));
    g.setColour (colours::accentBright());

    for (int line = 0; line < 2; ++line)
    {
        const auto ms = resolvedTimeMs (settings, line);
        g.drawText (juce::String (ms < 100.0f ? juce::String (ms, 1) : juce::String (juce::roundToInt (ms)))
                        + " ms",
                    patternArea.getRight() - 130.0f,
                    patternArea.getY() + 2.0f + (float) line * 13.0f, 62.0f, 12.0f,
                    juce::Justification::centredRight, false);
    }
}

void DelayDisplay::drawFilters (juce::Graphics& g) const
{
    const auto settings = const_cast<NodoDelayProcessor&> (processor).getSettings();

    g.setColour (colours::panel);
    g.fillRoundedRectangle (filterArea, metrics::cornerRadius);

    constexpr float topDb = 6.0f;
    constexpr float bottomDb = -42.0f;

    auto toX = [this] (float hz)
    {
        return filterArea.getX() + frequencyToProportion (hz) * filterArea.getWidth();
    };

    auto toY = [this] (float db)
    {
        return filterArea.getY() + (topDb - juce::jlimit (bottomDb, topDb, db))
                                       / (topDb - bottomDb) * filterArea.getHeight();
    };

    g.setColour (colours::grid);

    for (auto hz : { 100.0f, 1000.0f, 10000.0f })
        g.drawVerticalLine ((int) toX (hz), filterArea.getY(), filterArea.getBottom());

    g.setColour (colours::gridStrong);
    g.drawHorizontalLine ((int) toY (0.0f), filterArea.getX(), filterArea.getRight());

    /*  Once per generation. The point the picture is making is that the loop's
        tone compounds: a gentle low pass that looks like nothing on the first
        repeat has taken the top off entirely by the fifth, and that is the
        thing you cannot see on a single curve.
    */
    for (int generation = 5; generation >= 1; --generation)
    {
        juce::Path path;
        const auto steps = 90;

        for (int i = 0; i <= steps; ++i)
        {
            const auto proportion = (float) i / (float) steps;
            const auto hz = minHz * std::pow (maxHz / minHz, proportion);
            const auto x = filterArea.getX() + proportion * filterArea.getWidth();
            const auto y = toY (responseDb (settings, hz, generation));

            if (i == 0)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
        }

        const auto alpha = generation == 1 ? 1.0f : 0.5f / (float) generation;
        g.setColour (colours::curve.withAlpha (alpha));
        g.strokePath (path, juce::PathStrokeType (generation == 1 ? 1.8f : 1.0f));
    }

    g.setColour (colours::textFaint);
    g.setFont (fonts::bold (8.5f));
    g.drawText ("LOOP", filterArea.getX() + 6.0f, filterArea.getY() + 3.0f, 60.0f, 11.0f,
                juce::Justification::centredLeft, false);

    g.setFont (fonts::regular (9.0f));

    for (auto hz : { 100.0f, 1000.0f, 10000.0f })
        g.drawText (hz >= 1000.0f ? juce::String (juce::roundToInt (hz / 1000.0f)) + "k"
                                  : juce::String (juce::roundToInt (hz)),
                    toX (hz) - 14.0f, filterArea.getBottom() - 12.0f, 28.0f, 11.0f,
                    juce::Justification::centred, false);

    g.setColour (colours::grid);
    g.drawRoundedRectangle (filterArea, metrics::cornerRadius, 1.0f);
}

void DelayDisplay::paint (juce::Graphics& g)
{
    g.setColour (colours::background);
    g.fillRect (getLocalBounds());

    drawPattern (g);
    drawSteps (g);
    drawFilters (g);
}
} // namespace nodo::delay
