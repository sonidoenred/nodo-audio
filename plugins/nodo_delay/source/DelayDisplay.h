#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <nodo_ui/nodo_ui.h>

#include "PluginProcessor.h"
#include "TapPattern.h"

namespace nodo::delay
{
/** The pattern of repeats, drawn.

    Two lines feeding each other at different times do not produce a decaying
    series, they produce an interleaved rhythm, and most delay plugins ask you
    to find it by ear. Here it is on screen: every repeat as a bar on a time
    axis, up for the left of the stereo picture and down for the right, its
    height the level it arrives at.

    On the right, the loop's filter curve. It belongs next to the pattern rather
    than tucked away with the other knobs, because those two pictures together
    are the whole answer to "what will this sound like": one says when, the
    other says what colour, and the fact that the colour compounds on every
    repeat is the thing a curve drawn once cannot show — so the curve is drawn
    several times over, once per generation, fading out.
*/
class DelayDisplay : public juce::Component,
                     private juce::Timer
{
public:
    explicit DelayDisplay (NodoDelayProcessor&);
    ~DelayDisplay() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    void drawPattern (juce::Graphics&) const;
    void drawSteps (juce::Graphics&) const;
    void drawFilters (juce::Graphics&) const;

    /** Which step column a point falls in, or -1. */
    int stepAt (juce::Point<float> position) const;

    /** Turns a point inside a column into the two things a step has: how loud
        it is and where it sits in the picture.
    */
    void setStepFromPoint (int step, juce::Point<float> position, bool startGesture);
    void endStepGesture();

    NodoDelayProcessor& processor;

    juce::Rectangle<float> patternArea, stepArea, filterArea;

    int draggingStep { -1 };
    int hoveredStep { -1 };
    bool gestureOpen { false };
    bool showingSteps { false };

    std::vector<Tap> taps;
    float horizonMs { 2000.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DelayDisplay)
};
} // namespace nodo::delay
