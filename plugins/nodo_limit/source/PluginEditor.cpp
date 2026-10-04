#include "PluginEditor.h"

namespace nodo::limit
{
using namespace nodo::theme;

namespace
{
    const juce::Identifier editorWidthProperty  { "editorWidth" };
    const juce::Identifier editorHeightProperty { "editorHeight" };

    constexpr int defaultWidth = 940;
    constexpr int defaultHeight = 600;
    constexpr int footerHeight = 190;
}

NodoLimitEditor::NodoLimitEditor (NodoLimitProcessor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse),
      processor (processorToUse),
      header ("LIMIT", processorToUse.getPresetManager()),
      display (processorToUse)
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (header);
    addAndMakeVisible (display);

    auto& state = processor.getState();

    gainKnob.attachTo    (state, ids::inputGain);
    ceilingKnob.attachTo (state, ids::ceiling);
    releaseKnob.attachTo (state, ids::release);

    for (auto* knob : { &gainKnob, &ceilingKnob, &releaseKnob })
        addAndMakeVisible (knob);

    lookaheadSlider.attachTo     (state, ids::lookahead);
    transientLinkSlider.attachTo (state, ids::transientLink);
    releaseLinkSlider.attachTo   (state, ids::releaseLink);

    for (auto* slim : { &lookaheadSlider, &transientLinkSlider, &releaseLinkSlider })
        addAndMakeVisible (slim);

    gainKnob.getSlider().setTooltip (
        "How hard the signal is pushed into the ceiling. This is the loudness "
        "control: the ceiling stays where it is and the music comes up to meet it.");
    lookaheadSlider.getSlider().setTooltip (
        "How far ahead the limiter looks. Longer is smoother and adds latency, "
        "which the host compensates.");
    transientLinkSlider.getSlider().setTooltip (
        "How much a peak on one channel pulls the other down with it. Unlinking "
        "lets each side breathe, at the cost of the image moving.");

    const auto names = styleNames();

    for (int i = 0; i < numStyles; ++i)
    {
        auto& button = styleButtons[(size_t) i];

        button.setButtonText (names[i]);
        button.setClickingTogglesState (false);
        button.setColour (juce::TextButton::buttonColourId, colours::panelRaised);
        button.setColour (juce::TextButton::buttonOnColourId, colours::accent());
        button.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        button.setTooltip (styleDescription ((LimitStyle) i));

        button.onClick = [this, i]
        {
            if (auto* parameter = processor.getState().getParameter (ids::style))
            {
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) i));
                parameter->endChangeGesture();
            }
        };

        addAndMakeVisible (button);
    }

    // Same rule as the compressor: the index comes from the attachment, not
    // from re-reading the tree, which updates a beat later.
    if (auto* styleParameter = state.getParameter (ids::style))
    {
        styleAttachment = std::make_unique<juce::ParameterAttachment> (
            *styleParameter,
            [this] (float value) { refreshStyleButtons ((int) std::round (value)); },
            nullptr);

        styleAttachment->sendInitialUpdate();
    }

    styleHint.setJustificationType (juce::Justification::centredLeft);
    styleHint.setColour (juce::Label::textColourId, colours::textDim);
    styleHint.setFont (fonts::regular (11.5f));
    addAndMakeVisible (styleHint);

    auto styleToggle = [this] (juce::TextButton& b, juce::Colour on)
    {
        b.setClickingTogglesState (true);
        b.setColour (juce::TextButton::buttonColourId, colours::panelRaised);
        b.setColour (juce::TextButton::buttonOnColourId, on);
        b.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        addAndMakeVisible (b);
    };

    styleToggle (truePeakButton, colours::accent());
    styleToggle (dcButton, colours::accent());
    styleToggle (triggerButton, colours::accent());
    styleToggle (deltaButton, colours::warning);

    truePeakAttachment = std::make_unique<ButtonAttachment> (state, ids::truePeak, truePeakButton);
    dcAttachment       = std::make_unique<ButtonAttachment> (state, ids::dcFilter, dcButton);
    triggerAttachment  = std::make_unique<ButtonAttachment> (state, ids::externalTrigger, triggerButton);
    deltaAttachment    = std::make_unique<ButtonAttachment> (state, ids::delta, deltaButton);

    truePeakButton.setTooltip ("Measures peaks four times oversampled, the way a converter will "
                               "reconstruct them. Costs a little latency and stops your master "
                               "clipping on someone else's DAC.");
    dcButton.setTooltip ("A 12 Hz high pass. Removes an offset that would otherwise eat headroom.");
    triggerButton.setTooltip ("Limit from the sidechain input instead of from the signal itself.");
    deltaButton.setTooltip ("Listen to what the limiter is removing rather than what it keeps.");

    triggerButton.onClick = [this] { refreshSidechainState(); };

    ditherBox.addItemList (ditherNames(), 1);
    ditherAttachment = std::make_unique<ComboAttachment> (state, ids::dither, ditherBox);
    ditherBox.setTooltip ("Only for the last plugin before a fixed point file. "
                          "Dithering twice is worse than not dithering at all.");
    addAndMakeVisible (ditherBox);

    targetBox.addItemList (targetNames(), 1);
    targetAttachment = std::make_unique<ComboAttachment> (state, ids::target, targetBox);
    targetBox.setTooltip ("Shows how far the integrated loudness is from a target. "
                          "Advisory: it does not change the sound.");
    addAndMakeVisible (targetBox);

    applyTargetButton.setColour (juce::TextButton::buttonColourId, colours::panelRaised);
    applyTargetButton.onClick = [this] { processor.nudgeTowardsTarget(); };
    applyTargetButton.setTooltip ("Adds the distance to the target to the gain. One step: "
                                  "the limiter eats part of any increase, so it usually takes "
                                  "a second click to land.");
    addAndMakeVisible (applyTargetButton);

    styleToggle (matchButton, colours::accent());
    matchAttachment = std::make_unique<ButtonAttachment> (state, ids::bypassMatch, matchButton);
    matchButton.setTooltip ("Bypass at matched loudness, so comparing is about the sound "
                            "rather than about which one is louder.");

    bypassAttachment = std::make_unique<ButtonAttachment> (state, ids::bypass, header.getBypassButton());

    header.onSlotSelected = [this] (bool isB) { processor.switchToSlot (isB); };
    header.onCopyToOtherSlot = [this] { processor.copyCurrentSlotToOther(); };
    header.setLevelSource ([this] (int index) { return processor.getMeterLevel (index); });
    header.setRmsSource ([this] (int index) { return processor.getMeterRms (index); });
    header.setProblemReportSource ([this]
    {
        return nodo::buildProblemReport (processor, JucePlugin_VersionString);
    });
    header.setSlot (processor.isSlotB());

    refreshSidechainState();

    constrainer.setSizeLimits (820, 523, 1800, 1149);
    constrainer.setFixedAspectRatio ((double) defaultWidth / (double) defaultHeight);
    setConstrainer (&constrainer);
    setResizable (true, true);

    const auto width  = (int) state.state.getProperty (editorWidthProperty, defaultWidth);
    const auto height = (int) state.state.getProperty (editorHeightProperty, defaultHeight);
    setSize (juce::jlimit (820, 1800, width), juce::jlimit (523, 1149, height));
}

NodoLimitEditor::~NodoLimitEditor()
{
    storeEditorSize();
    setLookAndFeel (nullptr);
}

void NodoLimitEditor::storeEditorSize()
{
    auto& tree = processor.getState().state;
    tree.setProperty (editorWidthProperty, getWidth(), nullptr);
    tree.setProperty (editorHeightProperty, getHeight(), nullptr);
}

void NodoLimitEditor::refreshStyleButtons (int styleIndex)
{
    const auto current = (LimitStyle) juce::jlimit (0, numStyles - 1, styleIndex);

    for (int i = 0; i < numStyles; ++i)
        styleButtons[(size_t) i].setToggleState ((LimitStyle) i == current, juce::dontSendNotification);

    styleHint.setText (styleDescription (current), juce::dontSendNotification);
}

void NodoLimitEditor::refreshSidechainState()
{
    sidechainNote = processor.getSettings().externalTrigger && ! processor.isSidechainConnected()
                  ? juce::String ("Nothing routed to the sidechain input")
                  : juce::String();

    repaint();
}

void NodoLimitEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    g.setFont (fonts::bold (8.5f));
    g.setColour (colours::textFaint);
    g.drawText ("LIMITING", limitingCaption, juce::Justification::centredLeft, false);
    g.drawText ("STEREO LINK", linkCaption, juce::Justification::centredLeft, false);
    g.drawText ("OPTIONS", optionsCaption, juce::Justification::centredLeft, false);

    g.setColour (colours::grid);
    g.fillRect (groupDivider);

    if (sidechainNote.isNotEmpty())
    {
        g.setFont (fonts::regular (10.5f));
        g.setColour (colours::warning);
        g.drawText (sidechainNote, optionsCaption.withWidth (260),
                    juce::Justification::centredRight, false);
    }
}

void NodoLimitEditor::resized()
{
    auto bounds = getLocalBounds();

    header.setBounds (bounds.removeFromTop ((int) metrics::headerHeight));

    auto footer = bounds.removeFromBottom (footerHeight);
    display.setBounds (bounds.reduced ((int) metrics::padding, 6));

    footer.reduce ((int) metrics::padding, 8);

    // Style row, with the dither menu at the far right: both are decisions about
    // the file you are about to make rather than about the sound.
    auto styleRow = footer.removeFromTop (22);
    ditherBox.setBounds (styleRow.removeFromRight (104));
    styleRow.removeFromRight (6);
    applyTargetButton.setBounds (styleRow.removeFromRight (66));
    styleRow.removeFromRight (3);
    targetBox.setBounds (styleRow.removeFromRight (112));
    styleRow.removeFromRight (10);

    const auto styleWidth = juce::jmax (52, (styleRow.getWidth() - 4 * (numStyles - 1)) / numStyles);

    for (auto& button : styleButtons)
    {
        button.setBounds (styleRow.removeFromLeft (styleWidth));
        styleRow.removeFromLeft (4);
    }

    styleHint.setBounds (footer.removeFromTop (14));
    footer.removeFromTop (6);

    constexpr int captionHeight = 11;
    constexpr int slimHeight = 20;

    // Right block: linking above, the switches below.
    auto right = footer.removeFromRight (juce::jmax (260, footer.getWidth() / 3));

    linkCaption = right.removeFromTop (captionHeight);
    right.removeFromTop (3);
    transientLinkSlider.setBounds (right.removeFromTop (slimHeight));
    right.removeFromTop (4);
    releaseLinkSlider.setBounds (right.removeFromTop (slimHeight));
    right.removeFromTop (8);

    optionsCaption = right.removeFromTop (captionHeight);
    right.removeFromTop (3);

    auto switches = right.removeFromTop (24);
    const auto switchWidth = juce::jmax (40, (switches.getWidth() - 16) / 5);

    for (auto* button : { &truePeakButton, &dcButton, &triggerButton, &deltaButton, &matchButton })
    {
        button->setBounds (switches.removeFromLeft (switchWidth));
        switches.removeFromLeft (4);
    }

    footer.removeFromRight (10);
    groupDivider = footer.removeFromRight (1).withTrimmedTop (2).withTrimmedBottom (12);
    footer.removeFromRight (12);

    // Left block: the three knobs, with lookahead as a thin control under them.
    // Caption over them like every other group in the suite.
    limitingCaption = footer.removeFromTop (captionHeight);
    footer.removeFromTop (3);

    auto knobRow = footer.removeFromTop (juce::jmax (80, footer.getHeight() - slimHeight - 6));
    const auto knobWidth = juce::jmax (64, knobRow.getWidth() / 3);

    for (auto* knob : { &gainKnob, &ceilingKnob, &releaseKnob })
        knob->setBounds (knobRow.removeFromLeft (knobWidth));

    footer.removeFromTop (4);
    lookaheadSlider.setBounds (footer.removeFromTop (slimHeight)
                                     .withWidth (juce::jmax (140, knobWidth * 2)));

    storeEditorSize();
}
} // namespace nodo::limit
