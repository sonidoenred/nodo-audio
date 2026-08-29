namespace nodo
{
using namespace nodo::theme;

NodoHeader::NodoHeader (juce::String name, PresetManager& presets)
    : presetManager (presets),
      pluginName (std::move (name))
{
    auto styleNav = [this] (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId, colours::panelRaised);
        addAndMakeVisible (b);
    };

    styleNav (previousButton);
    styleNav (nextButton);
    styleNav (presetButton);
    styleNav (saveButton);
    styleNav (slotAButton);
    styleNav (slotBButton);

    bypassButton.setClickingTogglesState (true);
    bypassButton.setColour (juce::TextButton::buttonColourId, colours::panelRaised);
    bypassButton.setColour (juce::TextButton::buttonOnColourId, colours::warning);
    bypassButton.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    addAndMakeVisible (bypassButton);

    previousButton.onClick = [this] { presetManager.loadPrevious(); };
    nextButton.onClick     = [this] { presetManager.loadNext(); };
    presetButton.onClick   = [this] { showPresetMenu(); };
    saveButton.onClick     = [this] { promptForPresetName(); };

    slotAButton.setClickingTogglesState (false);
    slotBButton.setClickingTogglesState (false);
    slotAButton.onClick = [this] { setSlot (false); if (onSlotSelected) onSlotSelected (false); };
    slotBButton.onClick = [this] { setSlot (true);  if (onSlotSelected) onSlotSelected (true); };

    setSlot (false);

    inputMeter.setCaption ("IN");
    outputMeter.setCaption ("OUT");
    addChildComponent (inputMeter);
    addChildComponent (outputMeter);

    presetManager.addChangeListener (this);
    refreshPresetDisplay();
}

NodoHeader::~NodoHeader()
{
    presetManager.removeChangeListener (this);
}

void NodoHeader::setLevelSource (std::function<float (int)> source)
{
    if (source == nullptr)
        return;

    inputMeter.setSource ([source] (int ch) { return source (ch); }, 2);
    outputMeter.setSource ([source] (int ch) { return source (ch + 2); }, 2);

    metersVisible = true;
    inputMeter.setVisible (true);
    outputMeter.setVisible (true);
    resized();
}

void NodoHeader::setSlot (bool isB)
{
    slotIsB = isB;
    slotAButton.setColour (juce::TextButton::buttonColourId,
                           isB ? colours::panelRaised : colours::accent());
    slotBButton.setColour (juce::TextButton::buttonColourId,
                           isB ? colours::accent() : colours::panelRaised);
    slotAButton.setColour (juce::TextButton::textColourOffId,
                           isB ? colours::textDim : colours::text);
    slotBButton.setColour (juce::TextButton::textColourOffId,
                           isB ? colours::text : colours::textDim);
    repaint();
}

void NodoHeader::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshPresetDisplay();
}

void NodoHeader::refreshPresetDisplay()
{
    auto name = presetManager.getCurrentPresetName();

    if (name.isEmpty())
        name = "Default";

    if (presetManager.isPresetModified())
        name += " *";

    presetButton.setButtonText (name);

    // The description is where the teaching happens, so it rides along as a
    // tooltip rather than being lost in a menu that is already closed.
    const auto description = presetManager.getCurrentDescription();
    presetButton.setTooltip (description.isNotEmpty()
                             ? description
                             : juce::String ("Choose a preset"));
}

void NodoHeader::showPresetMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());

    menu.addItem (1, "Default");

    // Factory presets first, grouped by category, in the order the author chose.
    const auto& factory = presetManager.getFactoryPresets();

    if (! factory.empty())
    {
        menu.addSeparator();

        for (const auto& category : presetManager.getFactoryCategories())
        {
            juce::PopupMenu categoryMenu;

            for (size_t i = 0; i < factory.size(); ++i)
            {
                if (factory[i].category != category)
                    continue;

                categoryMenu.addItem ((int) i + 2000, factory[i].name, true,
                                      factory[i].name == presetManager.getCurrentPresetName());
            }

            menu.addSubMenu (category, categoryMenu);
        }
    }

    const auto names = presetManager.getPresetNames();

    menu.addSeparator();

    if (names.isEmpty())
    {
        menu.addItem (-1, "No presets of your own yet", false, false);
    }
    else
    {
        juce::PopupMenu userMenu;

        for (int i = 0; i < names.size(); ++i)
            userMenu.addItem (i + 2, names[i], true, names[i] == presetManager.getCurrentPresetName());

        menu.addSubMenu ("My presets", userMenu);
    }

    menu.addSeparator();
    menu.addItem (1000, "Show preset folder...");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetButton),
                        [this, names] (int result)
                        {
                            if (result == 1)
                                presetManager.loadDefault();
                            else if (result == 1000)
                                presetManager.getPresetDirectory().revealToUser();
                            else if (result >= 2000)
                            {
                                const auto& presets = presetManager.getFactoryPresets();
                                const auto index = (size_t) (result - 2000);

                                if (index < presets.size())
                                    presetManager.loadFactoryPreset (presets[index].name);
                            }
                            else if (result >= 2 && result - 2 < names.size())
                                presetManager.loadPreset (names[result - 2]);
                        });
}

void NodoHeader::promptForPresetName()
{
    nameWindow = std::make_unique<juce::AlertWindow> ("Save preset",
                                                      "Name this preset:",
                                                      juce::MessageBoxIconType::NoIcon);
    nameWindow->addTextEditor ("name", presetManager.getCurrentPresetName(), {});
    nameWindow->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    nameWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    nameWindow->enterModalState (true,
        juce::ModalCallbackFunction::create ([this] (int result)
        {
            if (result == 1 && nameWindow != nullptr)
            {
                const auto name = nameWindow->getTextEditorContents ("name").trim();

                if (name.isNotEmpty())
                    presetManager.savePreset (name);
            }

            nameWindow.reset();
        }), false);
}

void NodoHeader::paint (juce::Graphics& g)
{
    g.setColour (colours::panel);
    g.fillRect (getLocalBounds());

    g.setColour (colours::grid);
    g.fillRect (getLocalBounds().removeFromBottom (1));

    /*  Wordmark: NODO in the brand purple whatever the plugin's accent is. The
        logo belongs to SonidoenRed and stays put; the accent belongs to the
        plugin and changes. Tying the two together would mean the brand appears
        in a different colour in every plugin, which is the opposite of what a
        brand is for.
    */
    auto textArea = getLocalBounds().reduced ((int) metrics::padding, 0).toFloat();

    g.setFont (fonts::bold (15.0f));
    const auto brandWidth = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), "NODO");

    g.setColour (colours::wordmark);
    g.drawText ("NODO", textArea, juce::Justification::centredLeft, false);

    g.setFont (fonts::regular (15.0f));
    g.setColour (colours::textDim);
    g.drawText (pluginName.toUpperCase(),
                textArea.withTrimmedLeft (brandWidth + 7.0f),
                juce::Justification::centredLeft, false);
}

void NodoHeader::resized()
{
    auto bounds = getLocalBounds().reduced ((int) metrics::padding, 6);
    const auto h = bounds.getHeight();

    // Meters live in what used to be dead space between the wordmark and the
    // preset controls, which was most of the header.
    if (metersVisible)
    {
        auto meterArea = bounds.withTrimmedLeft (96).withWidth (240);
        inputMeter.setBounds (meterArea.removeFromLeft (116));
        meterArea.removeFromLeft (8);
        outputMeter.setBounds (meterArea);
    }

    // Right hand side: bypass, then the A/B pair.
    bypassButton.setBounds (bounds.removeFromRight (66));
    bounds.removeFromRight (8);
    slotBButton.setBounds (bounds.removeFromRight (24));
    slotAButton.setBounds (bounds.removeFromRight (24));
    bounds.removeFromRight (12);

    // Centre: preset navigation, right-aligned so it never collides with the
    // wordmark on a narrow window.
    saveButton.setBounds (bounds.removeFromRight (48));
    bounds.removeFromRight (6);
    nextButton.setBounds (bounds.removeFromRight (h));
    presetButton.setBounds (bounds.removeFromRight (juce::jmin (170, bounds.getWidth() - 120)));
    previousButton.setBounds (bounds.removeFromRight (h));
}
} // namespace nodo
