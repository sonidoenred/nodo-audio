#include "PluginEditor.h"

namespace nodo::eq
{
using namespace nodo::theme;

namespace
{
    const juce::Identifier editorWidthProperty  { "editorWidth" };
    const juce::Identifier editorHeightProperty { "editorHeight" };

    constexpr int defaultWidth = 1000;
    constexpr int defaultHeight = 680;
}

NodoEqEditor::NodoEqEditor (NodoEqProcessor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse),
      processor (processorToUse),
      header ("EQ", processorToUse.getPresetManager()),
      pianoStrip (processorToUse),
      curve (processorToUse),
      bandPanel (processorToUse)
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (header);
    addAndMakeVisible (curve);
    addAndMakeVisible (bandPanel);
    addAndMakeVisible (outputKnob);
    addAndMakeVisible (autoGainButton);
    addAndMakeVisible (pianoRollButton);
    addAndMakeVisible (oversamplingBox);
    addAndMakeVisible (filterModeBox);
    addAndMakeVisible (dynSidechainBox);
    addAndMakeVisible (analyserBox);
    addAndMakeVisible (rangeBox);

    auto& state = processor.getState();

    outputKnob.attachTo (state, ids::outputGain);

    autoGainButton.setClickingTogglesState (true);
    autoGainButton.setColour (juce::TextButton::buttonColourId, colours::panelRaised);
    autoGainButton.setColour (juce::TextButton::buttonOnColourId, colours::accent());
    autoGainButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    autoGainAttachment = std::make_unique<ButtonAttachment> (state, ids::autoGain, autoGainButton);

    bypassAttachment = std::make_unique<ButtonAttachment> (state, ids::bypass, header.getBypassButton());

    oversamplingBox.addItemList ({ "OS Off", "OS 2x" }, 1);
    oversamplingAttachment = std::make_unique<ComboAttachment> (state, ids::oversampling, oversamplingBox);

    analyserBox.addItemList ({ "Analyser off", "Pre", "Post", "Pre + Post" }, 1);
    analyserAttachment = std::make_unique<ComboAttachment> (state, ids::analyserMode, analyserBox);

    filterModeBox.addItemList ({ "Digital filters", "Analog filters" }, 1);
    filterModeAttachment = std::make_unique<ComboAttachment> (state, ids::filterMode, filterModeBox);

    dynSidechainBox.addItemList ({ "Dyn: Internal", "Dyn: Sidechain" }, 1);
    dynSidechainAttachment = std::make_unique<ComboAttachment> (state, ids::dynSidechain, dynSidechainBox);
    dynSidechainBox.setTooltip ("Where the dynamic bands listen. Sidechain uses the "
                                "plugin's sidechain input, so another track decides "
                                "when the bands move.");

    // The display range is a view setting, not automation, so it is stored in the
    // state tree rather than as a parameter.
    rangeBox.addItemList ({ juce::String::fromUTF8 ("± 6 dB"),
                            juce::String::fromUTF8 ("± 12 dB"),
                            juce::String::fromUTF8 ("± 30 dB") }, 1);
    const auto storedRange = processor.getDisplayRangeDb();
    rangeBox.setSelectedId (storedRange <= 6.5f ? 1 : storedRange <= 12.5f ? 2 : 3,
                            juce::dontSendNotification);
    rangeBox.onChange = [this]
    {
        const auto id = rangeBox.getSelectedId();
        processor.setDisplayRangeDb (id == 1 ? 6.0f : id == 2 ? 12.0f : 30.0f);
    };

    pianoRollButton.setClickingTogglesState (true);
    pianoRollButton.setColour (juce::TextButton::buttonColourId, colours::panelRaised);
    pianoRollButton.setColour (juce::TextButton::buttonOnColourId, colours::accent());
    pianoRollButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    pianoRollButton.setToggleState (processor.getPianoRollVisible(), juce::dontSendNotification);
    pianoRollButton.setTooltip ("Puts a keyboard along the top and shades the notes on the graph. "
                                "Double click a key to create a band on that exact note, and every "
                                "band leaves a dot of its colour on the key it falls on. For hunting "
                                "a ringing note or placing a cut by ear rather than by number.");
    pianoRollButton.onClick = [this]
    {
        processor.setPianoRollVisible (pianoRollButton.getToggleState());

        // El teclado aparece y desaparece con el sombreado de notas: son la
        // misma idea, una para leer y otra para tocar.
        pianoStrip.setVisible (pianoRollButton.getToggleState());
        resized();
    };

    curve.onBandSelected = [this] (int band)
    {
        bandPanel.setBand (band);
        pianoStrip.setSelectedBand (band);
    };

    /*  El teclado crea bandas en la nota exacta que se pulsa. La ganancia
        empieza en cero: la nota dice donde, y el que la pulsa todavia no ha
        dicho cuanto.
    */
    pianoStrip.onNoteDoubleClicked = [this] (double frequency)
    {
        const auto band = curve.addBand (frequency, 0.0, FilterType::bell);

        if (band >= 0)
            pianoStrip.setSelectedBand (band);
    };

    pianoStrip.onBandClicked = [this] (int band)
    {
        curve.setSelectedBand (band);
        pianoStrip.setSelectedBand (band);
    };

    addAndMakeVisible (pianoStrip);
    pianoStrip.setVisible (processor.getPianoRollVisible());

    header.onSlotSelected = [this] (bool isB)
    {
        processor.switchToSlot (isB);
        // The other slot may not even have the soloed band switched on.
        processor.setSoloBand (-1);
        bandPanel.setBand (-1);
    };
    header.setLevelSource ([this] (int index) { return processor.getMeterLevel (index); });
    header.setSlot (processor.isSlotB());

    constrainer.setSizeLimits (820, 560, 1900, 1300);
    constrainer.setFixedAspectRatio ((double) defaultWidth / (double) defaultHeight);
    setConstrainer (&constrainer);
    setResizable (true, true);

    const auto width  = (int) state.state.getProperty (editorWidthProperty, defaultWidth);
    const auto height = (int) state.state.getProperty (editorHeightProperty, defaultHeight);
    setSize (juce::jlimit (820, 1900, width), juce::jlimit (560, 1300, height));
}

NodoEqEditor::~NodoEqEditor()
{
    storeEditorSize();
    setLookAndFeel (nullptr);
}

void NodoEqEditor::storeEditorSize()
{
    auto& tree = processor.getState().state;
    tree.setProperty (editorWidthProperty, getWidth(), nullptr);
    tree.setProperty (editorHeightProperty, getHeight(), nullptr);
}

void NodoEqEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    // Captions over the two groups of global controls. Without them a user has
    // no way to tell that "Analyser" changes nothing about the sound while
    // "Analog filters" changes everything: they were four identical dropdowns.
    g.setFont (fonts::bold (8.5f));
    g.setColour (colours::textFaint);
    g.drawText ("PROCESS", processGroupCaption, juce::Justification::centredLeft, false);
    g.drawText ("VIEW", viewGroupCaption, juce::Justification::centredLeft, false);

    g.setColour (colours::grid);
    g.fillRect (groupDivider);
}

void NodoEqEditor::resized()
{
    auto bounds = getLocalBounds();

    header.setBounds (bounds.removeFromTop ((int) metrics::headerHeight));

    auto footer = bounds.removeFromBottom ((int) metrics::footerHeight);

    // El teclado se lleva su franja de la parte de arriba del grafico, que es
    // la que casi siempre esta vacia.
    if (pianoStrip.isVisible())
        pianoStrip.setBounds (bounds.removeFromTop (26));

    curve.setBounds (bounds);

    footer.reduce ((int) metrics::padding, 8);

    // Right hand side of the footer: two columns of global controls along the
    // top, the output knob tucked underneath them, and everything to the left of
    // that goes to the band panel.
    constexpr int boxHeight = 24;
    constexpr int columnWidth = 142;
    constexpr int captionHeight = 12;

    auto right = footer.removeFromRight (columnWidth * 2 + 18);

    auto viewColumn = right.removeFromRight (columnWidth);
    right.removeFromRight (8);
    groupDivider = right.removeFromRight (1).withTrimmedTop (4).withTrimmedBottom (32);
    right.removeFromRight (8);
    auto processColumn = right.removeFromRight (columnWidth);

    // Left group: settings that change the sound.
    processGroupCaption = processColumn.removeFromTop (captionHeight);
    processColumn.removeFromTop (3);
    filterModeBox.setBounds (processColumn.removeFromTop (boxHeight));
    processColumn.removeFromTop (5);
    oversamplingBox.setBounds (processColumn.removeFromTop (boxHeight));
    processColumn.removeFromTop (5);
    dynSidechainBox.setBounds (processColumn.removeFromTop (boxHeight));

    // Right group: settings that change only what you are looking at.
    viewGroupCaption = viewColumn.removeFromTop (captionHeight);
    viewColumn.removeFromTop (3);
    analyserBox.setBounds (viewColumn.removeFromTop (boxHeight));
    viewColumn.removeFromTop (5);

    auto lastRow = viewColumn.removeFromTop (boxHeight);
    pianoRollButton.setBounds (lastRow.removeFromRight (40));
    lastRow.removeFromRight (4);
    rangeBox.setBounds (lastRow);

    // Output sits to the left of the columns and keeps a sensible size instead
    // of stretching to whatever height the footer happens to be.
    footer.removeFromRight (12);
    auto outputArea = footer.removeFromRight (86).withSizeKeepingCentre (86, 106);
    autoGainButton.setBounds (outputArea.removeFromBottom (18).reduced (16, 0));
    outputKnob.setBounds (outputArea);

    footer.removeFromRight (12);
    bandPanel.setBounds (footer);

    storeEditorSize();
}
} // namespace nodo::eq
