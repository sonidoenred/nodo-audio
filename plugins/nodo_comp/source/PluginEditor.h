#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <nodo_ui/nodo_ui.h>

#include "PluginProcessor.h"
#include "CompDisplay.h"

namespace nodo::comp
{
class NodoCompEditor : public juce::AudioProcessorEditor
{
public:
    explicit NodoCompEditor (NodoCompProcessor&);
    ~NodoCompEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void storeEditorSize();
    void refreshStyleButtons (int styleIndex);
    void refreshSidechainState();

    NodoCompProcessor& processor;
    NodoLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 700 };

    NodoHeader header;
    CompDisplay display;

    // The four that get moved on every track.
    NodoKnob thresholdKnob { "Threshold" };
    NodoKnob ratioKnob     { "Ratio" };
    NodoKnob attackKnob    { "Attack" };
    NodoKnob releaseKnob   { "Release" };

    // The ones that get set once. Thin rather than round on purpose: see
    // NodoSlimSlider for why.
    NodoSlimSlider kneeSlider      { "Knee" };
    NodoSlimSlider rangeSlider     { "Range" };
    NodoSlimSlider lookaheadSlider { "Lookahead" };
    NodoSlimSlider holdSlider      { "Hold" };

    NodoKnob makeupKnob     { "Makeup" };
    NodoKnob mixKnob        { "Mix" };
    NodoKnob linkKnob       { "Link" };
    NodoKnob scHighpassKnob { "SC High" };
    NodoKnob scLowpassKnob  { "SC Low" };

    std::array<juce::TextButton, numStyles> styleButtons;
    juce::Label styleHint;

    juce::ComboBox detectionBox, directionBox, sidechainBox, oversamplingBox;
    juce::TextButton listenButton { "LISTEN" };
    juce::TextButton autoThresholdButton { "THR" };
    juce::TextButton autoReleaseButton { "REL" };
    juce::TextButton autoGainButton { "GAIN" };

    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<ButtonAttachment> bypassAttachment, listenAttachment,
                                      autoGainAttachment, autoThresholdAttachment,
                                      autoReleaseAttachment;
    std::unique_ptr<ComboAttachment> detectionAttachment, directionAttachment,
                                     sidechainAttachment, oversamplingAttachment;
    std::unique_ptr<juce::ParameterAttachment> styleAttachment;

    juce::Rectangle<int> detectorCaption, sidechainCaption, outputCaption,
                         compressionCaption, autoCaption, groupDivider;
    juce::String sidechainNote;

    juce::ComponentBoundsConstrainer constrainer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodoCompEditor)
};
} // namespace nodo::comp
