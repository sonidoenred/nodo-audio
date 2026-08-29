#include "BandPanel.h"

namespace nodo::eq
{
using namespace nodo::theme;

BandPanel::BandPanel (NodoEqProcessor& processorToUse)
    : processor (processorToUse),
      state (processorToUse.getState())
{
    typeBox.addItemList (filterTypeNames(), 1);
    slopeBox.addItemList (slopeNames(), 1);
    channelBox.addItemList (channelModeNames(), 1);
    dynamicModeBox.addItemList (dynamicModeNames(), 1);

    auto styleToggle = [this] (juce::TextButton& b, juce::Colour on)
    {
        b.setColour (juce::TextButton::buttonColourId, colours::panelRaised);
        b.setColour (juce::TextButton::buttonOnColourId, on);
        b.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        addAndMakeVisible (b);
    };

    enableButton.setClickingTogglesState (true);
    styleToggle (enableButton, colours::accent());

    dynamicButton.setClickingTogglesState (true);
    styleToggle (dynamicButton, colours::accentBright());

    soloButton.setClickingTogglesState (false);
    styleToggle (soloButton, colours::warning);
    soloButton.onClick = [this]
    {
        if (currentBand < 0)
            return;

        processor.setSoloBand (processor.getSoloBand() == currentBand ? -1 : currentBand);
        updateEnablement();
    };

    // Invert is an action, not a state: it flips the sign of the gain once.
    styleToggle (invertButton, colours::accent());
    invertButton.onClick = [this]
    {
        if (currentBand < 0)
            return;

        if (auto* parameter = state.getParameter (ids::bandGain (currentBand)))
        {
            const auto current = readBand (state, currentBand).gainDb;
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (-current));
        }
    };

    dynamicHint.setJustificationType (juce::Justification::centredLeft);
    dynamicHint.setColour (juce::Label::textColourId, colours::textFaint);
    dynamicHint.setFont (fonts::regular (11.5f));
    dynamicHint.setText ("Off. The band sits at its gain.", juce::dontSendNotification);
    addAndMakeVisible (dynamicHint);

    noteLabel.setJustificationType (juce::Justification::centred);
    noteLabel.setColour (juce::Label::textColourId, colours::textDim);
    noteLabel.setFont (fonts::regular (11.0f));

    /*  Tooltips say why, not what: the caption already says what. The rest of
        the suite explains its controls this way and the equaliser — the plugin
        with the most controls of the seven — was the one that explained none of
        them.
    */
    enableButton.setTooltip ("Takes the band out of the chain without losing what it was set to. "
                            "A bypassed band costs nothing: it is skipped, not run flat.");
    soloButton.setTooltip ("Hear only what this band is working on. The listening filter is not "
                           "the band: soloing a low cut plays what it removes, because what you "
                           "want to check is what you are throwing away.");
    invertButton.setTooltip ("Flips the sign of the gain. A cut becomes the boost it was, which "
                             "is the fastest way to hear whether the frequency was the right one.");
    dynamicButton.setTooltip ("The band's gain follows the level instead of sitting still. What "
                              "the knob sets becomes the most it may move, not where it is.");

    typeBox.setTooltip ("The shape. The order is frozen - it is written into every saved "
                        "session - so new shapes go on the end of the list, never in the middle.");
    slopeBox.setTooltip ("How steeply a cut falls, in decibels per octave. Steep is not better: "
                         "a 96 dB slope rings, and the ringing is at the corner frequency.");
    channelBox.setTooltip ("Which part of the stereo image the band works on. Mid and Side are the "
                          "sum and the difference, so a Side band is deaf to anything centred.");
    dynamicModeBox.setTooltip ("Whether the band moves when the level goes above the threshold or "
                               "below it. Above is a de-esser, below is an exciter.");

    thresholdKnob.getSlider().setTooltip ("The level at which the band starts to move.");
    ratioKnob.getSlider().setTooltip ("How much of the excess turns into movement.");
    attackKnob.getSlider().setTooltip ("How fast the band reaches its target gain.");
    releaseKnob.getSlider().setTooltip ("How fast it goes back. Too short chatters on sustained "
                                        "material, too long and the band never returns between notes.");
    freqKnob.getSlider().setTooltip ("Where the band sits. It is smoothed in octaves rather than "
                                     "in hertz, so a sweep travels at the same musical speed "
                                     "everywhere on the scale.");
    gainKnob.getSlider().setTooltip ("How much the band lifts or cuts. In a dynamic band this is "
                                     "the furthest it may go, not where it stays.");
    qKnob.getSlider().setTooltip ("How wide the band is. On a cut it also sets how much it "
                                  "resonates at the corner.");

    dynRangeSlider.getSlider().setTooltip (
        "How far the band travels from where it sits, in decibels, when the "
        "dynamics reach the end of their range. At zero the band does not move: "
        "turning the dynamics on changes nothing until you say how much. It is "
        "the second handle on the curve, and dragging that is usually faster.");
    // Bipolar: el cero esta en el medio porque el recorrido va en las dos
    // direcciones, y una barra que se llena desde la izquierda diria que un
    // recorrido nulo esta a media escala.
    dynRangeSlider.setBipolar (true);
    addAndMakeVisible (dynRangeSlider);

    addAndMakeVisible (typeBox);
    addAndMakeVisible (slopeBox);
    addAndMakeVisible (channelBox);
    addAndMakeVisible (dynamicModeBox);
    addAndMakeVisible (thresholdKnob);
    addAndMakeVisible (ratioKnob);
    addAndMakeVisible (attackKnob);
    addAndMakeVisible (releaseKnob);
    addAndMakeVisible (freqKnob);
    addAndMakeVisible (gainKnob);
    addAndMakeVisible (qKnob);
    addAndMakeVisible (noteLabel);

    // setBand() early-outs when the band has not changed, and currentBand starts
    // at -1, so the initial hidden state has to be applied directly.
    updateEnablement();
    startTimerHz (12);
}

BandPanel::~BandPanel()
{
    stopTimer();
}

void BandPanel::setBand (int band)
{
    if (currentBand == band)
        return;

    currentBand = band;
    rebuildAttachments();
    updateEnablement();
    repaint();
}

void BandPanel::rebuildAttachments()
{
    // Attachments must be destroyed before new ones are created for the same
    // controls, otherwise two of them fight over the parameter.
    enableAttachment.reset();
    typeAttachment.reset();
    slopeAttachment.reset();
    channelAttachment.reset();
    dynamicAttachment.reset();
    dynamicModeAttachment.reset();
    thresholdKnob.detach();
    ratioKnob.detach();
    attackKnob.detach();
    releaseKnob.detach();
    dynRangeSlider.detach();
    freqKnob.detach();
    gainKnob.detach();
    qKnob.detach();

    if (currentBand < 0)
        return;

    const auto band = currentBand;

    enableAttachment = std::make_unique<ButtonAttachment> (state, ids::bandEnabled (band), enableButton);
    typeAttachment   = std::make_unique<ComboAttachment> (state, ids::bandType (band), typeBox);
    slopeAttachment  = std::make_unique<ComboAttachment> (state, ids::bandSlope (band), slopeBox);
    channelAttachment = std::make_unique<ComboAttachment> (state, ids::bandChannel (band), channelBox);
    dynamicAttachment = std::make_unique<ButtonAttachment> (state, ids::bandDynamic (band), dynamicButton);
    dynamicModeAttachment = std::make_unique<ComboAttachment> (state, ids::bandDynMode (band), dynamicModeBox);

    thresholdKnob.attachTo (state, ids::bandThreshold (band));
    ratioKnob.attachTo (state, ids::bandRatio (band));
    attackKnob.attachTo (state, ids::bandAttack (band));
    releaseKnob.attachTo (state, ids::bandRelease (band));
    dynRangeSlider.attachTo (state, ids::bandDynRange (band));

    freqKnob.attachTo (state, ids::bandFreq (band));
    gainKnob.attachTo (state, ids::bandGain (band));
    qKnob.attachTo (state, ids::bandQ (band));

    const auto colour = colours::forBand (band);

    for (auto* knob : { &freqKnob, &gainKnob, &qKnob,
                        &thresholdKnob, &ratioKnob, &attackKnob, &releaseKnob })
        knob->setAccentColour (colour);
}

void BandPanel::updateEnablement()
{
    const auto hasBand = currentBand >= 0;

    const std::array<juce::Component*, 10> always {
        &enableButton, &soloButton, &invertButton, &typeBox, &slopeBox,
        &freqKnob, &gainKnob, &qKnob, &noteLabel, &channelBox
    };

    dynamicButton.setVisible (hasBand);

    for (auto* c : always)
        c->setVisible (hasBand);

    if (! hasBand)
    {
        dynamicHint.setVisible (false);
        dynamicModeBox.setVisible (false);

        for (auto* knob : { &thresholdKnob, &ratioKnob, &attackKnob, &releaseKnob })
            knob->setVisible (false);

        dynRangeSlider.setVisible (false);

        return;
    }

    const auto settings = readBand (state, currentBand);

    gainKnob.setEnabled (usesGain (settings.type));
    gainKnob.getSlider().setEnabled (usesGain (settings.type));
    invertButton.setEnabled (usesGain (settings.type));

    qKnob.setEnabled (usesQ (settings.type));
    qKnob.getSlider().setEnabled (usesQ (settings.type));

    slopeBox.setEnabled (isCut (settings.type));

    soloButton.setToggleState (processor.getSoloBand() == currentBand, juce::dontSendNotification);

    // The dynamic section only means anything on a shape that has a gain: there
    // is nothing for a notch or an all pass to move towards.
    const auto canBeDynamic = usesGain (settings.type);
    dynamicButton.setEnabled (canBeDynamic);

    const auto dynamicLive = canBeDynamic && settings.dynamic;

    // When the dynamic section is off it disappears rather than sitting there
    // greyed out. Five dimmed controls saying nothing is more visual noise than
    // one line of text saying the same thing.
    const std::array<juce::Component*, 6> dynamicControls {
        &dynamicModeBox, &dynRangeSlider, &thresholdKnob, &ratioKnob, &attackKnob, &releaseKnob
    };

    for (auto* c : dynamicControls)
        c->setVisible (dynamicLive);

    dynamicHint.setVisible (! dynamicLive);
    dynamicHint.setText (canBeDynamic ? "Off. The band sits at its gain."
                                      : "Not available on this filter shape.",
                         juce::dontSendNotification);

    gainReductionDb = dynamicLive
                    ? processor.getCurrentGainDb (currentBand)
                    : 0.0f;

    noteLabel.setText (format::noteName (settings.frequency), juce::dontSendNotification);
}

void BandPanel::timerCallback()
{
    if (currentBand >= 0)
        updateEnablement();
}

void BandPanel::paint (juce::Graphics& g)
{
    g.setColour (colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), metrics::cornerRadius);

    if (currentBand < 0)
    {
        g.setColour (colours::textFaint);
        g.setFont (fonts::regular (12.5f));
        g.drawText ("No band selected", getLocalBounds(), juce::Justification::centred, false);
        return;
    }

    // A stripe down the left edge in the band's colour, so which band you are
    // editing is readable from the corner of the eye rather than from a number.
    const auto colour = colours::forBand (currentBand);
    g.setColour (colour);
    g.fillRoundedRectangle (getLocalBounds().toFloat().withWidth (3.0f)
                              .withTrimmedTop (6.0f).withTrimmedBottom (6.0f), 1.5f);

    auto badge = getLocalBounds().reduced (10).removeFromLeft (24).removeFromTop (22);
    g.setColour (colour);
    g.fillRoundedRectangle (badge.toFloat(), 3.0f);
    g.setColour (colours::background);
    g.setFont (fonts::bold (12.0f));
    g.drawText (juce::String (currentBand + 1), badge, juce::Justification::centred, false);

    // Gain reduction, next to the DYN button, only while it is doing something.
    if (! dynamicButton.getToggleState() || std::abs (gainReductionDb) < 0.05f)
        return;

    // To the right of the mode box, not of the DYN button: the button's immediate
    // right is where the mode box lives, and a child component paints over this.
    auto meter = juce::Rectangle<float> ((float) dynamicModeBox.getRight() + 10.0f,
                                         (float) dynamicButton.getY() + 4.0f, 92.0f, 16.0f);

    g.setFont (fonts::mono (11.0f));
    g.setColour (colour);
    g.drawText (format::decibels (gainReductionDb), meter,
                juce::Justification::centredLeft, false);
}

void BandPanel::resized()
{
    auto full = getLocalBounds().reduced (10);

    // The panel is two rows: what the filter is, and when it moves.
    constexpr int filterRowHeight = 81;
    constexpr int noteRowHeight = 13;

    auto bounds = full.removeFromTop (filterRowHeight);
    full.removeFromTop (6);
    auto dynamics = full;

    // Left column: badge, the three action buttons, then type and slope.
    auto left = bounds.removeFromLeft (260);
    left.removeFromLeft (30);   // badge painted in paint()

    auto buttonRow = left.removeFromTop (22);
    enableButton.setBounds (buttonRow.removeFromLeft (50));
    buttonRow.removeFromLeft (4);
    soloButton.setBounds (buttonRow.removeFromLeft (54));
    buttonRow.removeFromLeft (4);
    invertButton.setBounds (buttonRow.removeFromLeft (46));

    left.removeFromTop (6);

    // Filter type and stereo placement sit side by side; the slope goes
    // underneath because it only applies to cuts.
    auto typeRow = left.removeFromTop (24);
    channelBox.setBounds (typeRow.removeFromRight (92));
    typeRow.removeFromRight (6);
    typeBox.setBounds (typeRow);

    left.removeFromTop (5);
    slopeBox.setBounds (left.removeFromLeft (132).withHeight (24));

    bounds.removeFromLeft (12);

    // The note name gets its own strip at the bottom of the row rather than
    // hanging off the knob, which used to overlap the row underneath.
    auto noteRow = bounds.removeFromBottom (noteRowHeight);

    const auto knobWidth = juce::jmin (78, juce::jmax (40, bounds.getWidth() / 3));
    freqKnob.setBounds (bounds.removeFromLeft (knobWidth));
    gainKnob.setBounds (bounds.removeFromLeft (knobWidth));
    qKnob.setBounds (bounds.removeFromLeft (knobWidth));

    noteLabel.setBounds (noteRow.removeFromLeft (knobWidth));

    // Second row: the dynamic section.
    auto dynLeft = dynamics.removeFromLeft (260);
    dynLeft.removeFromLeft (30);

    // Centred in the dynamics row so DYN lines up with the middle of the knobs
    // beside it rather than sitting down at the level of their value readouts.
    auto dynRow = dynLeft.withSizeKeepingCentre (dynLeft.getWidth(), 24);
    dynamicButton.setBounds (dynRow.removeFromLeft (50));
    dynRow.removeFromLeft (6);
    dynamicModeBox.setBounds (dynRow.removeFromLeft (92));

    // El recorrido y el aviso ocupan el mismo sitio: el aviso solo existe
    // mientras la dinamica esta apagada, y entonces no hay recorrido que poner.
    dynamicHint.setBounds (dynRow.withWidth (240));
    dynRangeSlider.setBounds (dynRow.removeFromLeft (juce::jmax (96, dynRow.getWidth()))
                                   .withSizeKeepingCentre (juce::jmax (96, dynRow.getWidth()), 20));

    dynamics.removeFromLeft (12);

    const auto dynKnobWidth = juce::jmin (78, juce::jmax (40, dynamics.getWidth() / 4));
    thresholdKnob.setBounds (dynamics.removeFromLeft (dynKnobWidth));
    ratioKnob.setBounds (dynamics.removeFromLeft (dynKnobWidth));
    attackKnob.setBounds (dynamics.removeFromLeft (dynKnobWidth));
    releaseKnob.setBounds (dynamics.removeFromLeft (dynKnobWidth));
}
} // namespace nodo::eq
