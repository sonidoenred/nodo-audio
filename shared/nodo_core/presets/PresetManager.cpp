namespace nodo
{
PresetManager::PresetManager (juce::AudioProcessorValueTreeState& stateToUse,
                              juce::String pluginId)
    : apvts (stateToUse),
      pluginIdentifier (std::move (pluginId))
{
    getPresetDirectory().createDirectory();
}

juce::File PresetManager::getPresetDirectory() const
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
             .getChildFile ("Nodo")
             .getChildFile (pluginIdentifier)
             .getChildFile ("Presets");
}

juce::File PresetManager::fileForName (const juce::String& name) const
{
    return getPresetDirectory().getChildFile (juce::File::createLegalFileName (name)
                                              + "." + extension);
}

juce::StringArray PresetManager::getPresetNames() const
{
    juce::StringArray names;

    for (const auto& entry : juce::RangedDirectoryIterator (getPresetDirectory(),
                                                            false,
                                                            juce::String ("*.") + extension,
                                                            juce::File::findFiles))
        names.add (entry.getFile().getFileNameWithoutExtension());

    names.sortNatural();
    return names;
}

void PresetManager::setFactoryPresets (std::vector<FactoryPreset> presets)
{
    factoryPresets = std::move (presets);
}

juce::StringArray PresetManager::getFactoryCategories() const
{
    juce::StringArray categories;

    for (const auto& preset : factoryPresets)
        if (! categories.contains (preset.category))
            categories.add (preset.category);

    return categories;
}

bool PresetManager::loadFactoryPreset (const juce::String& name)
{
    for (const auto& preset : factoryPresets)
    {
        if (preset.name != name)
            continue;

        // Start from a clean slate. Without this a preset with three bands would
        // inherit whatever the previous one left switched on in bands four
        // upwards, which is the classic way factory presets end up sounding
        // different depending on what you loaded before them.
        loadDefault();

        for (const auto& [parameterID, value] : preset.assignments)
            if (auto* parameter = apvts.getParameter (parameterID))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));

        currentPreset = name;
        modified = false;
        sendChangeMessage();
        return true;
    }

    return false;
}

juce::String PresetManager::getCurrentDescription() const
{
    for (const auto& preset : factoryPresets)
        if (preset.name == currentPreset)
            return preset.description;

    return {};
}

void PresetManager::markAsModified()
{
    if (! modified)
    {
        modified = true;
        sendChangeMessage();
    }
}

bool PresetManager::savePreset (const juce::String& name)
{
    if (name.isEmpty())
        return false;

    auto state = apvts.copyState();
    state::tagState (state, pluginIdentifier);

    auto xml = state.createXml();

    if (xml == nullptr)
        return false;

    auto file = fileForName (name);
    file.getParentDirectory().createDirectory();

    if (! xml->writeTo (file))
        return false;

    currentPreset = name;
    modified = false;
    sendChangeMessage();
    return true;
}

bool PresetManager::loadPreset (const juce::String& name)
{
    auto file = fileForName (name);

    if (! file.existsAsFile())
        return false;

    auto xml = juce::XmlDocument::parse (file);

    if (xml == nullptr)
        return false;

    auto tree = juce::ValueTree::fromXml (*xml);

    if (! tree.isValid())
        return false;

    state::migrateIfNeeded (tree);
    apvts.replaceState (tree);

    currentPreset = name;
    modified = false;
    sendChangeMessage();
    return true;
}

bool PresetManager::deletePreset (const juce::String& name)
{
    auto file = fileForName (name);

    if (! file.existsAsFile() || ! file.deleteFile())
        return false;

    if (currentPreset == name)
        currentPreset = {};

    sendChangeMessage();
    return true;
}

void PresetManager::loadNext()
{
    const auto names = getPresetNames();

    if (names.isEmpty())
        return;

    const auto index = names.indexOf (currentPreset);
    loadPreset (names[(index + 1) % names.size()]);
}

void PresetManager::loadPrevious()
{
    const auto names = getPresetNames();

    if (names.isEmpty())
        return;

    const auto index = names.indexOf (currentPreset);
    const auto previous = index <= 0 ? names.size() - 1 : index - 1;
    loadPreset (names[previous]);
}

void PresetManager::loadDefault()
{
    for (auto* parameter : apvts.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());

    currentPreset = {};
    modified = false;
    sendChangeMessage();
}
} // namespace nodo
