#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace nodo
{
/** Turns a UTF-8 literal into a juce::String correctly.

    juce::String reads a plain `const char*` as ASCII: a byte above 127 becomes
    that code point rather than part of a UTF-8 sequence, so "Sala pequeña"
    arrives on screen as "Sala pequeÃ±a". Every factory preset in this suite is
    written in Spanish, so this is not an edge case — it was every accented
    preset name in all seven plugins, and it was invisible until somebody
    opened the menu and looked at it.
*/
inline juce::String utf8 (const char* literal)
{
    return juce::String (juce::CharPointer_UTF8 (literal));
}

/** A preset that ships inside the binary rather than living on disk.

    Held as a plain list of parameter assignments so it can be checked against
    the parameter layout at build time: a factory preset with a typo in an ID
    would otherwise load silently and do nothing at all.
*/
struct FactoryPreset
{
    juce::String category;
    juce::String name;
    juce::String description;
    std::vector<std::pair<juce::String, float>> assignments;
};

/** User preset handling shared by the whole suite.

    Presets live in one predictable place per plugin:

        macOS    ~/Documents/Nodo/<Plugin>/Presets
        Windows  Documents\Nodo\<Plugin>\Presets

    so a user who learns where Nodo EQ keeps its presets already knows where the
    compressor keeps its own. Files are plain XML with a .nodo extension, which
    means they can be shared, versioned, edited by hand and read by a future
    version of the plugin.
*/
class PresetManager : public juce::ChangeBroadcaster
{
public:
    static constexpr const char* extension = "nodo";

    PresetManager (juce::AudioProcessorValueTreeState& stateToUse,
                   juce::String pluginId);

    /** Directory presets are read from and written to; created on demand. */
    juce::File getPresetDirectory() const;

    /** Names of every preset on disk, sorted, without extension. */
    juce::StringArray getPresetNames() const;

    /** Presets built into the plugin. Set once, at construction. */
    void setFactoryPresets (std::vector<FactoryPreset> presets);
    const std::vector<FactoryPreset>& getFactoryPresets() const noexcept { return factoryPresets; }

    /** Categories in the order they first appear, so the menu keeps the order
        the author chose rather than sorting it alphabetically.
    */
    juce::StringArray getFactoryCategories() const;

    bool loadFactoryPreset (const juce::String& name);

    /** One line explaining the preset currently loaded, if it is a factory one. */
    juce::String getCurrentDescription() const;

    juce::String getCurrentPresetName() const { return currentPreset; }
    bool         isPresetModified() const     { return modified; }

    /** Marks the current preset as edited, so the GUI can show an asterisk. */
    void markAsModified();

    bool savePreset (const juce::String& name);
    bool loadPreset (const juce::String& name);
    bool deletePreset (const juce::String& name);

    /** Wraps around the sorted preset list. Does nothing when there are none. */
    void loadNext();
    void loadPrevious();

    /** Resets every parameter to its default and clears the preset name. */
    void loadDefault();

private:
    juce::File fileForName (const juce::String& name) const;

    juce::AudioProcessorValueTreeState& apvts;
    juce::String pluginIdentifier;
    std::vector<FactoryPreset> factoryPresets;
    juce::String currentPreset;
    bool modified { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManager)
};
} // namespace nodo
