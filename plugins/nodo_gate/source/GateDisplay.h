#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <nodo_ui/nodo_ui.h>

#include "PluginProcessor.h"

namespace nodo::gate
{
/** The gate's picture: what it has been doing, and what it would do to any
    level you fed it.

    Left, a history that scrolls: the detector level as a filled shape, the gain
    reduction hanging from the top, and *two* threshold lines rather than one —
    the level at which the gate opens and the lower one at which it closes
    again. Drawing only one would hide the single most useful thing about a
    gate's behaviour, which is the gap between those two numbers.

    Right, the static curve. On a gate this reads bottom-left rather than
    top-right: the interesting corner is down where quiet material falls off,
    and the flat floor across the bottom is the range doing its job.
*/
class GateDisplay : public juce::Component,
                    private juce::Timer
{
public:
    explicit GateDisplay (NodoGateProcessor&);
    ~GateDisplay() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    void drawGrid (juce::Graphics&) const;
    void drawHistory (juce::Graphics&) const;
    void drawCurve (juce::Graphics&) const;

    float levelToY (float db) const;
    float yToLevel (float y) const;

    void setThresholdFromY (float y);

    static constexpr int historyPoints = 260;
    static constexpr float topDb = 0.0f;
    static constexpr float bottomDb = -90.0f;

    NodoGateProcessor& processor;

    std::array<float, historyPoints> levelHistory {};
    std::array<float, historyPoints> reductionHistory {};
    std::array<bool, historyPoints> openHistory {};
    int writePos { 0 };

    juce::Rectangle<float> historyArea, curveArea;

    bool draggingThreshold { false };
    bool hoveringThreshold { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GateDisplay)
};
} // namespace nodo::gate
