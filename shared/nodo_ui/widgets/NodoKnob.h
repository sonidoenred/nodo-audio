#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../theme/NodoTheme.h"
#include "NodoSlider.h"

namespace nodo
{
/** A labelled rotary control: caption on top, knob in the middle, editable value
    underneath. Used by every plugin so a knob never has to be laid out twice.
*/
class NodoKnob : public juce::Component
{
public:
    explicit NodoKnob (juce::String caption);

    /** Binds the knob to a parameter, replacing any previous binding.

        The old attachment is destroyed first, and that ordering matters: a
        SliderAttachment writes to its parameter whenever the slider moves, and
        constructing a new one moves the slider to the new parameter's value. If
        the previous attachment were still alive at that moment it would hear
        that movement and write the new band's value into the old band. That is
        how re-pointing this panel at a different band used to quietly corrupt
        the band you just left.
    */
    void attachTo (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID);

    /** Releases the parameter binding. */
    void detach();

    void setAccentColour (juce::Colour colour);
    void setCaption (const juce::String& newCaption);

    juce::Slider& getSlider() noexcept { return slider; }

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    NodoSlider slider { juce::Slider::RotaryHorizontalVerticalDrag,
                        juce::Slider::TextBoxBelow };
    juce::String caption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodoKnob)
};
} // namespace nodo
