#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../theme/NodoTheme.h"

namespace nodo
{
/** A compact horizontal level meter with peak hold.

    Deliberately small and quiet: a mixing plugin's meter is there to tell you
    that signal is arriving and that you are not clipping, not to be looked at.
    Anything more detailed belongs on the channel strip.

    The level is pulled from a callback rather than pushed in, so the audio
    thread never has to know a meter exists.
*/
class NodoMeter : public juce::Component,
                  private juce::Timer
{
public:
    NodoMeter();
    ~NodoMeter() override;

    /** Returns the current linear magnitude for a channel, 1.0 == 0 dBFS. */
    void setSource (std::function<float (int)> source, int numChannels);

    /** Optional. With it the meter also prints two numbers: the peak it has
        reached and the running RMS. Without it the meter stays as it was.
    */
    void setRmsSource (std::function<float (int)> source);

    void setCaption (juce::String newCaption);

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    static juce::String formatDb (float db);

    static constexpr float floorDb = -54.0f;
    static constexpr int maxChannels = 2;

    std::function<float (int)> levelSource;
    std::function<float (int)> rmsSource;
    int channels { 2 };
    juce::String caption;

    std::array<float, maxChannels> levelDb { floorDb, floorDb };
    std::array<float, maxChannels> peakDb { floorDb, floorDb };
    std::array<int, maxChannels> peakAge { 0, 0 };

    float shownPeakDb { floorDb };
    float shownRmsDb { floorDb };
    int   shownPeakAge { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodoMeter)
};
} // namespace nodo
