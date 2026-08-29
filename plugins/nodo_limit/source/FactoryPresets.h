#pragma once

#include <nodo_core/nodo_core.h>

namespace nodo::limit
{
/** Presets for the limiter, written as starting points for a target rather than
    as finished sounds: the gain that gets you to a loudness target depends on
    the mix, and no preset can know that. Each description says what to watch.
*/
std::vector<FactoryPreset> buildFactoryPresets();
} // namespace nodo::limit
