#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <nodo_ui/nodo_ui.h>

#include "PluginProcessor.h"
#include "EssDisplay.h"

namespace nodo::ess
{
class NodoEssEditor : public juce::AudioProcessorEditor
{
public:
    explicit NodoEssEditor (NodoEssProcessor&);
    ~NodoEssEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void storeEditorSize();

    NodoEssProcessor& processor;
    NodoLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 700 };

    NodoHeader header;
    EssDisplay display;

    NodoKnob frequencyKnob { "Frequency" };
    NodoKnob detectorTopKnob { "Det. top" };
    NodoKnob thresholdKnob { "Threshold" };
    NodoKnob rangeKnob     { "Range" };
    NodoKnob attackKnob    { "Attack" };
    NodoKnob releaseKnob   { "Release" };

    NodoSlimSlider kneeSlider      { "Knee" };
    NodoSlimSlider lookaheadSlider { "Lookahead" };
    NodoSlimSlider linkSlider      { "Stereo link" };

    juce::ComboBox modeBox, detectionBox, channelBox, monitorBox;

    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<ButtonAttachment> bypassAttachment;
    std::unique_ptr<ComboAttachment> modeAttachment, detectionAttachment,
                                     channelAttachment, monitorAttachment;
    std::unique_ptr<juce::ParameterAttachment> channelWatcher;

    juce::Rectangle<int> settingsCaption, deessingCaption, groupDivider;

    juce::ComponentBoundsConstrainer constrainer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodoEssEditor)
};
} // namespace nodo::ess
