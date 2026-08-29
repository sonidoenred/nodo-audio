#include "PluginEditor.h"

namespace nodo::ess
{
using namespace nodo::theme;

namespace
{
    const juce::Identifier editorWidthProperty  { "editorWidth" };
    const juce::Identifier editorHeightProperty { "editorHeight" };

    constexpr int defaultWidth = 880;
    constexpr int defaultHeight = 520;
    constexpr int footerHeight = 168;
}

NodoEssEditor::NodoEssEditor (NodoEssProcessor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse),
      processor (processorToUse),
      header ("ESS", processorToUse.getPresetManager()),
      display (processorToUse)
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (header);
    addAndMakeVisible (display);

    auto& state = processor.getState();

    frequencyKnob.attachTo   (state, ids::frequency);
    detectorTopKnob.attachTo (state, ids::detectorTop);
    thresholdKnob.attachTo   (state, ids::threshold);
    rangeKnob.attachTo       (state, ids::range);
    attackKnob.attachTo      (state, ids::attack);
    releaseKnob.attachTo     (state, ids::release);

    for (auto* knob : { &frequencyKnob, &detectorTopKnob, &thresholdKnob,
                        &rangeKnob, &attackKnob, &releaseKnob })
        addAndMakeVisible (knob);

    kneeSlider.attachTo      (state, ids::knee);
    lookaheadSlider.attachTo (state, ids::lookahead);
    linkSlider.attachTo      (state, ids::stereoLink);

    for (auto* slim : { &kneeSlider, &lookaheadSlider, &linkSlider })
        addAndMakeVisible (slim);

    frequencyKnob.getSlider().setTooltip (
        "Where the crossover sits. Sibilance is usually between 5 and 9 kHz; "
        "use Listen band to find it on this voice rather than guessing.");
    rangeKnob.getSlider().setTooltip (
        "The most it may take off. This is the control that stops a de-esser "
        "turning an S into a lisp: set it to 6 dB and it cannot do more.");
    thresholdKnob.getSlider().setTooltip (
        "In adaptive mode this is how far above the band's own running level "
        "the threshold sits, so the same setting works on a quiet take and a "
        "loud one. The line on the graph shows where it ended up.");
    detectorTopKnob.getSlider().setTooltip (
        "The top of what the detector listens to. Sibilance is a band, not "
        "everything above a corner: bring this down to about 12 kHz and cymbals "
        "or room air stop dragging the detector along with the S. Detection "
        "only - the sound never goes through this filter.");

    modeBox.addItemList (modeNames(), 1);
    modeAttachment = std::make_unique<ComboAttachment> (state, ids::mode, modeBox);
    attackKnob.getSlider().setTooltip (
        "How fast the reduction arrives. A de-esser that is late lets the front of "
        "the ess through, which is the part that hurts.");
    releaseKnob.getSlider().setTooltip (
        "How fast it lets go. Long enough to not chatter inside a word, short enough "
        "to be gone before the next vowel.");

    modeBox.setTooltip ("Split band turns down only the top; wideband turns down everything "
                        "for as long as the S lasts. Split keeps the body, wideband keeps "
                        "the tone. Neither is free.");
    addAndMakeVisible (modeBox);

    detectionBox.addItemList (detectionNames(), 1);
    detectionAttachment = std::make_unique<ComboAttachment> (state, ids::detection, detectionBox);
    addAndMakeVisible (detectionBox);

    channelBox.addItemList (channelNames(), 1);
    channelAttachment = std::make_unique<ComboAttachment> (state, ids::channel, channelBox);
    channelBox.setTooltip ("Mid only de-esses the voice and leaves the reverb and the doubles "
                           "in the sides alone. Side only is for when the sibilance is coming "
                           "from the widened doubles rather than from the voice.");
    addAndMakeVisible (channelBox);

    /*  Stereo link is about two sides of one image, so it means nothing once
        the two working channels are mid and side — those are different signals,
        not two halves of the same one. The control goes quiet rather than
        staying live and doing nothing.
    */
    if (auto* channelParameter = state.getParameter (ids::channel))
    {
        channelWatcher = std::make_unique<juce::ParameterAttachment> (
            *channelParameter,
            [this] (float value)
            {
                linkSlider.setEnabled (value < 0.5f);
                linkSlider.repaint();
            },
            nullptr);

        channelWatcher->sendInitialUpdate();
    }

    monitorBox.addItemList (monitorNames(), 1);
    monitorAttachment = std::make_unique<ComboAttachment> (state, ids::monitor, monitorBox);
    monitorBox.setTooltip ("Listen band plays what the detector hears. Listen difference plays "
                           "what is being removed - if you hear words in it, it is doing too much.");
    addAndMakeVisible (monitorBox);

    bypassAttachment = std::make_unique<ButtonAttachment> (state, ids::bypass, header.getBypassButton());

    header.onSlotSelected = [this] (bool isB) { processor.switchToSlot (isB); };
    header.onCopyToOtherSlot = [this] { processor.copyCurrentSlotToOther(); };
    header.setLevelSource ([this] (int index) { return processor.getMeterLevel (index); });
    header.setSlot (processor.isSlotB());

    constrainer.setSizeLimits (780, 461, 1700, 1004);
    constrainer.setFixedAspectRatio ((double) defaultWidth / (double) defaultHeight);
    setConstrainer (&constrainer);
    setResizable (true, true);

    const auto width  = (int) state.state.getProperty (editorWidthProperty, defaultWidth);
    const auto height = (int) state.state.getProperty (editorHeightProperty, defaultHeight);
    setSize (juce::jlimit (780, 1700, width), juce::jlimit (461, 1004, height));
}

NodoEssEditor::~NodoEssEditor()
{
    storeEditorSize();
    setLookAndFeel (nullptr);
}

void NodoEssEditor::storeEditorSize()
{
    auto& tree = processor.getState().state;
    tree.setProperty (editorWidthProperty, getWidth(), nullptr);
    tree.setProperty (editorHeightProperty, getHeight(), nullptr);
}

void NodoEssEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    g.setFont (fonts::bold (8.5f));
    g.setColour (colours::textFaint);
    g.drawText ("DE-ESSING", deessingCaption, juce::Justification::centredLeft, false);
    g.drawText ("SETTINGS", settingsCaption, juce::Justification::centredLeft, false);

    g.setColour (colours::grid);
    g.fillRect (groupDivider);
}

void NodoEssEditor::resized()
{
    auto bounds = getLocalBounds();

    header.setBounds (bounds.removeFromTop ((int) metrics::headerHeight));

    auto footer = bounds.removeFromBottom (footerHeight);
    display.setBounds (bounds.reduced ((int) metrics::padding, 6));

    footer.reduce ((int) metrics::padding, 8);

    constexpr int captionHeight = 11;
    constexpr int boxHeight = 24;
    constexpr int slimHeight = 20;

    auto right = footer.removeFromRight (juce::jmax (220, footer.getWidth() / 3));

    settingsCaption = right.removeFromTop (captionHeight);
    right.removeFromTop (3);

    auto topRow = right.removeFromTop (boxHeight);
    modeBox.setBounds (topRow.removeFromLeft (topRow.getWidth() / 2 - 3));
    topRow.removeFromLeft (6);
    detectionBox.setBounds (topRow);

    right.removeFromTop (5);

    auto secondRow = right.removeFromTop (boxHeight);
    channelBox.setBounds (secondRow.removeFromLeft (secondRow.getWidth() / 2 - 3));
    secondRow.removeFromLeft (6);
    monitorBox.setBounds (secondRow);

    right.removeFromTop (8);

    kneeSlider.setBounds (right.removeFromTop (slimHeight));
    right.removeFromTop (4);
    lookaheadSlider.setBounds (right.removeFromTop (slimHeight));
    right.removeFromTop (4);
    linkSlider.setBounds (right.removeFromTop (slimHeight));

    footer.removeFromRight (10);
    groupDivider = footer.removeFromRight (1).withTrimmedTop (2).withTrimmedBottom (10);
    footer.removeFromRight (12);

    // Caption over the knobs like every other group in the suite.
    deessingCaption = footer.removeFromTop (captionHeight);
    footer.removeFromTop (3);

    const auto knobWidth = juce::jmax (58, footer.getWidth() / 6);

    for (auto* knob : { &frequencyKnob, &detectorTopKnob, &thresholdKnob,
                        &rangeKnob, &attackKnob, &releaseKnob })
        knob->setBounds (footer.removeFromLeft (knobWidth).withHeight (juce::jmin (footer.getHeight(), 104)));

    storeEditorSize();
}
} // namespace nodo::ess
