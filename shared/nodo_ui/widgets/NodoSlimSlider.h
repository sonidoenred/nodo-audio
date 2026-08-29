#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../theme/NodoTheme.h"
#include "NodoSlider.h"

namespace nodo
{
/** A thin horizontal control: caption on the left, value on the right, a track
    filled from the left underneath both.

    Exists because a plugin with twelve knobs is a plugin nobody can read. The
    controls that are set once and left alone — knee, range, hold, lookahead —
    do not deserve the same visual weight as the ones that get moved on every
    track, and giving them a knob each is what turns a compressor into a
    dashboard.
*/
class NodoSlimSlider : public juce::Component
{
public:
    explicit NodoSlimSlider (juce::String caption);
    ~NodoSlimSlider() override;

    void attachTo (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID);
    void detach();

    void setAccentColour (juce::Colour colour);

    /** Fills outwards from the middle instead of from the left. For a control
        whose zero is in the centre — a pan, a modulation amount — a bar that
        starts at the left reads as half on when it is off.
    */
    void setBipolar (bool shouldBeBipolar);

    juce::Slider& getSlider() noexcept { return slider; }

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    /** The slider is here for its behaviour — dragging, keyboard, host
        automation through the attachment — not for its looks. Everything on
        screen is painted by this component, so the default track and thumb are
        suppressed rather than recoloured: a transparent thumb is still a thumb
        drawn on top of the caption.
    */
    struct InvisibleSliderLookAndFeel : public juce::LookAndFeel_V4
    {
        void drawLinearSlider (juce::Graphics&, int, int, int, int,
                               float, float, float,
                               juce::Slider::SliderStyle, juce::Slider&) override {}
    };

    InvisibleSliderLookAndFeel sliderLookAndFeel;
    NodoSlider slider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::String caption;
    bool bipolar { false };
    juce::Colour accent { theme::colours::accent() };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodoSlimSlider)
};
} // namespace nodo
