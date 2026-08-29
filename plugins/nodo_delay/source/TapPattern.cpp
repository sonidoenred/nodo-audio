#include "TapPattern.h"

#include <algorithm>
#include <cmath>

namespace nodo::delay
{
void panGains (float pan, float& leftGain, float& rightGain) noexcept
{
    // Constant power: a line panned to the centre is not 3 dB louder than the
    // same line panned hard, which is what a linear law would do.
    const auto angle = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * 0.25f
                     * juce::MathConstants<float>::pi;

    leftGain = std::cos (angle);
    rightGain = std::sin (angle);
}

std::vector<Tap> buildTapPattern (const DelaySettings& settings,
                                  float horizonMs,
                                  int maximumTaps,
                                  float floorAmplitude)
{
    std::vector<Tap> taps;

    const float times[2] { resolvedTimeMs (settings, 0), resolvedTimeMs (settings, 1) };

    if (times[0] <= 0.0f || times[1] <= 0.0f)
        return taps;

    float panL = 0.0f, panR = 0.0f;
    float gains[2][2];
    panGains (settings.panLeft, panL, panR);
    gains[0][0] = panL; gains[0][1] = panR;
    panGains (settings.panRight, panL, panR);
    gains[1][0] = panL; gains[1][1] = panR;

    const auto steps = activeSteps (settings);
    const auto normalise = 1.0f / std::sqrt ((float) juce::jmax (1, steps));
    const auto spread = juce::jlimit (0.0f, 1.5f, settings.tapSpread);

    const auto pingPong = settings.routing == Routing::pingPong;
    const auto cross = pingPong ? 1.0f : juce::jlimit (0.0f, 1.0f, settings.cross);
    const auto feedback = settings.freeze ? 1.0f : juce::jlimit (0.0f, 1.1f, settings.feedback);
    const auto sign = settings.invertFeedback ? -1.0f : 1.0f;

    /*  What an impulse on both inputs puts into each line. Ping-pong sums to the
        left line only, which is what makes the first repeat land on one side.
    */
    struct Event { float timeMs; float amplitude; int line; };

    std::vector<Event> pending;

    switch (settings.routing)
    {
        case Routing::pingPong:
            pending.push_back ({ 0.0f, 1.0f, 0 });
            break;

        case Routing::mono:
            pending.push_back ({ 0.0f, 1.0f, 0 });
            pending.push_back ({ 0.0f, 1.0f, 1 });
            break;

        case Routing::stereo:
        default:
            pending.push_back ({ 0.0f, 1.0f, 0 });
            pending.push_back ({ 0.0f, 1.0f, 1 });
            break;
    }

    while (! pending.empty() && (int) taps.size() < maximumTaps)
    {
        // Take the earliest event. A linear scan rather than a heap: the queue
        // never holds more than a couple of hundred entries and this runs once
        // per repaint.
        const auto earliest = (size_t) std::distance (
            pending.begin(),
            std::min_element (pending.begin(), pending.end(),
                              [] (const Event& a, const Event& b) { return a.timeMs < b.timeMs; }));

        const auto event = pending[earliest];
        pending.erase (pending.begin() + (long) earliest);

        const auto arrival = event.timeMs + times[event.line];

        if (arrival > horizonMs || std::abs (event.amplitude) < floorAmplitude)
            continue;

        if (steps > 1 || settings.multiTap)
        {
            /*  Every pass through a line puts out the whole pattern, not one
                repeat. The last step lands on the delay time itself — which is
                where the loop closes — so at a division of one this reduces to
                exactly the single tap it was before any of this existed.
            */
            for (int step = 1; step <= steps; ++step)
            {
                const auto& stepSettings = settings.taps[(size_t) (step - 1)];

                if (! stepSettings.on || stepSettings.level <= 0.0f)
                    continue;

                float left = 0.0f, right = 0.0f;
                panGains (juce::jlimit (-1.0f, 1.0f, stepSettings.pan * spread), left, right);

                const auto gain = stepSettings.level * normalise;
                const auto at = event.timeMs + times[event.line] * (float) step / (float) steps;

                if (at > horizonMs)
                    continue;

                Tap tap;
                tap.timeMs = at;
                tap.left = event.amplitude * gain * left;
                tap.right = event.amplitude * gain * right;
                tap.line = event.line;
                taps.push_back (tap);
            }
        }
        else
        {
            Tap tap;
            tap.timeMs = arrival;
            tap.left = event.amplitude * gains[event.line][0];
            tap.right = event.amplitude * gains[event.line][1];
            tap.line = event.line;
            taps.push_back (tap);
        }

        const auto passed = event.amplitude * feedback * sign;
        const auto other = 1 - event.line;

        if (std::abs (passed) >= floorAmplitude)
        {
            if (cross < 1.0f)
                pending.push_back ({ arrival, passed * (1.0f - cross), event.line });

            if (cross > 0.0f)
                pending.push_back ({ arrival, passed * cross, other });
        }
    }

    std::sort (taps.begin(), taps.end(),
               [] (const Tap& a, const Tap& b) { return a.timeMs < b.timeMs; });

    /*  Two routes through the network can arrive at the same instant — with
        120 ms and 200 ms lines, left-then-right and right-then-left both land
        at 320 — and when they do, the audio adds them. Leaving them as separate
        entries would draw two bars on top of each other and predict the wrong
        level for a repeat that is really the sum of two.
    */
    std::vector<Tap> merged;
    merged.reserve (taps.size());

    for (const auto& tap : taps)
    {
        if (! merged.empty() && std::abs (tap.timeMs - merged.back().timeMs) < 0.02f)
        {
            merged.back().left += tap.left;
            merged.back().right += tap.right;

            // Once two routes have been added together there is no single line
            // that produced the result, and saying there is would be a lie a
            // caller could act on.
            if (merged.back().line != tap.line)
                merged.back().line = -1;

            continue;
        }

        merged.push_back (tap);
    }

    return merged;
}
} // namespace nodo::delay
