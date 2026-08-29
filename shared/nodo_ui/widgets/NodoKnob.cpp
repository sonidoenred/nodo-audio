namespace nodo
{
using namespace nodo::theme;

NodoKnob::NodoKnob (juce::String captionToUse)
    : caption (std::move (captionToUse))
{
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, (int) metrics::knobValueH);
    slider.setColour (juce::Slider::textBoxTextColourId, colours::text);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

    /*  Velocity mode off: the fine adjustment is the modifier (see NodoSlider),
        not the speed of the gesture. A knob whose landing point depends on how
        fast you moved cannot be set twice to the same value on purpose.
    */
    slider.setVelocityBasedMode (false);
    slider.setDoubleClickReturnValue (true, 0.0);
    slider.setScrollWheelEnabled (true);

    addAndMakeVisible (slider);
}

void NodoKnob::detach()
{
    attachment.reset();
}

void NodoKnob::attachTo (juce::AudioProcessorValueTreeState& state,
                         const juce::String& parameterID)
{
    // Must come first: see the note in the header.
    attachment.reset();

    if (auto* parameter = state.getParameter (parameterID))
        slider.setDoubleClickReturnValue (true,
                                          parameter->convertFrom0to1 (parameter->getDefaultValue()));

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, parameterID, slider);
}

void NodoKnob::setAccentColour (juce::Colour colour)
{
    slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
    repaint();
}

void NodoKnob::setCaption (const juce::String& newCaption)
{
    caption = newCaption;
    repaint();
}

void NodoKnob::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop ((int) metrics::knobLabelH);
    slider.setBounds (bounds);
}

void NodoKnob::paint (juce::Graphics& g)
{
    g.setFont (fonts::bold (10.0f));

    // A control that has stopped meaning anything has to look like it.
    g.setColour (colours::textDim.withMultipliedAlpha (isEnabled() ? 1.0f : 0.4f));
    g.drawText (caption.toUpperCase(),
                getLocalBounds().removeFromTop ((int) metrics::knobLabelH),
                juce::Justification::centred, false);
}
} // namespace nodo
