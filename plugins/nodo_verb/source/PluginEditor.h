#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <nodo_ui/nodo_ui.h>

#include "PluginProcessor.h"
#include "VerbDisplay.h"

namespace nodo::verb
{
class NodoVerbEditor : public juce::AudioProcessorEditor
{
public:
    explicit NodoVerbEditor (NodoVerbProcessor&);
    ~NodoVerbEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void storeEditorSize();

    NodoVerbProcessor& processor;
    NodoLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 700 };

    NodoHeader header;
    VerbDisplay display;

    // The four that get moved on every track.
    NodoKnob decayKnob    { "Decay" };
    NodoKnob sizeKnob     { "Size" };
    NodoKnob predelayKnob { "Predelay" };
    NodoKnob mixKnob      { "Mix" };

    NodoKnob diffusionKnob { "Diffusion" };
    NodoKnob motionKnob    { "Motion" };
    NodoKnob preLowKnob    { "Low cut" };
    NodoKnob preHighKnob   { "High cut" };
    NodoKnob duckKnob      { "Duck" };

    NodoSlimSlider widthSlider       { "Width" };
    // Short captions: the group is already labelled DUCKING above them, and the
    // row is narrow enough that "Duck attack" would push the number off it.
    NodoSlimSlider duckAttackSlider  { "Attack" };
    NodoSlimSlider duckReleaseSlider { "Release" };
    NodoSlimSlider postMidQSlider    { "Post mid Q" };

    juce::TextButton freezeButton { "FREEZE" };

    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<ButtonAttachment> bypassAttachment, freezeAttachment;

    juce::Rectangle<int> spaceCaption, characterCaption, inputCaption, duckCaption, groupDivider;

    juce::ComponentBoundsConstrainer constrainer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodoVerbEditor)
};
} // namespace nodo::verb
