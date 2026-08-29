#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace nodo
{
/** A juce::Slider with one thing added: holding the command modifier — Cmd on
    macOS, Ctrl everywhere else — makes the drag fine.

    JUCE has no fine drag outside velocity mode, and velocity mode is worse than
    nothing here: where the value ends up depends on how fast the mouse was
    moving, so the same gesture twice gives two different values and a knob can
    run away from under the pointer. This keeps the plain one-to-one drag and
    only changes how many pixels a full sweep takes.

    Every knob and every thin row in the suite is one of these, so the gesture
    means the same thing in all seven plugins.
*/
class NodoSlider : public juce::Slider
{
public:
    NodoSlider (SliderStyle style, TextEntryBoxPosition textBoxPosition)
        : juce::Slider (style, textBoxPosition)
    {
        setMouseDragSensitivity (coarseSensitivity);
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        /*  Decided when the button goes down rather than on every motion. JUCE
            reads the sensitivity while the drag is running, so changing it
            halfway through makes the value jump — pressing the modifier mid
            drag would move the knob, which is the opposite of what a fine
            adjustment is for.
        */
        setMouseDragSensitivity (event.mods.isCommandDown() ? fineSensitivity : coarseSensitivity);

        juce::Slider::mouseDown (event);
    }

private:
    // JUCE's default, and eight times that. Eight because a 12 dB range then
    // moves in steps you can land on without holding your breath, and a 20
    // second decay still crosses its whole range in one screen height.
    static constexpr int coarseSensitivity = 250;
    static constexpr int fineSensitivity = 2000;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodoSlider)
};
} // namespace nodo
