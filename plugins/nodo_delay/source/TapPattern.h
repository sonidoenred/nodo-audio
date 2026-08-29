#pragma once

#include "DelayParameters.h"

#include <vector>

namespace nodo::delay
{
/** One repeat, as it arrives at the output. */
struct Tap
{
    float timeMs { 0.0f };
    float left { 0.0f };      ///< signed contribution to the left output
    float right { 0.0f };
    /** Which delay line produced it, or -1 when more than one route
        arrived at the same instant and they were added together.
    */
    int line { 0 };

    float magnitude() const noexcept { return std::sqrt (left * left + right * right); }
};

/** Works out where every repeat lands and how loud it is, by simulating the
    feedback network as a series of events rather than by running audio.

    Two lines that feed each other at different times do not produce a simple
    decaying series — they produce an interleaved pattern that most delay
    plugins ask you to discover by ear. It is cheap to compute exactly: each
    repeat that comes out of a line is an event, and each event schedules two
    more, one back into its own line and one into the other. Following that
    until the amplitude falls below hearing gives the whole pattern.

    The display draws this, and the tests check it against what the engine
    actually does — which is the point of having it as a function rather than as
    drawing code.
*/
std::vector<Tap> buildTapPattern (const DelaySettings& settings,
                                  float horizonMs = 4000.0f,
                                  int maximumTaps = 128,
                                  float floorAmplitude = 0.0015f);

/** Constant power pan gains for one line. -1 is hard left. */
void panGains (float pan, float& leftGain, float& rightGain) noexcept;
} // namespace nodo::delay
