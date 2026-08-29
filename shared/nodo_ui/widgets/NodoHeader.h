#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <nodo_core/nodo_core.h>
#include "../theme/NodoTheme.h"

namespace nodo
{
/** The bar across the top of every plugin in the suite.

    Wordmark and plugin name on the left, preset navigation in the middle, A/B
    and bypass on the right. It is deliberately the same component rather than
    four similar ones: this is the first thing a user sees, and identical
    behaviour across the suite is what makes four plugins feel like one product.
*/
class NodoHeader : public juce::Component,
                   private juce::ChangeListener
{
public:
    NodoHeader (juce::String pluginName, PresetManager& presets);
    ~NodoHeader() override;

    /** Called when the user clicks A or B. The editor owns the two snapshots. */
    std::function<void (bool /*isB*/)> onSlotSelected;
    std::function<void()> onCopyToOtherSlot;

    juce::Button& getBypassButton() noexcept { return bypassButton; }

    /** Feeds the input and output meters. Index 0 and 1 are the input channels,
        2 and 3 the output. Leave unset and the meters simply do not appear.
    */
    void setLevelSource (std::function<float (int)> source);

    void setSlot (bool isB);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshPresetDisplay();
    void showPresetMenu();
    void promptForPresetName();

    PresetManager& presetManager;
    juce::String pluginName;

    juce::TextButton previousButton { "<" };
    juce::TextButton nextButton { ">" };
    juce::TextButton presetButton { "Default" };
    juce::TextButton saveButton { "SAVE" };
    juce::TextButton slotAButton { "A" };
    juce::TextButton slotBButton { "B" };
    juce::TextButton bypassButton { "BYPASS" };

    NodoMeter inputMeter, outputMeter;
    bool metersVisible { false };

    std::unique_ptr<juce::AlertWindow> nameWindow;
    bool slotIsB { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodoHeader)
};
} // namespace nodo
