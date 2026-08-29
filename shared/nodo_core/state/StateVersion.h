#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace nodo::state
{
/** Every saved state and every preset carries a version number.

    This exists from day one on purpose: the first time a plugin ships an update
    that renames or re-ranges a parameter, sessions saved with the old version
    have to keep opening correctly. Adding versioning after the fact means
    breaking your users' projects once, which is exactly the kind of thing a free
    plugin never recovers from.
*/
inline constexpr int currentVersion = 1;

inline const juce::Identifier versionProperty { "nodoStateVersion" };
inline const juce::Identifier pluginProperty  { "nodoPlugin" };

inline void tagState (juce::ValueTree& tree, const juce::String& pluginId)
{
    tree.setProperty (versionProperty, currentVersion, nullptr);
    tree.setProperty (pluginProperty, pluginId, nullptr);
}

inline int getVersion (const juce::ValueTree& tree)
{
    return (int) tree.getProperty (versionProperty, 0);
}

/** Migrate an older state tree in place.

    Version 1 is the initial release, so there is nothing to do yet. When a
    breaking change lands, add a step here rather than scattering compatibility
    checks through the processors.
*/
inline void migrateIfNeeded (juce::ValueTree& tree)
{
    auto version = getVersion (tree);

    if (version == 0)
    {
        // Pre-release states had no version tag: treat them as version 1.
        tree.setProperty (versionProperty, 1, nullptr);
        version = 1;
    }

    // if (version < 2) { ... ; version = 2; }

    juce::ignoreUnused (version);
}
} // namespace nodo::state
