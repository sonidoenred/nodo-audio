#include "PluginEditor.h"

namespace nodo::verb
{
using namespace nodo::theme;

namespace
{
    const juce::Identifier editorWidthProperty  { "editorWidth" };
    const juce::Identifier editorHeightProperty { "editorHeight" };

    constexpr int defaultWidth = 1000;
    constexpr int defaultHeight = 620;
    constexpr int footerHeight = 200;
}

NodoVerbEditor::NodoVerbEditor (NodoVerbProcessor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse),
      processor (processorToUse),
      header ("VERB", processorToUse.getPresetManager()),
      display (processorToUse)
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (header);
    addAndMakeVisible (display);

    auto& state = processor.getState();

    decayKnob.attachTo     (state, ids::decay);
    sizeKnob.attachTo      (state, ids::size);
    predelayKnob.attachTo  (state, ids::predelay);
    mixKnob.attachTo       (state, ids::mix);
    diffusionKnob.attachTo (state, ids::diffusion);
    motionKnob.attachTo    (state, ids::motion);
    preLowKnob.attachTo    (state, ids::preLowCut);
    preHighKnob.attachTo   (state, ids::preHighCut);
    duckKnob.attachTo      (state, ids::duckAmount);

    for (auto* knob : { &decayKnob, &sizeKnob, &predelayKnob, &mixKnob,
                        &diffusionKnob, &motionKnob, &preLowKnob, &preHighKnob, &duckKnob })
        addAndMakeVisible (knob);

    widthSlider.attachTo       (state, ids::width);
    duckAttackSlider.attachTo  (state, ids::duckAttack);
    duckReleaseSlider.attachTo (state, ids::duckRelease);
    postMidQSlider.attachTo    (state, ids::postMidQ);

    for (auto* slim : { &widthSlider, &duckAttackSlider, &duckReleaseSlider, &postMidQSlider })
        addAndMakeVisible (slim);

    decayKnob.getSlider().setTooltip (
        "How long the tail takes to fall 60 dB in the middle of the spectrum. "
        "The two ends are set by dragging the curve above, in seconds, and what "
        "it draws is what a measurement of the output gives back.");
    sizeKnob.getSlider().setTooltip (
        "Scales the room without touching the decay time, which is the opposite "
        "of how a real space behaves and exactly what makes a reverb useful: a "
        "small bright room with a long tail does not exist and is often what a "
        "mix wants.");
    diffusionKnob.getSlider().setTooltip (
        "How much the input is smeared before it reaches the network. Low and "
        "you hear the first repeats as repeats; high and the attack arrives as "
        "a cloud.");
    motionKnob.getSlider().setTooltip (
        "Slow movement of the delay lines. Some of it is not optional - eight "
        "fixed delays ring on eight fixed frequencies and the tail goes "
        "metallic - so this decides how much beyond that.");
    preLowKnob.getSlider().setTooltip (
        "Cuts the bottom before the reverb rather than after it, so the energy "
        "is never in the tail to begin with. This is the control that stops a "
        "long reverb turning a mix to mud.");
    duckKnob.getSlider().setTooltip (
        "Pulls the tail down while the source is playing and lets it back in "
        "the gaps. It is what a long reverb on a voice needs to not eat the "
        "words. The envelope reads the dry input, not the wet.");

    predelayKnob.getSlider().setTooltip (
        "Silence between the sound and its reverb. It is what makes a voice sit in "
        "front of a big space instead of inside it: the ear places the source by "
        "what arrives first and judges the room by what comes after.");
    mixKnob.getSlider().setTooltip (
        "How much reverb goes out. On a send this belongs at 100 %; on an insert it "
        "is the control that decides everything else.");
    preHighKnob.getSlider().setTooltip (
        "Cuts the top before the reverb. Rooms absorb high frequencies and this is "
        "the cheapest way to say how far away the walls are.");
    widthSlider.getSlider().setTooltip (
        "How far apart the two sides of the tail are. At zero the reverb is mono and "
        "sits in the middle with the source, which for a small room is right.");
    duckAttackSlider.getSlider().setTooltip (
        "How fast the tail gets out of the way. Short enough and the first word is "
        "already clear; too short and you hear the reverb flinch.");
    duckReleaseSlider.getSlider().setTooltip (
        "How fast it comes back in the gaps. This is the one that decides whether the "
        "ducking is heard as an effect or not heard at all.");
    postMidQSlider.getSlider().setTooltip (
        "How wide the middle band of the post EQ is. The band itself is dragged on the "
        "picture above.");

    freezeButton.setClickingTogglesState (true);
    freezeButton.setColour (juce::TextButton::buttonColourId, colours::panelRaised);
    freezeButton.setColour (juce::TextButton::buttonOnColourId, colours::warning);
    freezeButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    freezeButton.setTooltip ("Every line hands back exactly what it was given, so the tail holds "
                             "for ever - exactly for ever, not nearly. Nothing new gets in while "
                             "it is on.");
    addAndMakeVisible (freezeButton);

    freezeAttachment = std::make_unique<ButtonAttachment> (state, ids::freeze, freezeButton);
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

    constrainer.setSizeLimits (860, 533, 1900, 1178);
    constrainer.setFixedAspectRatio ((double) defaultWidth / (double) defaultHeight);
    setConstrainer (&constrainer);
    setResizable (true, true);

    const auto width  = (int) state.state.getProperty (editorWidthProperty, defaultWidth);
    const auto height = (int) state.state.getProperty (editorHeightProperty, defaultHeight);
    setSize (juce::jlimit (860, 1900, width), juce::jlimit (533, 1178, height));
}

NodoVerbEditor::~NodoVerbEditor()
{
    storeEditorSize();
    setLookAndFeel (nullptr);
}

void NodoVerbEditor::storeEditorSize()
{
    auto& tree = processor.getState().state;
    tree.setProperty (editorWidthProperty, getWidth(), nullptr);
    tree.setProperty (editorHeightProperty, getHeight(), nullptr);
}

void NodoVerbEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    g.setFont (fonts::bold (8.5f));
    g.setColour (colours::textFaint);
    g.drawText ("SPACE", spaceCaption, juce::Justification::centredLeft, false);
    g.drawText ("CHARACTER", characterCaption, juce::Justification::centredLeft, false);
    g.drawText ("INPUT", inputCaption, juce::Justification::centredLeft, false);
    g.drawText ("DUCKING", duckCaption, juce::Justification::centredLeft, false);

    g.setColour (colours::grid);
    g.fillRect (groupDivider);
}

void NodoVerbEditor::resized()
{
    auto bounds = getLocalBounds();

    header.setBounds (bounds.removeFromTop ((int) metrics::headerHeight));

    auto footer = bounds.removeFromBottom (footerHeight);
    display.setBounds (bounds.reduced ((int) metrics::padding, 6));

    footer.reduce ((int) metrics::padding, 8);

    constexpr int captionHeight = 11;
    constexpr int knobHeight = 78;

    auto topRow = footer.removeFromTop (22);
    freezeButton.setBounds (topRow.removeFromLeft (78));
    topRow.removeFromLeft (10);
    widthSlider.setBounds (topRow.removeFromLeft (150).withSizeKeepingCentre (150, 20));
    topRow.removeFromLeft (10);
    postMidQSlider.setBounds (topRow.removeFromLeft (150).withSizeKeepingCentre (150, 20));

    footer.removeFromTop (8);

    // ---- right hand groups ------------------------------------------------
    /*  The right hand side carries six knobs and two slim rows; the left hand
        side carries four knobs and nothing else. Giving the right more than half
        looks lopsided on paper and reads correctly on screen — the first version
        split it evenly and the two ducking rows came out 75 px wide, which is
        narrower than the word "RELEASE".
    */
    auto right = footer.removeFromRight (juce::jmax (520, footer.getWidth() * 5 / 9));

    auto duckColumn = right.removeFromRight (juce::jmax (190, right.getWidth() / 3));
    right.removeFromRight (8);
    groupDivider = right.removeFromRight (1).withTrimmedTop (2).withTrimmedBottom (10);
    right.removeFromRight (10);

    auto characterColumn = right.removeFromLeft (juce::jmax (140, right.getWidth() / 2));
    right.removeFromLeft (8);
    auto inputColumn = right;

    characterCaption = characterColumn.removeFromTop (captionHeight);
    characterColumn.removeFromTop (3);
    auto characterKnobs = characterColumn.removeFromTop (knobHeight);
    diffusionKnob.setBounds (characterKnobs.removeFromLeft (characterKnobs.getWidth() / 2));
    motionKnob.setBounds (characterKnobs);

    inputCaption = inputColumn.removeFromTop (captionHeight);
    inputColumn.removeFromTop (3);
    auto inputKnobs = inputColumn.removeFromTop (knobHeight);
    preLowKnob.setBounds (inputKnobs.removeFromLeft (inputKnobs.getWidth() / 2));
    preHighKnob.setBounds (inputKnobs);

    duckCaption = duckColumn.removeFromTop (captionHeight);
    duckColumn.removeFromTop (3);
    auto duckKnobs = duckColumn.removeFromTop (knobHeight);
    duckKnob.setBounds (duckKnobs.removeFromLeft (juce::jmax (70, duckKnobs.getWidth() / 2)));

    auto duckSlims = duckKnobs.reduced (0, 14);
    duckAttackSlider.setBounds (duckSlims.removeFromTop (20));
    duckSlims.removeFromTop (6);
    duckReleaseSlider.setBounds (duckSlims.removeFromTop (20));

    footer.removeFromRight (12);

    // ---- the four that get moved on every track ---------------------------
    spaceCaption = footer.removeFromTop (captionHeight);
    footer.removeFromTop (3);

    auto knobRow = footer.removeFromTop (knobHeight);
    const auto knobWidth = juce::jmax (64, knobRow.getWidth() / 4);

    for (auto* knob : { &decayKnob, &sizeKnob, &predelayKnob, &mixKnob })
        knob->setBounds (knobRow.removeFromLeft (knobWidth));

    storeEditorSize();
}
} // namespace nodo::verb
