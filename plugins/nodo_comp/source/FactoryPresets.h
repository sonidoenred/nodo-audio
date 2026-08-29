#pragma once

#include <nodo_core/nodo_core.h>

namespace nodo::comp
{
/** The presets that ship inside the plugin.

    Written as starting points rather than finished sounds: every one of them
    assumes the threshold will be moved, because the right threshold depends on
    how loud the track already is and no preset can know that. The description
    that shows up as a tooltip says what to reach for first.
*/
std::vector<FactoryPreset> buildFactoryPresets();
} // namespace nodo::comp
