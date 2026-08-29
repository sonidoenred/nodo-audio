#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <nodo_ui/nodo_ui.h>

#include "PluginProcessor.h"

namespace nodo::eq
{
/** Numeric controls for whichever band is selected on the curve.

    One panel that re-points at a different band rather than twenty-four panels
    in tabs: it keeps the window short and means the controls are always in the
    same place, which matters more than it sounds when you are working fast.
*/
class BandPanel : public juce::Component,
                  private juce::Timer
{
public:
    explicit BandPanel (NodoEqProcessor& processorToUse);
    ~BandPanel() override;

    void setBand (int band);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void rebuildAttachments();
    void updateEnablement();

    NodoEqProcessor& processor;
    juce::AudioProcessorValueTreeState& state;
    int currentBand { -1 };

    juce::TextButton enableButton { "ON" };
    juce::TextButton soloButton { "SOLO" };
    juce::TextButton invertButton { "INV" };
    juce::ComboBox typeBox;
    juce::ComboBox slopeBox;
    juce::ComboBox channelBox;

    juce::TextButton dynamicButton { "DYN" };
    juce::ComboBox dynamicModeBox;
    NodoSlimSlider dynRangeSlider { "Range" };
    NodoKnob thresholdKnob { "Threshold" };
    NodoKnob ratioKnob { "Ratio" };
    NodoKnob attackKnob { "Attack" };
    NodoKnob releaseKnob { "Release" };
    NodoKnob freqKnob { "Freq" };
    NodoKnob gainKnob { "Gain" };
    NodoKnob qKnob { "Q" };
    juce::Label noteLabel;
    juce::Label dynamicHint;

    float gainReductionDb { 0.0f };

    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<ButtonAttachment> enableAttachment;
    std::unique_ptr<ComboAttachment> typeAttachment;
    std::unique_ptr<ComboAttachment> slopeAttachment;
    std::unique_ptr<ComboAttachment> channelAttachment;
    std::unique_ptr<ButtonAttachment> dynamicAttachment;
    std::unique_ptr<ComboAttachment> dynamicModeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandPanel)
};
} // namespace nodo::eq
