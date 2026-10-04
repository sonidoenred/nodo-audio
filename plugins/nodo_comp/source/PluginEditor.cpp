#include "PluginEditor.h"

namespace nodo::comp
{
using namespace nodo::theme;

namespace
{
    const juce::Identifier editorWidthProperty  { "editorWidth" };
    const juce::Identifier editorHeightProperty { "editorHeight" };

    constexpr int defaultWidth = 1000;
    constexpr int defaultHeight = 660;
    constexpr int footerHeight = 236;
}

NodoCompEditor::NodoCompEditor (NodoCompProcessor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse),
      processor (processorToUse),
      header ("COMP", processorToUse.getPresetManager()),
      display (processorToUse)
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (header);
    addAndMakeVisible (display);

    auto& state = processor.getState();

    thresholdKnob.attachTo (state, ids::threshold);
    ratioKnob.attachTo     (state, ids::ratio);
    attackKnob.attachTo    (state, ids::attack);
    releaseKnob.attachTo   (state, ids::release);
    makeupKnob.attachTo    (state, ids::makeup);
    mixKnob.attachTo       (state, ids::mix);
    linkKnob.attachTo      (state, ids::stereoLink);
    scHighpassKnob.attachTo (state, ids::scHighpass);
    scLowpassKnob.attachTo  (state, ids::scLowpass);

    for (auto* knob : { &thresholdKnob, &ratioKnob, &attackKnob, &releaseKnob,
                        &makeupKnob, &mixKnob, &linkKnob,
                        &scHighpassKnob, &scLowpassKnob })
        addAndMakeVisible (knob);

    kneeSlider.attachTo      (state, ids::knee);
    rangeSlider.attachTo     (state, ids::range);
    lookaheadSlider.attachTo (state, ids::lookahead);
    holdSlider.attachTo      (state, ids::hold);

    for (auto* slim : { &kneeSlider, &rangeSlider, &lookaheadSlider, &holdSlider })
        addAndMakeVisible (slim);

    lookaheadSlider.getSlider().setTooltip (
        "Delays the audio so the gain can move before the peak arrives. "
        "Adds latency, which the host compensates.");
    holdSlider.getSlider().setTooltip (
        "Keeps the gain where it is before letting go. Stops the chatter you get "
        "when a signal dips between words or between hits.");
    rangeSlider.getSlider().setTooltip (
        "The most the compressor may move the gain. Essential in Upward mode, "
        "where an unbounded gain computer will happily lift the noise floor.");
    linkKnob.getSlider().setTooltip (
        "0 % is two independent compressors, 100 % is one gain for both channels. "
        "Anything below 100 % lets a loud side pull the image.");
    mixKnob.getSlider().setTooltip (
        "Blends the compressed signal with the dry one. Parallel compression "
        "without a second track.");

    // Styles as buttons rather than a menu: nine is still few enough to show,
    // and the difference between them is the kind of thing you audition by
    // clicking rather than by reading.
    const auto names = styleNames();

    for (int i = 0; i < numStyles; ++i)
    {
        auto& button = styleButtons[(size_t) i];

        button.setButtonText (names[i]);
        button.setClickingTogglesState (false);
        button.setColour (juce::TextButton::buttonColourId, colours::panelRaised);
        button.setColour (juce::TextButton::buttonOnColourId, colours::accent());
        button.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        button.setTooltip (styleDescription ((CompStyle) i));

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

    /*  The index comes from the attachment rather than being read back out of
        the tree. setValueNotifyingHost() updates the parameter object first and
        the APVTS's cached value afterwards, so a callback that re-read the tree
        would see the *previous* style and leave the buttons showing the wrong
        one — the DSP would change and the interface would not.
    */
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

    detectionBox.addItemList (detectionNames(), 1);
    detectionAttachment = std::make_unique<ComboAttachment> (state, ids::detection, detectionBox);
    detectionBox.setTooltip ("Peak reacts to transients, RMS reacts to loudness.");
    addAndMakeVisible (detectionBox);

    directionBox.addItemList (directionNames(), 1);
    directionAttachment = std::make_unique<ComboAttachment> (state, ids::direction, directionBox);
    directionBox.setTooltip ("Downward turns loud material down. Upward lifts what falls "
                             "below the threshold - use Range with it.");
    addAndMakeVisible (directionBox);

    sidechainBox.addItemList (sidechainSourceNames(), 1);
    sidechainAttachment = std::make_unique<ComboAttachment> (state, ids::scSource, sidechainBox);
    sidechainBox.onChange = [this] { refreshSidechainState(); };
    addAndMakeVisible (sidechainBox);

    oversamplingBox.addItemList ({ "OS Off", "OS 2x" }, 1);
    oversamplingAttachment = std::make_unique<ComboAttachment> (state, ids::oversampling, oversamplingBox);
    addAndMakeVisible (oversamplingBox);

    auto styleToggle = [this] (juce::TextButton& b, juce::Colour on)
    {
        b.setClickingTogglesState (true);
        b.setColour (juce::TextButton::buttonColourId, colours::panelRaised);
        b.setColour (juce::TextButton::buttonOnColourId, on);
        b.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        addAndMakeVisible (b);
    };

    styleToggle (listenButton, colours::warning);
    styleToggle (autoThresholdButton, colours::accent());
    styleToggle (autoReleaseButton, colours::accent());
    styleToggle (autoGainButton, colours::accent());

    listenAttachment  = std::make_unique<ButtonAttachment> (state, ids::scListen, listenButton);
    autoGainAttachment = std::make_unique<ButtonAttachment> (state, ids::autoGain, autoGainButton);
    autoThresholdAttachment = std::make_unique<ButtonAttachment> (state, ids::autoThreshold, autoThresholdButton);
    autoReleaseAttachment = std::make_unique<ButtonAttachment> (state, ids::autoRelease, autoReleaseButton);

    listenButton.setTooltip ("Hear what the detector hears, filters and all.");
    autoThresholdButton.setTooltip ("The threshold follows the running level of the material, "
                                    "so the same setting works on a quiet take and a loud one. "
                                    "The knob becomes an offset from that level.");
    autoReleaseButton.setTooltip ("The release adapts to how deep the reduction is instead of "
                                  "obeying the knob exactly.");
    autoGainButton.setTooltip ("Gives back the average reduction, slowly.");

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

    constrainer.setSizeLimits (860, 568, 1900, 1254);
    constrainer.setFixedAspectRatio ((double) defaultWidth / (double) defaultHeight);
    setConstrainer (&constrainer);
    setResizable (true, true);

    const auto width  = (int) state.state.getProperty (editorWidthProperty, defaultWidth);
    const auto height = (int) state.state.getProperty (editorHeightProperty, defaultHeight);
    setSize (juce::jlimit (860, 1900, width), juce::jlimit (568, 1254, height));
}

NodoCompEditor::~NodoCompEditor()
{
    storeEditorSize();
    setLookAndFeel (nullptr);
}

void NodoCompEditor::storeEditorSize()
{
    auto& tree = processor.getState().state;
    tree.setProperty (editorWidthProperty, getWidth(), nullptr);
    tree.setProperty (editorHeightProperty, getHeight(), nullptr);
}

void NodoCompEditor::refreshStyleButtons (int styleIndex)
{
    const auto current = (CompStyle) juce::jlimit (0, numStyles - 1, styleIndex);

    for (int i = 0; i < numStyles; ++i)
        styleButtons[(size_t) i].setToggleState ((CompStyle) i == current, juce::dontSendNotification);

    styleHint.setText (styleDescription (current), juce::dontSendNotification);
}

void NodoCompEditor::refreshSidechainState()
{
    const auto external = processor.getSettings().sidechain == SidechainSource::external;

    // A user who selects External and hears nothing change deserves to be told
    // why, rather than concluding the plugin is broken.
    sidechainNote = external && ! processor.isSidechainConnected()
                  ? juce::String ("Nothing routed to the sidechain input")
                  : juce::String();

    repaint();
}

void NodoCompEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    g.setFont (fonts::bold (8.5f));
    g.setColour (colours::textFaint);
    g.drawText ("DETECTOR", detectorCaption, juce::Justification::centredLeft, false);
    g.drawText ("SIDECHAIN", sidechainCaption, juce::Justification::centredLeft, false);
    g.drawText ("OUTPUT", outputCaption, juce::Justification::centredLeft, false);
    g.drawText ("COMPRESSION", compressionCaption, juce::Justification::centredLeft, false);
    g.drawText ("AUTO", autoCaption, juce::Justification::centredRight, false);

    g.setColour (colours::grid);
    g.fillRect (groupDivider);

    if (sidechainNote.isNotEmpty())
    {
        g.setFont (fonts::regular (10.5f));
        g.setColour (colours::warning);
        g.drawText (sidechainNote,
                    sidechainCaption.withY (sidechainCaption.getY() - 1).withWidth (240),
                    juce::Justification::centredRight, false);
    }
}

void NodoCompEditor::resized()
{
    auto bounds = getLocalBounds();

    header.setBounds (bounds.removeFromTop ((int) metrics::headerHeight));

    auto footer = bounds.removeFromBottom (footerHeight);
    display.setBounds (bounds.reduced ((int) metrics::padding, 6));

    footer.reduce ((int) metrics::padding, 8);

    // ---- style row -------------------------------------------------------
    // Oversampling lives up here rather than with the output controls: it
    // changes how the compressor works, not how loud it is.
    auto styleRow = footer.removeFromTop (22);
    oversamplingBox.setBounds (styleRow.removeFromRight (86));
    styleRow.removeFromRight (10);

    const auto styleWidth = juce::jmax (48, (styleRow.getWidth() - 8 * 3) / numStyles);

    for (auto& button : styleButtons)
    {
        button.setBounds (styleRow.removeFromLeft (styleWidth));
        styleRow.removeFromLeft (3);
    }

    styleHint.setBounds (footer.removeFromTop (14));
    footer.removeFromTop (8);

    // ---- right hand groups ------------------------------------------------
    auto right = footer.removeFromRight (juce::jmax (380, footer.getWidth() * 4 / 9));

    auto outputColumn = right.removeFromRight (104);
    right.removeFromRight (8);
    groupDivider = right.removeFromRight (1).withTrimmedTop (2).withTrimmedBottom (40);
    right.removeFromRight (10);

    constexpr int captionHeight = 11;
    constexpr int boxHeight = 24;
    constexpr int knobHeight = 76;

    auto detectorColumn = right.removeFromLeft (juce::jmax (100, right.getWidth() * 2 / 5));
    right.removeFromLeft (8);
    auto sidechainColumn = right;

    detectorCaption = detectorColumn.removeFromTop (captionHeight);
    detectorColumn.removeFromTop (3);
    detectionBox.setBounds (detectorColumn.removeFromTop (boxHeight));
    detectorColumn.removeFromTop (4);
    directionBox.setBounds (detectorColumn.removeFromTop (boxHeight));
    detectorColumn.removeFromTop (4);
    linkKnob.setBounds (detectorColumn.removeFromTop (knobHeight));

    sidechainCaption = sidechainColumn.removeFromTop (captionHeight);
    sidechainColumn.removeFromTop (3);

    auto scTopRow = sidechainColumn.removeFromTop (boxHeight);
    listenButton.setBounds (scTopRow.removeFromRight (58));
    scTopRow.removeFromRight (4);
    sidechainBox.setBounds (scTopRow);

    sidechainColumn.removeFromTop (4 + boxHeight + 4);
    auto scKnobRow = sidechainColumn.removeFromTop (knobHeight);
    scHighpassKnob.setBounds (scKnobRow.removeFromLeft (scKnobRow.getWidth() / 2));
    scLowpassKnob.setBounds (scKnobRow);

    outputCaption = outputColumn.removeFromTop (captionHeight);
    outputColumn.removeFromTop (3 + boxHeight + 4);
    auto outputKnobs = outputColumn.removeFromTop (knobHeight);
    makeupKnob.setBounds (outputKnobs.removeFromLeft (outputKnobs.getWidth() / 2));
    mixKnob.setBounds (outputKnobs);

    footer.removeFromRight (12);

    // ---- main knobs and the thin rows underneath --------------------------
    // The group gets a caption like every other group in the suite: without it
    // the four knobs that matter most are the only block on screen with no name
    // over them.
    compressionCaption = footer.removeFromTop (captionHeight);
    footer.removeFromTop (3);

    auto knobRow = footer.removeFromTop (juce::jmax (86, knobHeight + 28));

    const auto knobWidth = juce::jmax (64, knobRow.getWidth() / 4);

    for (auto* knob : { &thresholdKnob, &ratioKnob, &attackKnob, &releaseKnob })
        knob->setBounds (knobRow.removeFromLeft (knobWidth));

    footer.removeFromTop (4);

    /*  Two rows of two rather than four in a line. Four across left each row
        64 px at the minimum editor size, which is narrower than "LOOKAHEAD"
        plus its value, so the captions vanished exactly when the window was
        small enough that you needed them.
    */
    auto slimRow = footer.removeFromTop (20);

    // The three AUTO switches sit at the end of the first row: they belong to
    // three different controls, so putting them inside any one group would be a
    // lie about what they do.
    auto autoArea = slimRow.removeFromRight (168);
    autoCaption = autoArea.removeFromLeft (34);
    autoArea.removeFromLeft (4);

    const auto autoWidth = juce::jmax (30, (autoArea.getWidth() - 8) / 3);

    for (auto* button : { &autoThresholdButton, &autoReleaseButton, &autoGainButton })
    {
        button->setBounds (autoArea.removeFromLeft (autoWidth));
        autoArea.removeFromLeft (4);
    }

    slimRow.removeFromRight (12);

    footer.removeFromTop (4);
    auto secondSlimRow = footer.removeFromTop (20).withWidth (slimRow.getWidth());

    const auto slimWidth = juce::jmax (56, (slimRow.getWidth() - 6) / 2);

    kneeSlider.setBounds       (slimRow.removeFromLeft (slimWidth));
    slimRow.removeFromLeft (6);
    rangeSlider.setBounds      (slimRow.removeFromLeft (slimWidth));

    lookaheadSlider.setBounds  (secondSlimRow.removeFromLeft (slimWidth));
    secondSlimRow.removeFromLeft (6);
    holdSlider.setBounds       (secondSlimRow.removeFromLeft (slimWidth));

    storeEditorSize();
}
} // namespace nodo::comp
