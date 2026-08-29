namespace nodo
{
using namespace nodo::theme;

NodoLookAndFeel::NodoLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, colours::background);

    setColour (juce::Slider::rotarySliderFillColourId,    colours::accent());
    setColour (juce::Slider::rotarySliderOutlineColourId, colours::grid);
    setColour (juce::Slider::thumbColourId,               colours::text);
    setColour (juce::Slider::trackColourId,               colours::accent());
    setColour (juce::Slider::backgroundColourId,          colours::grid);
    setColour (juce::Slider::textBoxTextColourId,         colours::text);
    setColour (juce::Slider::textBoxBackgroundColourId,   colours::panel);
    setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId,    colours::accentDim());

    setColour (juce::Label::textColourId,                 colours::text);
    setColour (juce::Label::backgroundColourId,           juce::Colours::transparentBlack);

    setColour (juce::TextButton::buttonColourId,          colours::panelRaised);
    setColour (juce::TextButton::buttonOnColourId,        colours::accent());
    setColour (juce::TextButton::textColourOffId,         colours::textDim);
    setColour (juce::TextButton::textColourOnId,          colours::text);

    setColour (juce::ComboBox::backgroundColourId,        colours::panelRaised);
    setColour (juce::ComboBox::textColourId,              colours::text);
    setColour (juce::ComboBox::outlineColourId,           colours::grid);
    setColour (juce::ComboBox::arrowColourId,             colours::textDim);

    setColour (juce::PopupMenu::backgroundColourId,       colours::panelRaised);
    setColour (juce::PopupMenu::textColourId,             colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accent());
    setColour (juce::PopupMenu::highlightedTextColourId,  juce::Colours::white);

    setColour (juce::TooltipWindow::backgroundColourId,   colours::panelRaised);
    setColour (juce::TooltipWindow::textColourId,         colours::text);
}

void NodoLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float rotaryStartAngle,
                                        float rotaryEndAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto thickness = juce::jmax (2.5f, radius * 0.16f);
    const auto arcRadius = radius - thickness * 0.5f;

    const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    const auto fill = slider.findColour (juce::Slider::rotarySliderFillColourId);
    const auto track = slider.findColour (juce::Slider::rotarySliderOutlineColourId);

    // Background arc.
    juce::Path background;
    background.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                              rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (track);
    g.strokePath (background, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));

    // Value arc. Bipolar controls fill outwards from the centre, unipolar ones
    // from the start, which is what makes a gain knob readable at a glance.
    const auto range = slider.getRange();
    const auto isBipolar = range.getStart() < 0.0 && range.getEnd() > 0.0;
    const auto originAngle = isBipolar
                           ? rotaryStartAngle + (float) slider.valueToProportionOfLength (0.0)
                                                * (rotaryEndAngle - rotaryStartAngle)
                           : rotaryStartAngle;

    if (std::abs (angle - originAngle) > 1.0e-4f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             juce::jmin (originAngle, angle),
                             juce::jmax (originAngle, angle), true);
        g.setColour (slider.isEnabled() ? fill : fill.withAlpha (0.4f));
        g.strokePath (value, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    // Pointer.
    const auto pointerLength = arcRadius * 0.62f;
    const auto pointerThickness = juce::jmax (1.6f, thickness * 0.42f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-pointerThickness * 0.5f, -arcRadius + thickness * 0.4f,
                                 pointerThickness, pointerLength, pointerThickness * 0.5f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre));

    g.setColour (colours::text.withMultipliedAlpha (slider.isEnabled() ? 1.0f : 0.4f));
    g.fillPath (pointer);
}

void NodoLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float, float,
                                        juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const auto isHorizontal = style == juce::Slider::LinearHorizontal
                           || style == juce::Slider::LinearBar;

    const auto trackThickness = 4.0f;
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();

    juce::Rectangle<float> track;

    if (isHorizontal)
        track = { bounds.getX(), bounds.getCentreY() - trackThickness * 0.5f,
                  bounds.getWidth(), trackThickness };
    else
        track = { bounds.getCentreX() - trackThickness * 0.5f, bounds.getY(),
                  trackThickness, bounds.getHeight() };

    g.setColour (slider.findColour (juce::Slider::backgroundColourId));
    g.fillRoundedRectangle (track, trackThickness * 0.5f);

    auto filled = track;

    if (isHorizontal)
        filled.setRight (sliderPos);
    else
        filled.setTop (sliderPos);

    g.setColour (slider.findColour (juce::Slider::trackColourId));
    g.fillRoundedRectangle (filled, trackThickness * 0.5f);

    const auto thumbRadius = 6.0f;
    const juce::Point<float> thumbCentre = isHorizontal
        ? juce::Point<float> (sliderPos, track.getCentreY())
        : juce::Point<float> (track.getCentreX(), sliderPos);

    g.setColour (colours::text);
    g.fillEllipse (juce::Rectangle<float> (thumbRadius * 2.0f, thumbRadius * 2.0f)
                     .withCentre (thumbCentre));
}

void NodoLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                            const juce::Colour& backgroundColour,
                                            bool shouldDrawButtonAsHighlighted,
                                            bool shouldDrawButtonAsDown)
{
    auto colour = backgroundColour;

    if (shouldDrawButtonAsDown)
        colour = colour.brighter (0.2f);
    else if (shouldDrawButtonAsHighlighted)
        colour = colour.brighter (0.1f);

    g.setColour (colour);
    g.fillRoundedRectangle (button.getLocalBounds().toFloat(), metrics::cornerRadius);
}

void NodoLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                      bool, bool)
{
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour (button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId
                                                            : juce::TextButton::textColourOffId)
                   .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));

    g.drawText (button.getButtonText(), button.getLocalBounds(),
                juce::Justification::centred, false);
}

void NodoLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                        bool shouldDrawButtonAsHighlighted, bool)
{
    const auto bounds = button.getLocalBounds().toFloat();
    const auto on = button.getToggleState();

    auto fill = on ? colours::accent() : colours::panelRaised;

    if (shouldDrawButtonAsHighlighted)
        fill = fill.brighter (0.12f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, metrics::cornerRadius);

    if (button.getButtonText().isNotEmpty())
    {
        g.setFont (fonts::bold (11.0f));
        g.setColour (on ? juce::Colours::white : colours::textDim);
        g.drawText (button.getButtonText(), bounds, juce::Justification::centred, false);
    }
}

void NodoLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                    int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat();

    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, metrics::cornerRadius);

    // Chevron.
    const auto arrowArea = juce::Rectangle<float> ((float) width - 18.0f, 0.0f, 12.0f, (float) height)
                             .reduced (0.0f, (float) height * 0.42f);

    juce::Path chevron;
    chevron.startNewSubPath (arrowArea.getX(), arrowArea.getY());
    chevron.lineTo (arrowArea.getCentreX(), arrowArea.getBottom());
    chevron.lineTo (arrowArea.getRight(), arrowArea.getY());

    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.strokePath (chevron, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
}

void NodoLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 0, box.getWidth() - 26, box.getHeight());
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centredLeft);
}

void NodoLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    /*  Esquinas rectas a proposito.

        Un menu vive en su propia ventana, y esa ventana solo puede tener las
        esquinas transparentes si el sistema deja usar ventanas semitransparentes.
        En macOS no las dejaba: las cuatro esquinas redondeadas se quedaban sin
        pintar y salia el blanco de la ventana por debajo, que es justo lo que
        se veia en las capturas. Rellenar el rectangulo entero y bordearlo es
        feo en un solo sitio —la esquina— y correcto en todos los sistemas.
    */
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);

    g.fillAll (colours::panelRaised);

    g.setColour (colours::gridStrong);
    g.drawRect (bounds, 1.0f);
}

juce::Font NodoLookAndFeel::getLabelFont (juce::Label& label)
{
    return fonts::regular (juce::jlimit (11.0f, 15.0f, (float) label.getHeight() * 0.72f));
}

juce::Font NodoLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return fonts::regular (12.5f);
}

juce::Font NodoLookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return fonts::bold (11.5f);
}

juce::Font NodoLookAndFeel::getPopupMenuFont()
{
    return fonts::regular (13.0f);
}
} // namespace nodo
