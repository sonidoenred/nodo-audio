#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <nodo_ui/nodo_ui.h>

#include "PluginProcessor.h"
#include "GateDisplay.h"

namespace nodo::gate
{
class NodoGateEditor : public juce::AudioProcessorEditor
{
public:
    explicit NodoGateEditor (NodoGateProcessor&);
    ~NodoGateEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void storeEditorSize();
    void refreshStyleButtons (int styleIndex);
    void refreshSidechainState();
    void refreshTriggerState (int triggerIndex);

    NodoGateProcessor& processor;
    NodoLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 700 };

    NodoHeader header;
    GateDisplay display;

    // The four that get moved on every track.
    NodoKnob thresholdKnob { "Threshold" };
    NodoKnob rangeKnob     { "Range" };
    NodoKnob attackKnob    { "Attack" };
    NodoKnob releaseKnob   { "Release" };

    NodoKnob ratioKnob      { "Ratio" };
    NodoKnob mixKnob        { "Mix" };
    NodoKnob scHighpassKnob { "SC High" };
    NodoKnob scLowpassKnob  { "SC Low" };

    // The ones that get set once. Thin rather than round on purpose.
    NodoSlimSlider holdSlider       { "Hold" };
    NodoSlimSlider hysteresisSlider { "Hysteresis" };
    NodoSlimSlider kneeSlider       { "Knee" };
    NodoSlimSlider lookaheadSlider  { "Lookahead" };

    std::array<juce::TextButton, numStyles> styleButtons;
    juce::Label styleHint;

    juce::ComboBox directionBox, channelModeBox, triggerBox, sidechainBox;
    juce::TextButton listenButton { "LISTEN" };

    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<ButtonAttachment> bypassAttachment, listenAttachment;
    std::unique_ptr<ComboAttachment> directionAttachment, channelModeAttachment,
                                     triggerAttachment, sidechainAttachment;
    std::unique_ptr<juce::ParameterAttachment> styleAttachment, triggerWatcher;

    juce::Rectangle<int> detectorCaption, sidechainCaption, outputCaption, gatingCaption,
                         groupDivider, noteArea;
    juce::String sidechainNote, triggerNote;

    juce::ComponentBoundsConstrainer constrainer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodoGateEditor)
};
} // namespace nodo::gate
