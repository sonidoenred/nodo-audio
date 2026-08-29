#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <nodo_ui/nodo_ui.h>

#include "PluginProcessor.h"
#include "EqCurveComponent.h"
#include "PianoStrip.h"
#include "BandPanel.h"

namespace nodo::eq
{
class NodoEqEditor : public juce::AudioProcessorEditor
{
public:
    explicit NodoEqEditor (NodoEqProcessor&);
    ~NodoEqEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void storeEditorSize();

    NodoEqProcessor& processor;
    NodoLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 700 };

    NodoHeader header;
    PianoStrip pianoStrip;
    EqCurveComponent curve;
    BandPanel bandPanel;

    NodoKnob outputKnob { "Output" };
    juce::TextButton autoGainButton { "AUTO" };
    juce::TextButton pianoRollButton { "KEYS" };
    juce::ComboBox oversamplingBox;
    juce::ComboBox filterModeBox;
    juce::ComboBox dynSidechainBox;
    juce::ComboBox analyserBox;
    juce::ComboBox rangeBox;

    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<ButtonAttachment> bypassAttachment;
    std::unique_ptr<ButtonAttachment> autoGainAttachment;
    std::unique_ptr<ComboAttachment> oversamplingAttachment;
    std::unique_ptr<ComboAttachment> analyserAttachment;
    std::unique_ptr<ComboAttachment> filterModeAttachment;
    std::unique_ptr<ComboAttachment> dynSidechainAttachment;

    juce::Rectangle<int> processGroupCaption, viewGroupCaption, groupDivider;

    juce::ComponentBoundsConstrainer constrainer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodoEqEditor)
};
} // namespace nodo::eq
