#include "NodoSlimSlider.h"

namespace nodo
{
using namespace nodo::theme;

NodoSlimSlider::NodoSlimSlider (juce::String captionToUse)
    : caption (std::move (captionToUse))
{
    slider.setLookAndFeel (&sliderLookAndFeel);
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    slider.setColour (juce::Slider::backgroundColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::trackColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::thumbColourId, juce::Colours::transparentBlack);
    slider.onValueChange = [this] { repaint(); };

    addAndMakeVisible (slider);
}

NodoSlimSlider::~NodoSlimSlider()
{
    slider.setLookAndFeel (nullptr);
}

void NodoSlimSlider::attachTo (juce::AudioProcessorValueTreeState& state,
                               const juce::String& parameterID)
{
    // Same ordering rule as NodoKnob: the old attachment has to be gone before
    // the new one moves the slider, or it writes this value into the previous
    // parameter on its way out.
    attachment.reset();

    // Double click goes back to the factory value, the same as on the knobs:
    // these are the controls people are most likely to have nudged by accident.
    if (auto* parameter = state.getParameter (parameterID))
        slider.setDoubleClickReturnValue (true,
                                          parameter->convertFrom0to1 (parameter->getDefaultValue()));

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, parameterID, slider);

    repaint();
}

void NodoSlimSlider::detach()
{
    attachment.reset();
}

void NodoSlimSlider::setAccentColour (juce::Colour colour)
{
    accent = colour;
    repaint();
}

void NodoSlimSlider::setBipolar (bool shouldBeBipolar)
{
    bipolar = shouldBeBipolar;
    repaint();
}

void NodoSlimSlider::resized()
{
    slider.setBounds (getLocalBounds());
}

void NodoSlimSlider::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.0f, 1.0f);

    /*  A control that has stopped meaning anything has to look like it. Hiding
        it would be worse — the row would move and you would wonder where it
        went — so it stays where it is and goes quiet.
    */
    const auto dim = isEnabled() ? 1.0f : 0.3f;

    g.setColour (colours::panelRaised.withMultipliedAlpha (dim));
    g.fillRoundedRectangle (bounds, 3.0f);

    const auto range = slider.getRange();
    const auto span = range.getLength();
    const auto proportion = span > 0.0
                          ? (float) ((slider.getValue() - range.getStart()) / span)
                          : 0.0f;

    // The fill is the value: no thumb to hunt for, and the row still reads at a
    // glance from across the room.
    if (bipolar)
    {
        const auto centre = bounds.getCentreX();
        const auto edge = bounds.getX() + bounds.getWidth() * proportion;

        if (std::abs (edge - centre) > 0.5f)
        {
            g.setColour (accent.withAlpha (0.55f * dim));
            g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (centre, edge), bounds.getY(),
                                                            std::abs (edge - centre), bounds.getHeight()),
                                    3.0f);
        }

        g.setColour (colours::grid);
        g.drawVerticalLine ((int) centre, bounds.getY() + 2.0f, bounds.getBottom() - 2.0f);
    }
    else if (proportion > 0.0f)
    {
        auto filled = bounds.withWidth (juce::jmax (3.0f, bounds.getWidth() * proportion));
        g.setColour (accent.withAlpha (0.55f * dim));
        g.fillRoundedRectangle (filled, 3.0f);
    }

    auto text = bounds.reduced (7.0f, 0.0f);

    const auto captionFont = fonts::bold (9.0f);
    const auto valueFont = fonts::mono (10.0f);
    const auto valueText = slider.getTextFromValue (slider.getValue());

    /*  Caption on the left, value on the right, in the same rectangle: when the
        row gets narrow they print on top of each other and neither can be read.
        The value wins, because a control whose name you have forgotten is still
        usable and a number you cannot read is not. This happened for real in
        two plugins before the check was here.

        The cost of deciding it from the current value is that a caption can
        disappear when the number gets longer — so every row in the suite is
        laid out with room to spare rather than exactly enough. Guaranteeing
        that the two never overlap is worth more than guaranteeing that the
        caption never moves.
    */
    const auto captionWidth = juce::GlyphArrangement::getStringWidth (captionFont, caption.toUpperCase());
    const auto valueWidth = juce::GlyphArrangement::getStringWidth (valueFont, valueText);

    if (captionWidth + valueWidth + 3.0f < text.getWidth())
    {
        g.setFont (captionFont);
        g.setColour (colours::textDim.withMultipliedAlpha (dim));
        g.drawText (caption.toUpperCase(), text, juce::Justification::centredLeft, false);
    }

    g.setFont (valueFont);
    g.setColour (colours::text.withMultipliedAlpha (dim));
    g.drawText (valueText, text, juce::Justification::centredRight, false);
}
} // namespace nodo
