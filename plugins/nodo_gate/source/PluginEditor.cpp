#include "PluginEditor.h"

namespace nodo::gate
{
using namespace nodo::theme;

namespace
{
    const juce::Identifier editorWidthProperty  { "editorWidth" };
    const juce::Identifier editorHeightProperty { "editorHeight" };

    constexpr int defaultWidth = 1000;
    constexpr int defaultHeight = 640;
    constexpr int footerHeight = 224;
}

NodoGateEditor::NodoGateEditor (NodoGateProcessor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse),
      processor (processorToUse),
      header ("GATE", processorToUse.getPresetManager()),
      display (processorToUse)
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (header);
    addAndMakeVisible (display);

    auto& state = processor.getState();

    thresholdKnob.attachTo  (state, ids::threshold);
    rangeKnob.attachTo      (state, ids::range);
    attackKnob.attachTo     (state, ids::attack);
    releaseKnob.attachTo    (state, ids::release);
    ratioKnob.attachTo      (state, ids::ratio);
    mixKnob.attachTo        (state, ids::mix);
    scHighpassKnob.attachTo (state, ids::scHighpass);
    scLowpassKnob.attachTo  (state, ids::scLowpass);

    for (auto* knob : { &thresholdKnob, &rangeKnob, &attackKnob, &releaseKnob,
                        &ratioKnob, &mixKnob, &scHighpassKnob, &scLowpassKnob })
        addAndMakeVisible (knob);

    holdSlider.attachTo       (state, ids::hold);
    hysteresisSlider.attachTo (state, ids::hysteresis);
    kneeSlider.attachTo       (state, ids::knee);
    lookaheadSlider.attachTo  (state, ids::lookahead);

    for (auto* slim : { &holdSlider, &hysteresisSlider, &kneeSlider, &lookaheadSlider })
        addAndMakeVisible (slim);

    thresholdKnob.getSlider().setTooltip (
        "Where the gate opens. Drag the dashed line on the graph instead if it "
        "is easier - that is the same control.");
    rangeKnob.getSlider().setTooltip (
        "The most the gate may take off. This is the control that makes a gate "
        "usable on anything with a room in it: closing all the way sounds like "
        "the tape stopping, and 12 dB is usually plenty.");
    ratioKnob.getSlider().setTooltip (
        "At the top it is a gate. Below that it is an expander: 2:1 takes a "
        "signal 10 dB under the threshold down another 10 dB, which lowers a "
        "noise floor without ever slamming shut.");
    holdSlider.getSlider().setTooltip (
        "How long the gate stays where it is after the signal falls away, "
        "before the release starts. The cheapest cure there is for chatter.");
    hysteresisSlider.getSlider().setTooltip (
        "How far below the opening threshold the level has to fall before the "
        "gate will close again. Without it, material sitting on the threshold "
        "opens and closes on every wobble. The shaded band on the graph is this.");
    lookaheadSlider.getSlider().setTooltip (
        "Delays the audio so the gate can be open before the transient arrives. "
        "This is what stops a fast gate clipping the front of a snare. Adds "
        "latency, which the host compensates.");
    mixKnob.getSlider().setTooltip (
        "Blends the gated signal with the dry one. At 50 % you get the shape "
        "without losing the room entirely.");

    // Styles as buttons rather than a menu: six is few enough to show, and the
    // difference between them is something you audition by clicking.
    const auto names = styleNames();

    for (int i = 0; i < numStyles; ++i)
    {
        auto& button = styleButtons[(size_t) i];

        button.setButtonText (names[i]);
        button.setClickingTogglesState (false);
        button.setColour (juce::TextButton::buttonColourId, colours::panelRaised);
        button.setColour (juce::TextButton::buttonOnColourId, colours::accent());
        button.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        button.setTooltip (styleDescription ((GateStyle) i));

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

    directionBox.addItemList (directionNames(), 1);
    directionAttachment = std::make_unique<ComboAttachment> (state, ids::direction, directionBox);
    directionBox.setTooltip ("Gate turns quiet material down. Ducking turns the signal down "
                             "while the detector is loud - with an external sidechain, that "
                             "is the voice-over duck.");
    addAndMakeVisible (directionBox);

    channelModeBox.addItemList (channelModeNames(), 1);
    channelModeAttachment = std::make_unique<ComboAttachment> (state, ids::channelMode, channelModeBox);
    channelModeBox.setTooltip ("Linked is one gate driven by whichever side is louder, and it is "
                               "what keeps a stereo image still. Two independent gates will open "
                               "at slightly different moments and the picture will move.");
    addAndMakeVisible (channelModeBox);

    triggerBox.addItemList (triggerNames(), 1);
    triggerAttachment = std::make_unique<ComboAttachment> (state, ids::trigger, triggerBox);
    triggerBox.setTooltip ("MIDI opens the gate from a note instead of from the detector. "
                           "Attack, hold, release and range still apply, so it sounds like the "
                           "same gate rather than like a switch.");
    addAndMakeVisible (triggerBox);

    /*  The threshold stops meaning anything under MIDI triggering, so the
        interface says so rather than leaving a live control that does nothing.
    */
    if (auto* triggerParameter = state.getParameter (ids::trigger))
    {
        triggerWatcher = std::make_unique<juce::ParameterAttachment> (
            *triggerParameter,
            [this] (float value) { refreshTriggerState ((int) std::round (value)); },
            nullptr);

        triggerWatcher->sendInitialUpdate();
    }

    sidechainBox.addItemList (sidechainSourceNames(), 1);
    sidechainAttachment = std::make_unique<ComboAttachment> (state, ids::scSource, sidechainBox);
    sidechainBox.onChange = [this] { refreshSidechainState(); };
    addAndMakeVisible (sidechainBox);

    listenButton.setClickingTogglesState (true);
    listenButton.setColour (juce::TextButton::buttonColourId, colours::panelRaised);
    listenButton.setColour (juce::TextButton::buttonOnColourId, colours::warning);
    listenButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    listenButton.setTooltip ("Hear what the detector hears, filters and all. On a gate this is "
                             "the fastest way to find a sidechain setting that separates the "
                             "hit from the spill.");
    addAndMakeVisible (listenButton);

    listenAttachment = std::make_unique<ButtonAttachment> (state, ids::scListen, listenButton);
    bypassAttachment = std::make_unique<ButtonAttachment> (state, ids::bypass, header.getBypassButton());

    header.onSlotSelected = [this] (bool isB) { processor.switchToSlot (isB); };
    header.onCopyToOtherSlot = [this] { processor.copyCurrentSlotToOther(); };
    header.setLevelSource ([this] (int index) { return processor.getMeterLevel (index); });
    header.setSlot (processor.isSlotB());

    refreshSidechainState();

    constrainer.setSizeLimits (860, 550, 1900, 1216);
    constrainer.setFixedAspectRatio ((double) defaultWidth / (double) defaultHeight);
    setConstrainer (&constrainer);
    setResizable (true, true);

    const auto width  = (int) state.state.getProperty (editorWidthProperty, defaultWidth);
    const auto height = (int) state.state.getProperty (editorHeightProperty, defaultHeight);
    setSize (juce::jlimit (860, 1900, width), juce::jlimit (550, 1216, height));
}

NodoGateEditor::~NodoGateEditor()
{
    storeEditorSize();
    setLookAndFeel (nullptr);
}

void NodoGateEditor::storeEditorSize()
{
    auto& tree = processor.getState().state;
    tree.setProperty (editorWidthProperty, getWidth(), nullptr);
    tree.setProperty (editorHeightProperty, getHeight(), nullptr);
}

void NodoGateEditor::refreshStyleButtons (int styleIndex)
{
    const auto current = (GateStyle) juce::jlimit (0, numStyles - 1, styleIndex);

    for (int i = 0; i < numStyles; ++i)
        styleButtons[(size_t) i].setToggleState ((GateStyle) i == current, juce::dontSendNotification);

    styleHint.setText (styleDescription (current), juce::dontSendNotification);
}

void NodoGateEditor::refreshTriggerState (int triggerIndex)
{
    const auto midi = (Trigger) triggerIndex == Trigger::midi;

    thresholdKnob.setEnabled (! midi);
    hysteresisSlider.setEnabled (! midi);
    hysteresisSlider.repaint();

    triggerNote = midi ? juce::String ("MIDI notes are opening the gate, not the threshold")
                       : juce::String();

    repaint();
}

void NodoGateEditor::refreshSidechainState()
{
    const auto external = processor.getSettings().sidechain == SidechainSource::external;

    // A user who selects External and hears nothing change deserves to be told
    // why, rather than concluding the plugin is broken.
    sidechainNote = external && ! processor.isSidechainConnected()
                  ? juce::String ("Nothing routed to the sidechain input")
                  : juce::String();

    repaint();
}

void NodoGateEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    g.setFont (fonts::bold (8.5f));
    g.setColour (colours::textFaint);
    g.drawText ("GATING", gatingCaption, juce::Justification::centredLeft, false);
    g.drawText ("DETECTOR", detectorCaption, juce::Justification::centredLeft, false);
    g.drawText ("SIDECHAIN", sidechainCaption, juce::Justification::centredLeft, false);
    g.drawText ("OUTPUT", outputCaption, juce::Justification::centredLeft, false);

    g.setColour (colours::grid);
    g.fillRect (groupDivider);

    if (sidechainNote.isNotEmpty())
    {
        g.setFont (fonts::regular (10.5f));
        g.setColour (colours::warning);
        g.drawText (sidechainNote,
                    sidechainCaption.withY (sidechainCaption.getY() - 1).withWidth (250),
                    juce::Justification::centredRight, false);
    }

    if (triggerNote.isNotEmpty())
    {
        /*  On the hint row rather than beside the DETECTOR caption: the caption
            row is already two columns wide and the note ran straight over the
            one next to it.
        */
        g.setFont (fonts::regular (11.0f));
        g.setColour (colours::warning);
        g.drawText (triggerNote, noteArea, juce::Justification::centredRight, false);
    }
}

void NodoGateEditor::resized()
{
    auto bounds = getLocalBounds();

    header.setBounds (bounds.removeFromTop ((int) metrics::headerHeight));

    auto footer = bounds.removeFromBottom (footerHeight);
    display.setBounds (bounds.reduced ((int) metrics::padding, 6));

    footer.reduce ((int) metrics::padding, 8);

    // ---- style row -------------------------------------------------------
    auto styleRow = footer.removeFromTop (22);

    const auto styleWidth = juce::jmax (56, (styleRow.getWidth() - 3 * (numStyles - 1)) / numStyles);

    for (auto& button : styleButtons)
    {
        button.setBounds (styleRow.removeFromLeft (styleWidth));
        styleRow.removeFromLeft (3);
    }

    auto hintRow = footer.removeFromTop (14);
    noteArea = hintRow;
    styleHint.setBounds (hintRow);
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

    auto detectorColumn = right.removeFromLeft (juce::jmax (110, right.getWidth() * 2 / 5));
    right.removeFromLeft (8);
    auto sidechainColumn = right;

    detectorCaption = detectorColumn.removeFromTop (captionHeight);
    detectorColumn.removeFromTop (3);
    directionBox.setBounds (detectorColumn.removeFromTop (boxHeight));
    detectorColumn.removeFromTop (4);
    channelModeBox.setBounds (detectorColumn.removeFromTop (boxHeight));
    detectorColumn.removeFromTop (4);
    ratioKnob.setBounds (detectorColumn.removeFromTop (knobHeight));

    sidechainCaption = sidechainColumn.removeFromTop (captionHeight);
    sidechainColumn.removeFromTop (3);

    auto scTopRow = sidechainColumn.removeFromTop (boxHeight);
    listenButton.setBounds (scTopRow.removeFromRight (58));
    scTopRow.removeFromRight (4);
    sidechainBox.setBounds (scTopRow);

    sidechainColumn.removeFromTop (4);
    triggerBox.setBounds (sidechainColumn.removeFromTop (boxHeight));
    sidechainColumn.removeFromTop (4);

    auto scKnobRow = sidechainColumn.removeFromTop (knobHeight);
    scHighpassKnob.setBounds (scKnobRow.removeFromLeft (scKnobRow.getWidth() / 2));
    scLowpassKnob.setBounds (scKnobRow);

    outputCaption = outputColumn.removeFromTop (captionHeight);
    outputColumn.removeFromTop (3 + boxHeight + 4 + boxHeight + 4);
    mixKnob.setBounds (outputColumn.removeFromTop (knobHeight));

    footer.removeFromRight (12);

    // ---- main knobs and the thin row underneath ---------------------------
    // Caption over them like every other group in the suite.
    gatingCaption = footer.removeFromTop (captionHeight);
    footer.removeFromTop (3);

    auto knobRow = footer.removeFromTop (juce::jmax (86, knobHeight + 28));

    const auto knobWidth = juce::jmax (64, knobRow.getWidth() / 4);

    for (auto* knob : { &thresholdKnob, &rangeKnob, &attackKnob, &releaseKnob })
        knob->setBounds (knobRow.removeFromLeft (knobWidth));

    footer.removeFromTop (4);

    auto slimRow = footer.removeFromTop (20);

    const auto slimWidth = juce::jmax (56, (slimRow.getWidth() - 18) / 4);

    for (auto* slim : { &holdSlider, &hysteresisSlider, &kneeSlider, &lookaheadSlider })
    {
        slim->setBounds (slimRow.removeFromLeft (slimWidth));
        slimRow.removeFromLeft (6);
    }

    storeEditorSize();
}
} // namespace nodo::gate
