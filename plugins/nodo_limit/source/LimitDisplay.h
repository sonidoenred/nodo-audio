#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <nodo_ui/nodo_ui.h>

#include "PluginProcessor.h"

namespace nodo::limit
{
/** What the limiter has been doing, and how loud the result is.

    Left: a scrolling history. Input level as a filled shape, output level drawn
    over it, the ceiling as a line, and the gain reduction hanging from the top.
    On a limiter these two shapes tell you the thing that matters at a glance —
    how much of the difference between them is the plugin.

    Right: loudness. Momentary, short term and integrated LUFS to BS.1770, and
    the highest true peak seen. A free limiter without a loudness meter sends
    people to another plugin to answer the one question they had.
*/
class LimitDisplay : public juce::Component,
                     private juce::Timer
{
public:
    explicit LimitDisplay (NodoLimitProcessor&);
    ~LimitDisplay() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    void drawHistory (juce::Graphics&) const;
    void drawLoudness (juce::Graphics&) const;
    void drawOutputMeter (juce::Graphics&) const;

    float levelToY (float db) const;

    static constexpr int historyPoints = 300;
    static constexpr float topDb = 3.0f;
    static constexpr float bottomDb = -45.0f;

    NodoLimitProcessor& processor;

    std::array<float, historyPoints> inputHistory {};
    std::array<float, historyPoints> outputHistory {};
    std::array<float, historyPoints> reductionHistory {};
    int writePos { 0 };

    juce::Rectangle<float> historyArea, loudnessArea, meterArea;

    // Peak hold for the output meter, in dB, decaying slowly.
    std::array<float, 2> heldPeakDb { -100.0f, -100.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LimitDisplay)
};
} // namespace nodo::limit
