#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <nodo_ui/nodo_ui.h>

#include "PluginProcessor.h"

namespace nodo::verb
{
/** The decay time, drawn against frequency, with the post EQ over the top.

    Two curves on one picture because they answer the two halves of the same
    question. The decay curve says *how long* the reverb takes to disappear at
    each frequency; the EQ curve says *how loud* it is there. Those are
    different things and most reverbs only give you the second one, which is why
    people reach for an EQ after a reverb to fix a boomy tail and end up with a
    thin one instead. Shortening the bass decay and turning the bass down are
    not the same repair.

    In seconds rather than in percentages of the decay knob. The number the tests
    measure out of the audio is a time, so the number on screen is a time.
*/
class VerbDisplay : public juce::Component,
                    private juce::Timer
{
public:
    explicit VerbDisplay (NodoVerbProcessor&);
    ~VerbDisplay() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    /** The five things on the picture that can be dragged. FROZEN only in the
        sense that the order is used for hit testing; nothing is saved.
    */
    enum class Handle { none = -1, decayLow = 0, decayHigh, postLow, postMid, postHigh };

    void timerCallback() override;

    void drawGrid (juce::Graphics&) const;
    void drawDecay (juce::Graphics&) const;
    void drawPostEq (juce::Graphics&) const;

    float frequencyToX (float hz) const;
    float xToFrequency (float x) const;
    float secondsToY (float seconds) const;
    float yToSeconds (float y) const;
    float gainToY (float db) const;
    float yToGain (float y) const;

    juce::Point<float> handlePosition (Handle) const;
    Handle handleAt (juce::Point<float>) const;
    void dragHandle (Handle, juce::Point<float>);
    void beginHandleGesture (Handle);
    void endHandleGesture();

    /** The two parameters a handle moves: its frequency and its amount. */
    void handleParameters (Handle, juce::RangedAudioParameter*&, juce::RangedAudioParameter*&) const;

    NodoVerbProcessor& processor;

    juce::Rectangle<float> plotArea;

    Handle dragging { Handle::none };
    Handle hovered { Handle::none };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VerbDisplay)
};
} // namespace nodo::verb
