#include "PluginEditor.h"

namespace nodo::delay
{
using namespace nodo::theme;

namespace
{
    const juce::Identifier editorWidthProperty  { "editorWidth" };
    const juce::Identifier editorHeightProperty { "editorHeight" };

    constexpr int defaultWidth = 1000;
    constexpr int defaultHeight = 640;
    constexpr int footerHeight = 228;
}

NodoDelayEditor::NodoDelayEditor (NodoDelayProcessor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse),
      processor (processorToUse),
      header ("DELAY", processorToUse.getPresetManager()),
      display (processorToUse)
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (header);
    addAndMakeVisible (display);

    auto& state = processor.getState();

    timeLeftKnob.attachTo  (state, ids::timeLeft);
    timeRightKnob.attachTo (state, ids::timeRight);
    feedbackKnob.attachTo  (state, ids::feedback);
    mixKnob.attachTo       (state, ids::mix);
    crossKnob.attachTo     (state, ids::cross);
    highpassKnob.attachTo  (state, ids::highpass);
    lowpassKnob.attachTo   (state, ids::lowpass);
    driveKnob.attachTo     (state, ids::drive);
    loFiKnob.attachTo      (state, ids::loFi);
    modRateKnob.attachTo   (state, ids::modRate);
    modDepthKnob.attachTo  (state, ids::modDepth);

    for (auto* knob : { &timeLeftKnob, &timeRightKnob, &feedbackKnob, &mixKnob,
                        &crossKnob, &highpassKnob, &lowpassKnob, &driveKnob,
                        &loFiKnob, &modRateKnob, &modDepthKnob })
        addAndMakeVisible (knob);

    panLeftSlider.attachTo  (state, ids::panLeft);
    panRightSlider.attachTo (state, ids::panRight);
    widthSlider.attachTo    (state, ids::width);

    for (auto* slim : { &panLeftSlider, &panRightSlider, &widthSlider })
        addAndMakeVisible (slim);

    panLeftSlider.setBipolar (true);
    panRightSlider.setBipolar (true);

    feedbackKnob.getSlider().setTooltip (
        "How much of each repeat goes round again. Past 100 % the loop sustains "
        "instead of decaying - allowed on purpose, and caught by a soft clipper "
        "so it lands on a tone rather than running away.");
    crossKnob.getSlider().setTooltip (
        "How much each line hands to the other. At 0 they are two unrelated "
        "delays; at 100 % everything bounces between them.");
    driveKnob.getSlider().setTooltip (
        "Saturation inside the loop, so every repeat is one generation further "
        "gone rather than all of them being distorted the same amount.");
    loFiKnob.getSlider().setTooltip (
        "Fewer bits and a lower rate, inside the loop. The aliasing is the "
        "point: it is what a rack delay from 1985 sounds like.");
    modDepthKnob.getSlider().setTooltip (
        "Wow and flutter: the read head wobbles. Two rates rather than one, "
        "because a single sine is heard as vibrato and tape is not that regular.");
    lowpassKnob.getSlider().setTooltip (
        "The loop's low pass. Look at the graph: the curve is drawn once per "
        "repeat, so you can see the tone compounding.");

    noteLeftBox.addItemList (noteValueNames(), 1);
    noteLeftAttachment = std::make_unique<ComboAttachment> (state, ids::noteLeft, noteLeftBox);
    addAndMakeVisible (noteLeftBox);

    noteRightBox.addItemList (noteValueNames(), 1);
    noteRightAttachment = std::make_unique<ComboAttachment> (state, ids::noteRight, noteRightBox);
    addAndMakeVisible (noteRightBox);

    routingBox.addItemList (routingNames(), 1);
    routingAttachment = std::make_unique<ComboAttachment> (state, ids::routing, routingBox);
    routingBox.setTooltip ("Ping-pong is a routing, not an amount: the input goes into one line "
                           "only and each line feeds the other, which is why the first repeat "
                           "lands on one side.");
    addAndMakeVisible (routingBox);

    timeModeBox.addItemList (timeModeNames(), 1);
    timeModeAttachment = std::make_unique<ComboAttachment> (state, ids::timeMode, timeModeBox);
    timeModeBox.setTooltip ("Tape slides the read head to the new time, so the tail bends in "
                            "pitch on the way. Fade jumps and crossfades: no bend, no glide.");
    addAndMakeVisible (timeModeBox);

    auto toggle = [this] (juce::TextButton& b, juce::Colour on)
    {
        b.setClickingTogglesState (true);
        b.setColour (juce::TextButton::buttonColourId, colours::panelRaised);
        b.setColour (juce::TextButton::buttonOnColourId, on);
        b.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        addAndMakeVisible (b);
    };

    toggle (syncButton, colours::accent());
    toggle (linkButton, colours::accent());
    toggle (invertButton, colours::accent());
    toggle (freezeButton, colours::warning);

    syncAttachment   = std::make_unique<ButtonAttachment> (state, ids::sync, syncButton);
    linkAttachment   = std::make_unique<ButtonAttachment> (state, ids::linkTimes, linkButton);
    invertAttachment = std::make_unique<ButtonAttachment> (state, ids::invert, invertButton);
    freezeAttachment = std::make_unique<ButtonAttachment> (state, ids::freeze, freezeButton);

    syncButton.setTooltip ("Lock the times to the host's tempo.");
    linkButton.setTooltip ("The right line copies the left. Two lines at the same time is the "
                           "most common setting there is and hunting two knobs to the same "
                           "number is a chore.");
    invertButton.setTooltip ("Flips the phase of the feedback. On short times it changes the "
                             "colour of the comb entirely; on long ones you will not hear it.");
    freezeButton.setTooltip ("Holds what is in the buffer and stops anything new getting in. "
                             "The loop filters still work on what you hear, so you can sweep "
                             "them over a held loop without eroding it.");

    bypassAttachment = std::make_unique<ButtonAttachment> (state, ids::bypass, header.getBypassButton());

    // ---- the taps page ----------------------------------------------------
    toggle (multiTapButton, colours::accent());
    multiTapAttachment = std::make_unique<ButtonAttachment> (state, ids::multiTap, multiTapButton);
    multiTapButton.setTooltip ("Divides the delay time into steps and lets you build a rhythm "
                               "out of it. The last step lands on the delay time itself, where "
                               "the loop closes, so one step is the plain delay.");

    divisionKnob.attachTo (state, ids::division);
    tapSpreadKnob.attachTo (state, ids::tapSpread);

    for (auto* knob : { &divisionKnob, &tapSpreadKnob })
        addAndMakeVisible (knob);

    divisionKnob.getSlider().setTooltip (
        "How many steps the delay time is divided into. The whole pattern is "
        "normalised by this, so sixteen steps are not sixteen times louder than "
        "one - and toggling a single step thins the pattern out instead of "
        "turning the others up.");
    tapSpreadKnob.getSlider().setTooltip (
        "Scales every step's position in the picture at once, so the pattern can "
        "be narrowed to the middle without touching sixteen controls.");

    tapHint.setJustificationType (juce::Justification::centredLeft);
    tapHint.setColour (juce::Label::textColourId, colours::textDim);
    tapHint.setFont (fonts::regular (11.5f));
    tapHint.setText ("Drag a step in the pattern lane: how high is the level, how far across is "
                     "the pan. Alt-click removes one.", juce::dontSendNotification);
    addAndMakeVisible (tapHint);

    // ---- the modulation page ----------------------------------------------
    for (int i = 0; i < numLfos; ++i)
    {
        auto& strip = lfoStrips[(size_t) i];

        strip.shapeBox.addItemList (lfoShapeNames(), 1);
        lfoShapeAttachments[(size_t) i] =
            std::make_unique<ComboAttachment> (state, ids::lfoShape (i + 1), strip.shapeBox);
        addAndMakeVisible (strip.shapeBox);

        strip.noteBox.addItemList (noteValueNames(), 1);
        lfoNoteAttachments[(size_t) i] =
            std::make_unique<ComboAttachment> (state, ids::lfoNote (i + 1), strip.noteBox);
        addAndMakeVisible (strip.noteBox);

        toggle (strip.syncButton, colours::accent());
        lfoSyncAttachments[(size_t) i] =
            std::make_unique<ButtonAttachment> (state, ids::lfoSync (i + 1), strip.syncButton);

        strip.rateSlider.attachTo (state, ids::lfoRate (i + 1));
        strip.phaseSlider.attachTo (state, ids::lfoPhase (i + 1));
        addAndMakeVisible (strip.rateSlider);
        addAndMakeVisible (strip.phaseSlider);

        strip.phaseSlider.getSlider().setTooltip (
            "Where this LFO starts relative to the other one. Ninety degrees "
            "between two of them pointed at the two delay times is what makes a "
            "modulated delay move across the picture instead of in and out.");
    }

    envAttackSlider.attachTo (state, ids::envAttack);
    envReleaseSlider.attachTo (state, ids::envRelease);
    addAndMakeVisible (envAttackSlider);
    addAndMakeVisible (envReleaseSlider);

    for (int slot = 0; slot < numModSlots; ++slot)
    {
        auto& row = modRows[(size_t) slot];

        row.sourceBox.addItemList (modSourceNames(), 1);
        modSourceAttachments[(size_t) slot] =
            std::make_unique<ComboAttachment> (state, ids::modSource (slot + 1), row.sourceBox);
        addAndMakeVisible (row.sourceBox);

        row.targetBox.addItemList (modTargetNames(), 1);
        modTargetAttachments[(size_t) slot] =
            std::make_unique<ComboAttachment> (state, ids::modTarget (slot + 1), row.targetBox);
        addAndMakeVisible (row.targetBox);

        row.amountSlider.attachTo (state, ids::modAmount (slot + 1));
        row.amountSlider.setBipolar (true);
        addAndMakeVisible (row.amountSlider);
    }

    // ---- the page buttons -------------------------------------------------
    const char* pageNames[3] { "MAIN", "TAPS", "MOD" };

    for (int i = 0; i < 3; ++i)
    {
        auto& button = pageButtons[(size_t) i];

        button.setButtonText (pageNames[i]);
        button.setClickingTogglesState (false);
        button.setColour (juce::TextButton::buttonColourId, colours::panelRaised);
        button.setColour (juce::TextButton::buttonOnColourId, colours::accent());
        button.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        button.onClick = [this, i] { setPage ((Page) i); };
        addAndMakeVisible (button);
    }

    if (auto* multiTapParameter = state.getParameter (ids::multiTap))
    {
        multiTapWatcher = std::make_unique<juce::ParameterAttachment> (
            *multiTapParameter, [this] (float v) { refreshMultiTapState (v > 0.5f); }, nullptr);
        multiTapWatcher->sendInitialUpdate();
    }

    setPage (Page::main);

    /*  Sync and link both take controls out of service, and a live knob that
        does nothing is worse than a dimmed one.
    */
    if (auto* syncParameter = state.getParameter (ids::sync))
    {
        syncWatcher = std::make_unique<juce::ParameterAttachment> (
            *syncParameter, [this] (float v) { refreshSyncState (v > 0.5f); }, nullptr);
        syncWatcher->sendInitialUpdate();
    }

    if (auto* linkParameter = state.getParameter (ids::linkTimes))
    {
        linkWatcher = std::make_unique<juce::ParameterAttachment> (
            *linkParameter, [this] (float v) { refreshLinkState (v > 0.5f); }, nullptr);
        linkWatcher->sendInitialUpdate();
    }

    header.onSlotSelected = [this] (bool isB) { processor.switchToSlot (isB); };
    header.onCopyToOtherSlot = [this] { processor.copyCurrentSlotToOther(); };
    header.setLevelSource ([this] (int index) { return processor.getMeterLevel (index); });
    header.setSlot (processor.isSlotB());

    // Fast enough that the source meters move like the thing they are showing.
    startTimerHz (24);

    constrainer.setSizeLimits (860, 550, 1900, 1216);
    constrainer.setFixedAspectRatio ((double) defaultWidth / (double) defaultHeight);
    setConstrainer (&constrainer);
    setResizable (true, true);

    const auto width  = (int) state.state.getProperty (editorWidthProperty, defaultWidth);
    const auto height = (int) state.state.getProperty (editorHeightProperty, defaultHeight);
    setSize (juce::jlimit (860, 1900, width), juce::jlimit (550, 1216, height));
}

NodoDelayEditor::~NodoDelayEditor()
{
    stopTimer();
    storeEditorSize();
    setLookAndFeel (nullptr);
}

void NodoDelayEditor::storeEditorSize()
{
    auto& tree = processor.getState().state;
    tree.setProperty (editorWidthProperty, getWidth(), nullptr);
    tree.setProperty (editorHeightProperty, getHeight(), nullptr);
}

void NodoDelayEditor::timerCallback()
{
    const auto settings = processor.getSettings();

    /*  A synced delay that cannot see the tempo is worse than useless, it is
        confidently wrong, so say which tempo is being used and whether the host
        has ever actually given us one.
    */
    const auto wanted = settings.sync
                      ? (processor.isTempoKnown()
                             ? juce::String (juce::roundToInt (processor.getHostBpm())) + " BPM"
                             : juce::String ("No tempo from the host - assuming 120 BPM"))
                      : juce::String();

    if (wanted != tempoNote)
    {
        tempoNote = wanted;
        repaint (tempoArea);
    }

    if (page == Page::modulation)
        for (const auto& meter : sourceMeters)
            repaint (meter.expanded (0, 12));
}

void NodoDelayEditor::setPage (Page newPage)
{
    page = newPage;

    for (int i = 0; i < 3; ++i)
        pageButtons[(size_t) i].setToggleState (i == (int) page, juce::dontSendNotification);

    applyPageVisibility();
    resized();
    repaint();
}

void NodoDelayEditor::applyPageVisibility()
{
    const auto main = page == Page::main;
    const auto taps = page == Page::taps;
    const auto mod = page == Page::modulation;

    const std::initializer_list<juce::Component*> mainControls {
        &timeLeftKnob, &timeRightKnob, &feedbackKnob, &mixKnob, &crossKnob,
        &highpassKnob, &lowpassKnob, &driveKnob, &loFiKnob, &modRateKnob, &modDepthKnob,
        &panLeftSlider, &panRightSlider, &widthSlider,
        &noteLeftBox, &noteRightBox, &routingBox, &timeModeBox,
        &syncButton, &linkButton, &invertButton, &freezeButton
    };

    for (auto* c : mainControls)
        c->setVisible (main);

    multiTapButton.setVisible (taps);
    divisionKnob.setVisible (taps);
    tapSpreadKnob.setVisible (taps);
    tapHint.setVisible (taps);

    for (auto& strip : lfoStrips)
    {
        strip.shapeBox.setVisible (mod);
        strip.noteBox.setVisible (mod);
        strip.syncButton.setVisible (mod);
        strip.rateSlider.setVisible (mod);
        strip.phaseSlider.setVisible (mod);
    }

    envAttackSlider.setVisible (mod);
    envReleaseSlider.setVisible (mod);

    for (auto& row : modRows)
    {
        row.sourceBox.setVisible (mod);
        row.targetBox.setVisible (mod);
        row.amountSlider.setVisible (mod);
    }
}

void NodoDelayEditor::refreshMultiTapState (bool on)
{
    divisionKnob.setEnabled (on);
    tapSpreadKnob.setEnabled (on);

    // With a pattern running, each step carries its own place in the picture,
    // so the two line pans have nothing left to say.
    panLeftSlider.setEnabled (! on);
    panRightSlider.setEnabled (! on);
    panLeftSlider.repaint();
    panRightSlider.repaint();

    divisionKnob.repaint();
    tapSpreadKnob.repaint();
}

void NodoDelayEditor::refreshSyncState (bool synced)
{
    timeLeftKnob.setEnabled (! synced);
    timeRightKnob.setEnabled (! synced);
    noteLeftBox.setEnabled (synced);
    noteRightBox.setEnabled (synced);

    timeLeftKnob.repaint();
    timeRightKnob.repaint();
    repaint();
}

void NodoDelayEditor::refreshLinkState (bool linked)
{
    timeRightKnob.setEnabled (! linked && ! processor.getSettings().sync);
    noteRightBox.setEnabled (! linked && processor.getSettings().sync);

    timeRightKnob.repaint();
    repaint();
}

void NodoDelayEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    g.setFont (fonts::bold (8.5f));
    g.setColour (colours::textFaint);

    if (page == Page::main)
    {
        g.drawText ("TIME", timeCaption, juce::Justification::centredLeft, false);
        g.drawText ("LOOP", loopCaption, juce::Justification::centredLeft, false);
        g.drawText ("CHARACTER", characterCaption, juce::Justification::centredLeft, false);
        g.drawText ("OUTPUT", outputCaption, juce::Justification::centredLeft, false);

        g.setColour (colours::grid);
        g.fillRect (groupDivider);
    }
    else if (page == Page::modulation)
    {
        g.drawText ("SOURCES", lfoCaption, juce::Justification::centredLeft, false);
        g.drawText ("MATRIX", matrixCaption, juce::Justification::centredLeft, false);

        g.setColour (colours::grid);
        g.fillRect (groupDivider);

        /*  What each source is putting out, right now. The LFOs read from the
            middle out because they swing both ways; the envelope reads from the
            bottom because it does not.
        */
        for (int i = 0; i < 3; ++i)
        {
            const auto area = sourceMeters[(size_t) i].toFloat();

            if (area.isEmpty())
                continue;

            g.setColour (colours::panelRaised);
            g.fillRoundedRectangle (area, 2.0f);

            const auto value = juce::jlimit (-1.0f, 1.0f, processor.getModulationSource (i));

            g.setColour (colours::accentBright());

            if (i < numLfos)
            {
                const auto centre = area.getCentreY();
                const auto extent = value * area.getHeight() * 0.5f;
                g.fillRect (area.getX() + 2.0f, juce::jmin (centre, centre - extent),
                            area.getWidth() - 4.0f, std::abs (extent));
            }
            else
            {
                const auto height = juce::jlimit (0.0f, 1.0f, value) * area.getHeight();
                g.fillRect (area.getX() + 2.0f, area.getBottom() - height,
                            area.getWidth() - 4.0f, height);
            }

            g.setColour (colours::textFaint);
            g.setFont (fonts::bold (7.5f));
            g.drawText (i < numLfos ? juce::String (i + 1) : juce::String ("E"),
                        area.translated (0.0f, -11.0f), juce::Justification::centredTop, false);
        }
    }

    if (tempoNote.isNotEmpty())
    {
        g.setFont (fonts::regular (10.5f));
        g.setColour (processor.isTempoKnown() ? colours::textDim : colours::warning);
        g.drawText (tempoNote, tempoArea, juce::Justification::centredRight, false);
    }
}

void NodoDelayEditor::resized()
{
    auto bounds = getLocalBounds();

    header.setBounds (bounds.removeFromTop ((int) metrics::headerHeight));

    auto footer = bounds.removeFromBottom (footerHeight);
    display.setBounds (bounds.reduced ((int) metrics::padding, 6));

    footer.reduce ((int) metrics::padding, 8);

    constexpr int captionHeight = 11;
    constexpr int boxHeight = 22;
    constexpr int knobHeight = 78;

    // ---- the page selector, on every page ---------------------------------
    auto tabRow = footer.removeFromTop (boxHeight);

    for (auto& button : pageButtons)
    {
        button.setBounds (tabRow.removeFromLeft (62));
        tabRow.removeFromLeft (3);
    }

    tempoArea = tabRow.withTrimmedLeft (12);
    footer.removeFromTop (8);

    if (page == Page::taps)
    {
        auto row = footer.removeFromTop (boxHeight);
        multiTapButton.setBounds (row.removeFromLeft (110));

        footer.removeFromTop (4);
        tapHint.setBounds (footer.removeFromTop (14));
        footer.removeFromTop (6);

        auto knobs = footer.removeFromTop (knobHeight);
        divisionKnob.setBounds (knobs.removeFromLeft (100));
        knobs.removeFromLeft (8);
        tapSpreadKnob.setBounds (knobs.removeFromLeft (100));

        storeEditorSize();
        return;
    }

    if (page == Page::modulation)
    {
        /*  Sources down the left, the matrix down the right. The matrix is one
            column of six rather than two of three: a slot reads left to right —
            this source, that target, this much — and breaking the list into
            columns makes you scan in two directions to answer one question.
        */
        auto right = footer.removeFromRight (juce::jmax (400, footer.getWidth() * 5 / 11));

        matrixCaption = right.removeFromTop (captionHeight);
        right.removeFromTop (3);

        for (auto& row : modRows)
        {
            auto line = right.removeFromTop (boxHeight);

            row.sourceBox.setBounds (line.removeFromLeft (juce::jmax (86, line.getWidth() / 4)));
            line.removeFromLeft (4);
            row.targetBox.setBounds (line.removeFromLeft (juce::jmax (96, line.getWidth() * 2 / 5)));
            line.removeFromLeft (6);
            row.amountSlider.setBounds (line.withSizeKeepingCentre (line.getWidth(), 20));

            right.removeFromTop (3);
        }

        footer.removeFromRight (12);
        groupDivider = footer.removeFromRight (1).withTrimmedTop (2).withTrimmedBottom (10);
        footer.removeFromRight (12);

        lfoCaption = footer.removeFromTop (captionHeight);
        footer.removeFromTop (3);

        for (int i = 0; i < numLfos; ++i)
        {
            auto& strip = lfoStrips[(size_t) i];
            auto line = footer.removeFromTop (boxHeight);

            // A live readout of what the source is doing, at the left of its own
            // row. A matrix whose sources you cannot see is one you set by
            // trial and error.
            sourceMeters[(size_t) i] = line.removeFromLeft (26).reduced (0, 4);
            line.removeFromLeft (6);

            strip.shapeBox.setBounds (line.removeFromLeft (juce::jmax (104, line.getWidth() / 4)));
            line.removeFromLeft (4);
            strip.syncButton.setBounds (line.removeFromLeft (46));
            line.removeFromLeft (4);
            strip.noteBox.setBounds (line.removeFromLeft (72));
            line.removeFromLeft (6);

            const auto slimWidth = juce::jmax (70, (line.getWidth() - 6) / 2);
            strip.rateSlider.setBounds (line.removeFromLeft (slimWidth).withSizeKeepingCentre (slimWidth, 20));
            line.removeFromLeft (6);
            strip.phaseSlider.setBounds (line.withSizeKeepingCentre (line.getWidth(), 20));

            footer.removeFromTop (6);
        }

        auto envRow = footer.removeFromTop (boxHeight);
        sourceMeters[2] = envRow.removeFromLeft (26).reduced (0, 4);
        envRow.removeFromLeft (6);

        const auto envWidth = juce::jmax (100, (envRow.getWidth() - 6) / 2);
        envAttackSlider.setBounds (envRow.removeFromLeft (envWidth).withSizeKeepingCentre (envWidth, 20));
        envRow.removeFromLeft (6);
        envReleaseSlider.setBounds (envRow.removeFromLeft (envWidth).withSizeKeepingCentre (envWidth, 20));

        storeEditorSize();
        return;
    }

    // ---- the main page ----------------------------------------------------
    auto topRow = footer.removeFromTop (boxHeight);

    routingBox.setBounds (topRow.removeFromLeft (110));
    topRow.removeFromLeft (6);
    timeModeBox.setBounds (topRow.removeFromLeft (90));
    topRow.removeFromLeft (10);

    syncButton.setBounds (topRow.removeFromLeft (54));
    topRow.removeFromLeft (4);
    linkButton.setBounds (topRow.removeFromLeft (54));
    topRow.removeFromLeft (4);
    invertButton.setBounds (topRow.removeFromLeft (54));
    topRow.removeFromLeft (4);
    freezeButton.setBounds (topRow.removeFromLeft (66));

    footer.removeFromTop (8);

    auto right = footer.removeFromRight (juce::jmax (530, footer.getWidth() * 6 / 11));

    auto outputColumn = right.removeFromRight (118);
    right.removeFromRight (8);
    groupDivider = right.removeFromRight (1).withTrimmedTop (2).withTrimmedBottom (30);
    right.removeFromRight (10);

    auto loopColumn = right.removeFromLeft (juce::jmax (128, right.getWidth() / 3));
    right.removeFromLeft (8);
    auto characterColumn = right;

    loopCaption = loopColumn.removeFromTop (captionHeight);
    loopColumn.removeFromTop (3);
    auto loopKnobs = loopColumn.removeFromTop (knobHeight);
    highpassKnob.setBounds (loopKnobs.removeFromLeft (loopKnobs.getWidth() / 2));
    lowpassKnob.setBounds (loopKnobs);

    characterCaption = characterColumn.removeFromTop (captionHeight);
    characterColumn.removeFromTop (3);
    auto characterKnobs = characterColumn.removeFromTop (knobHeight);
    const auto characterWidth = juce::jmax (56, characterKnobs.getWidth() / 4);
    driveKnob.setBounds    (characterKnobs.removeFromLeft (characterWidth));
    loFiKnob.setBounds     (characterKnobs.removeFromLeft (characterWidth));
    modRateKnob.setBounds  (characterKnobs.removeFromLeft (characterWidth));
    modDepthKnob.setBounds (characterKnobs);

    outputCaption = outputColumn.removeFromTop (captionHeight);
    outputColumn.removeFromTop (3);
    auto outputKnobs = outputColumn.removeFromTop (knobHeight);
    crossKnob.setBounds (outputKnobs.removeFromLeft (outputKnobs.getWidth() / 2));
    mixKnob.setBounds (outputKnobs);

    footer.removeFromRight (12);

    timeCaption = footer.removeFromTop (captionHeight);
    footer.removeFromTop (3);

    auto knobRow = footer.removeFromTop (knobHeight);
    const auto knobWidth = juce::jmax (64, knobRow.getWidth() / 4);

    timeLeftKnob.setBounds (knobRow.removeFromLeft (knobWidth));
    timeRightKnob.setBounds (knobRow.removeFromLeft (knobWidth));
    feedbackKnob.setBounds (knobRow.removeFromLeft (knobWidth));

    footer.removeFromTop (2);

    auto noteRow = footer.removeFromTop (boxHeight);
    noteLeftBox.setBounds (noteRow.removeFromLeft (knobWidth).reduced (8, 0));
    noteRightBox.setBounds (noteRow.removeFromLeft (knobWidth).reduced (8, 0));

    footer.removeFromTop (6);

    auto slimRow = footer.removeFromTop (20);
    const auto slimWidth = juce::jmax (86, (slimRow.getWidth() - 12) / 3);

    for (auto* slim : { &panLeftSlider, &panRightSlider, &widthSlider })
    {
        slim->setBounds (slimRow.removeFromLeft (slimWidth));
        slimRow.removeFromLeft (6);
    }

    storeEditorSize();
}
} // namespace nodo::delay
