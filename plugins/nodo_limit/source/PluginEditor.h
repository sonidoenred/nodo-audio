#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <nodo_ui/nodo_ui.h>

#include "PluginProcessor.h"
#include "LimitDisplay.h"

namespace nodo::limit
{
class NodoLimitEditor : public juce::AudioProcessorEditor
{
public:
    explicit NodoLimitEditor (NodoLimitProcessor&);
    ~NodoLimitEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void storeEditorSize();
    void refreshStyleButtons (int styleIndex);
    void refreshSidechainState();

    NodoLimitProcessor& processor;
    NodoLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 700 };

    NodoHeader header;
    LimitDisplay display;

    NodoKnob gainKnob    { "Gain" };
    NodoKnob ceilingKnob { "Ceiling" };
    NodoKnob releaseKnob { "Release" };

    NodoSlimSlider lookaheadSlider     { "Lookahead" };
    NodoSlimSlider transientLinkSlider { "Transient link" };
    NodoSlimSlider releaseLinkSlider   { "Release link" };

    std::array<juce::TextButton, numStyles> styleButtons;
    juce::Label styleHint;

    juce::TextButton truePeakButton { "TP" };
    juce::TextButton dcButton       { "DC" };
    juce::TextButton triggerButton  { "EXT" };
    juce::TextButton deltaButton    { "DELTA" };
    juce::ComboBox ditherBox, targetBox;
    juce::TextButton applyTargetButton { "SET GAIN" };
    juce::TextButton matchButton { "MATCH" };

    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<ButtonAttachment> bypassAttachment, truePeakAttachment,
                                      dcAttachment, triggerAttachment, deltaAttachment;
    std::unique_ptr<ComboAttachment> ditherAttachment, targetAttachment;
    std::unique_ptr<ButtonAttachment> matchAttachment;
    std::unique_ptr<juce::ParameterAttachment> styleAttachment;

    juce::Rectangle<int> optionsCaption, linkCaption, limitingCaption, groupDivider;
    juce::String sidechainNote;

    juce::ComponentBoundsConstrainer constrainer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodoLimitEditor)
};
} // namespace nodo::limit
