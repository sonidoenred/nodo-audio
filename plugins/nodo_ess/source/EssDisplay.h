#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <nodo_ui/nodo_ui.h>

#include "PluginProcessor.h"

namespace nodo::ess
{
/** The level of the band being watched, the threshold, and what came off.

    The threshold line is drawn where it has actually ended up, which in adaptive
    mode is not where the knob is: the knob sets a distance above the band's own
    running level, and the line shows the result. A moving threshold you cannot
    see is a threshold you cannot trust, and it is the reason most people give up
    on adaptive de-essers and go back to riding a fixed one.

    Dragging the line sets the threshold, in both modes.
*/
class EssDisplay : public juce::Component,
                   private juce::Timer
{
public:
    explicit EssDisplay (NodoEssProcessor&);
    ~EssDisplay() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    float levelToY (float db) const;
    float yToLevel (float y) const;
    void setThresholdFromY (float y);

    static constexpr int historyPoints = 260;
    static constexpr float topDb = 0.0f;
    static constexpr float bottomDb = -66.0f;

    NodoEssProcessor& processor;

    std::array<float, historyPoints> detectorHistory {};
    std::array<float, historyPoints> thresholdHistory {};
    std::array<float, historyPoints> reductionHistory {};
    int writePos { 0 };

    juce::Rectangle<float> historyArea, readoutArea;
    bool dragging { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EssDisplay)
};
} // namespace nodo::ess
