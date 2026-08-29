#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <nodo_ui/nodo_ui.h>

#include "PluginProcessor.h"

namespace nodo::comp
{
/** The compressor's one big picture: what it has been doing, and what it would
    do to any level you fed it.

    Left, a history that scrolls: input level as a filled shape, gain reduction
    hanging down from the top in the accent colour, the threshold as a line you
    can drag. Right, the static curve with a dot showing where the signal
    currently sits on it.

    The two halves answer different questions. The history answers "is it
    working, and is it breathing with the music". The curve answers "what will
    happen to a peak 3 dB louder than this one". A compressor that only shows a
    gain reduction meter makes you infer both.
*/
class CompDisplay : public juce::Component,
                    private juce::Timer
{
public:
    explicit CompDisplay (NodoCompProcessor&);
    ~CompDisplay() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    void drawHistory (juce::Graphics&) const;
    void drawCurve (juce::Graphics&) const;
    void drawGrid (juce::Graphics&) const;

    /** dB to y inside the history, over the display's level range. */
    float levelToY (float db) const;
    float yToLevel (float y) const;

    void setThresholdFromY (float y);

    static constexpr int historyPoints = 260;
    static constexpr float topDb = 6.0f;
    static constexpr float bottomDb = -60.0f;

    NodoCompProcessor& processor;

    std::array<float, historyPoints> levelHistory {};
    std::array<float, historyPoints> reductionHistory {};
    int writePos { 0 };

    juce::Rectangle<float> historyArea, curveArea;

    bool draggingThreshold { false };
    bool hoveringThreshold { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompDisplay)
};
} // namespace nodo::comp
