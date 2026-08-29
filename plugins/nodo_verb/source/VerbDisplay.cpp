#include "VerbDisplay.h"

namespace nodo::verb
{
using namespace nodo::theme;

namespace
{
    constexpr float minHz = 20.0f;
    constexpr float maxHz = 20000.0f;

    constexpr float minSeconds = 0.05f;
    constexpr float maxSeconds = 30.0f;

    constexpr float maxGainDb = 18.0f;

    float postEqDb (const VerbSettings& s, float hz, double sampleRate)
    {
        auto db = 0.0f;

        const auto add = [&db, hz, sampleRate] (const dsp::BiquadCoefficients& c)
        {
            db += 20.0f * std::log10 ((float) juce::jmax (1.0e-6, c.magnitudeAt (hz, sampleRate)));
        };

        add (dsp::BiquadCoefficients::lowShelf (sampleRate, s.postLowFreq, 0.707, s.postLowGain));
        add (dsp::BiquadCoefficients::peak (sampleRate, s.postMidFreq,
                                            juce::jmax (0.1f, s.postMidQ), s.postMidGain));
        add (dsp::BiquadCoefficients::highShelf (sampleRate, s.postHighFreq, 0.707, s.postHighGain));

        return db;
    }
}

VerbDisplay::VerbDisplay (NodoVerbProcessor& processorToUse)
    : processor (processorToUse)
{
    setOpaque (false);
    startTimerHz (20);
}

VerbDisplay::~VerbDisplay()
{
    stopTimer();
}

void VerbDisplay::timerCallback()
{
    repaint();
}

void VerbDisplay::resized()
{
    // Wider gutter on the right than on the left: "+12 dB" is a longer word than
    // "0.1s", and a label that runs off the edge of the component is no label.
    plotArea = getLocalBounds().toFloat().reduced (0.0f, 14.0f)
                   .withTrimmedLeft (34.0f)
                   .withTrimmedRight (46.0f)
                   .withTrimmedBottom (6.0f);
}

float VerbDisplay::frequencyToX (float hz) const
{
    const auto proportion = std::log (juce::jlimit (minHz, maxHz, hz) / minHz) / std::log (maxHz / minHz);
    return plotArea.getX() + proportion * plotArea.getWidth();
}

float VerbDisplay::xToFrequency (float x) const
{
    const auto proportion = juce::jlimit (0.0f, 1.0f,
                                          (x - plotArea.getX()) / juce::jmax (1.0f, plotArea.getWidth()));
    return minHz * std::pow (maxHz / minHz, proportion);
}

float VerbDisplay::secondsToY (float seconds) const
{
    const auto proportion = std::log (juce::jlimit (minSeconds, maxSeconds, seconds) / minSeconds)
                          / std::log (maxSeconds / minSeconds);
    return plotArea.getBottom() - proportion * plotArea.getHeight();
}

float VerbDisplay::yToSeconds (float y) const
{
    const auto proportion = juce::jlimit (0.0f, 1.0f,
                                          (plotArea.getBottom() - y) / juce::jmax (1.0f, plotArea.getHeight()));
    return minSeconds * std::pow (maxSeconds / minSeconds, proportion);
}

float VerbDisplay::gainToY (float db) const
{
    const auto proportion = (juce::jlimit (-maxGainDb, maxGainDb, db) + maxGainDb) / (2.0f * maxGainDb);
    return plotArea.getBottom() - proportion * plotArea.getHeight();
}

float VerbDisplay::yToGain (float y) const
{
    const auto proportion = juce::jlimit (0.0f, 1.0f,
                                          (plotArea.getBottom() - y) / juce::jmax (1.0f, plotArea.getHeight()));
    return proportion * 2.0f * maxGainDb - maxGainDb;
}

void VerbDisplay::drawGrid (juce::Graphics& g) const
{
    g.setFont (fonts::regular (9.0f));

    for (auto hz : { 50.0f, 100.0f, 500.0f, 1000.0f, 5000.0f, 10000.0f })
    {
        const auto x = frequencyToX (hz);

        g.setColour (colours::grid);
        g.drawVerticalLine ((int) x, plotArea.getY(), plotArea.getBottom());

        g.setColour (colours::textFaint);
        g.drawText (hz >= 1000.0f ? juce::String (juce::roundToInt (hz / 1000.0f)) + "k"
                                  : juce::String (juce::roundToInt (hz)),
                    x - 18.0f, plotArea.getBottom() + 1.0f, 36.0f, 11.0f,
                    juce::Justification::centred, false);
    }

    // Decay times down the left, in seconds, because that is the quantity.
    for (auto seconds : { 0.1f, 0.5f, 1.0f, 5.0f, 10.0f })
    {
        const auto y = secondsToY (seconds);

        g.setColour (colours::grid);
        g.drawHorizontalLine ((int) y, plotArea.getX(), plotArea.getRight());

        /*  Every label carries the unit. "500" on the left of a picture whose
            bottom edge is labelled in hertz reads as 500 Hz, which is exactly
            the wrong thing to think about a vertical axis.
        */
        g.setColour (colours::textFaint);
        g.drawText (seconds < 1.0f ? juce::String (seconds, 1) + "s"
                                   : juce::String (juce::roundToInt (seconds)) + "s",
                    plotArea.getX() - 32.0f, y - 6.0f, 28.0f, 12.0f,
                    juce::Justification::centredRight, false);
    }

    // And the EQ's decibels down the right.
    g.setColour (colours::textFaint);

    for (auto db : { -12.0f, 0.0f, 12.0f })
        g.drawText (db > 0.0f ? "+" + juce::String ((int) db) + " dB"
                              : juce::String ((int) db) + " dB",
                    plotArea.getRight() + 4.0f, gainToY (db) - 6.0f, 40.0f, 12.0f,
                    juce::Justification::centredLeft, false);
}

void VerbDisplay::drawDecay (juce::Graphics& g) const
{
    const auto settings = const_cast<NodoVerbProcessor&> (processor).getSettings();
    const auto sampleRate = processor.getEngineSampleRate();

    juce::Path curve, fill;
    const auto steps = 140;

    for (int i = 0; i <= steps; ++i)
    {
        const auto proportion = (float) i / (float) steps;
        const auto hz = minHz * std::pow (maxHz / minHz, proportion);
        const auto x = plotArea.getX() + proportion * plotArea.getWidth();
        const auto y = secondsToY (settings.decay.secondsAt (hz, sampleRate));

        if (i == 0)
        {
            curve.startNewSubPath (x, y);
            fill.startNewSubPath (x, plotArea.getBottom());
            fill.lineTo (x, y);
        }
        else
        {
            curve.lineTo (x, y);
            fill.lineTo (x, y);
        }
    }

    fill.lineTo (plotArea.getRight(), plotArea.getBottom());
    fill.closeSubPath();

    g.setColour (colours::accent().withAlpha (0.22f));
    g.fillPath (fill);

    g.setColour (colours::accentBright());
    g.strokePath (curve, juce::PathStrokeType (2.0f));

    for (auto handle : { Handle::decayLow, Handle::decayHigh })
    {
        const auto position = handlePosition (handle);
        const auto radius = handle == hovered || handle == dragging ? 6.5f : 5.0f;

        g.setColour (colours::accentBright());
        g.fillEllipse (position.x - radius, position.y - radius, radius * 2.0f, radius * 2.0f);
    }
}

void VerbDisplay::drawPostEq (juce::Graphics& g) const
{
    const auto settings = const_cast<NodoVerbProcessor&> (processor).getSettings();
    const auto sampleRate = processor.getEngineSampleRate();

    juce::Path curve;
    const auto steps = 140;

    for (int i = 0; i <= steps; ++i)
    {
        const auto proportion = (float) i / (float) steps;
        const auto hz = minHz * std::pow (maxHz / minHz, proportion);
        const auto x = plotArea.getX() + proportion * plotArea.getWidth();
        const auto y = gainToY (postEqDb (settings, hz, sampleRate));

        if (i == 0)
            curve.startNewSubPath (x, y);
        else
            curve.lineTo (x, y);
    }

    g.setColour (colours::warning.withAlpha (0.85f));
    g.strokePath (curve, juce::PathStrokeType (1.6f));

    for (auto handle : { Handle::postLow, Handle::postMid, Handle::postHigh })
    {
        const auto position = handlePosition (handle);
        const auto radius = handle == hovered || handle == dragging ? 5.5f : 4.0f;

        g.setColour (colours::warning);
        g.fillEllipse (position.x - radius, position.y - radius, radius * 2.0f, radius * 2.0f);
    }
}

void VerbDisplay::paint (juce::Graphics& g)
{
    g.setColour (colours::background);
    g.fillRect (getLocalBounds());

    drawGrid (g);
    drawDecay (g);
    drawPostEq (g);

    g.setFont (fonts::bold (8.5f));
    g.setColour (colours::accentBright().withAlpha (0.8f));
    g.drawText ("DECAY", plotArea.getX() + 5.0f, plotArea.getY() + 2.0f, 60.0f, 11.0f,
                juce::Justification::centredLeft, false);

    g.setColour (colours::warning.withAlpha (0.8f));
    g.drawText ("POST EQ", plotArea.getRight() - 65.0f, plotArea.getY() + 2.0f, 60.0f, 11.0f,
                juce::Justification::centredRight, false);

    g.setColour (colours::grid);
    g.drawRect (plotArea, 1.0f);
}

juce::Point<float> VerbDisplay::handlePosition (Handle handle) const
{
    const auto settings = const_cast<NodoVerbProcessor&> (processor).getSettings();
    const auto sampleRate = processor.getEngineSampleRate();

    switch (handle)
    {
        case Handle::decayLow:
            return { frequencyToX (settings.decay.lowFrequency),
                     secondsToY (settings.decay.secondsAt (settings.decay.lowFrequency, sampleRate)) };

        case Handle::decayHigh:
            return { frequencyToX (settings.decay.highFrequency),
                     secondsToY (settings.decay.secondsAt (settings.decay.highFrequency, sampleRate)) };

        case Handle::postLow:
            return { frequencyToX (settings.postLowFreq), gainToY (settings.postLowGain) };

        case Handle::postMid:
            return { frequencyToX (settings.postMidFreq), gainToY (settings.postMidGain) };

        case Handle::postHigh:
            return { frequencyToX (settings.postHighFreq), gainToY (settings.postHighGain) };

        case Handle::none:
        default:
            return {};
    }
}

VerbDisplay::Handle VerbDisplay::handleAt (juce::Point<float> position) const
{
    auto best = Handle::none;
    auto bestDistance = 15.0f;

    for (auto handle : { Handle::decayLow, Handle::decayHigh,
                         Handle::postLow, Handle::postMid, Handle::postHigh })
    {
        const auto distance = handlePosition (handle).getDistanceFrom (position);

        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = handle;
        }
    }

    return best;
}

void VerbDisplay::handleParameters (Handle handle,
                                    juce::RangedAudioParameter*& frequency,
                                    juce::RangedAudioParameter*& amount) const
{
    auto& state = const_cast<NodoVerbProcessor&> (processor).getState();

    auto get = [&state] (const juce::String& id)
    {
        return dynamic_cast<juce::RangedAudioParameter*> (state.getParameter (id));
    };

    switch (handle)
    {
        case Handle::decayLow:  frequency = get (ids::decayLowFreq);  amount = get (ids::decayLow);  break;
        case Handle::decayHigh: frequency = get (ids::decayHighFreq); amount = get (ids::decayHigh); break;
        case Handle::postLow:   frequency = get (ids::postLowFreq);   amount = get (ids::postLowGain); break;
        case Handle::postMid:   frequency = get (ids::postMidFreq);   amount = get (ids::postMidGain); break;
        case Handle::postHigh:  frequency = get (ids::postHighFreq);  amount = get (ids::postHighGain); break;

        case Handle::none:
        default: frequency = nullptr; amount = nullptr; break;
    }
}

void VerbDisplay::dragHandle (Handle handle, juce::Point<float> position)
{
    juce::RangedAudioParameter* frequency = nullptr;
    juce::RangedAudioParameter* amount = nullptr;
    handleParameters (handle, frequency, amount);

    if (frequency == nullptr || amount == nullptr)
        return;

    const auto hz = xToFrequency (position.x);
    frequency->setValueNotifyingHost (frequency->convertTo0to1 (hz));

    if (handle == Handle::decayLow || handle == Handle::decayHigh)
    {
        /*  The handle is dragged in seconds, which is what the axis is in, and
            the parameter is a multiplier of the mid decay. Converting here
            rather than making the parameter a time is deliberate: a multiplier
            keeps its meaning when the decay knob moves, so the shape of the
            curve survives being made longer or shorter.
        */
        const auto settings = processor.getSettings();
        const auto wanted = yToSeconds (position.y);
        const auto multiplier = wanted / juce::jmax (0.02f, settings.decay.midSeconds);

        amount->setValueNotifyingHost (amount->convertTo0to1 (multiplier));
    }
    else
    {
        amount->setValueNotifyingHost (amount->convertTo0to1 (yToGain (position.y)));
    }
}

void VerbDisplay::beginHandleGesture (Handle handle)
{
    juce::RangedAudioParameter* frequency = nullptr;
    juce::RangedAudioParameter* amount = nullptr;
    handleParameters (handle, frequency, amount);

    if (frequency != nullptr) frequency->beginChangeGesture();
    if (amount != nullptr)    amount->beginChangeGesture();
}

void VerbDisplay::endHandleGesture()
{
    if (dragging == Handle::none)
        return;

    juce::RangedAudioParameter* frequency = nullptr;
    juce::RangedAudioParameter* amount = nullptr;
    handleParameters (dragging, frequency, amount);

    if (frequency != nullptr) frequency->endChangeGesture();
    if (amount != nullptr)    amount->endChangeGesture();

    dragging = Handle::none;
}

void VerbDisplay::mouseDown (const juce::MouseEvent& event)
{
    const auto handle = handleAt (event.position);

    if (handle == Handle::none)
        return;

    dragging = handle;
    beginHandleGesture (handle);
    dragHandle (handle, event.position);
}

void VerbDisplay::mouseDrag (const juce::MouseEvent& event)
{
    if (dragging != Handle::none)
        dragHandle (dragging, event.position);
}

void VerbDisplay::mouseUp (const juce::MouseEvent&)
{
    endHandleGesture();
}

void VerbDisplay::mouseMove (const juce::MouseEvent& event)
{
    const auto handle = handleAt (event.position);

    if (handle != hovered)
    {
        hovered = handle;
        repaint();
    }
}

void VerbDisplay::mouseExit (const juce::MouseEvent&)
{
    if (hovered == Handle::none)
        return;

    hovered = Handle::none;
    repaint();
}
} // namespace nodo::verb
