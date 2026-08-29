#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <nodo_ui/nodo_ui.h>

#include "PluginProcessor.h"
#include "DelayDisplay.h"

namespace nodo::delay
{
class NodoDelayEditor : public juce::AudioProcessorEditor,
                        private juce::Timer
{
public:
    explicit NodoDelayEditor (NodoDelayProcessor&);
    ~NodoDelayEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void storeEditorSize();
    void timerCallback() override;
    void refreshSyncState (bool synced);
    void refreshLinkState (bool linked);
    void refreshMultiTapState (bool on);

    /** Which group of controls the footer is showing. The window would have to
        be half as tall again to show all three at once, and a delay you cannot
        see the repeats of is not worth the extra rows.
    */
    enum class Page { main = 0, taps, modulation };

    void setPage (Page newPage);
    void applyPageVisibility();

    NodoDelayProcessor& processor;
    NodoLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 700 };

    NodoHeader header;
    DelayDisplay display;

    NodoKnob timeLeftKnob  { "Time L" };
    NodoKnob timeRightKnob { "Time R" };
    NodoKnob feedbackKnob  { "Feedback" };
    NodoKnob mixKnob       { "Mix" };

    NodoKnob crossKnob     { "Cross" };
    NodoKnob highpassKnob  { "Loop HP" };
    NodoKnob lowpassKnob   { "Loop LP" };
    NodoKnob driveKnob     { "Drive" };
    NodoKnob loFiKnob      { "Lo-Fi" };
    NodoKnob modRateKnob   { "Rate" };
    NodoKnob modDepthKnob  { "Depth" };

    NodoSlimSlider panLeftSlider  { "Pan L" };
    NodoSlimSlider panRightSlider { "Pan R" };
    NodoSlimSlider widthSlider    { "Width" };

    juce::ComboBox noteLeftBox, noteRightBox, routingBox, timeModeBox;

    // ---- the taps page ----------------------------------------------------
    juce::TextButton multiTapButton { "MULTI-TAP" };
    NodoKnob divisionKnob  { "Steps" };
    NodoKnob tapSpreadKnob { "Spread" };
    juce::Label tapHint;

    // ---- the modulation page ----------------------------------------------
    struct LfoStrip
    {
        juce::ComboBox shapeBox, noteBox;
        juce::TextButton syncButton { "SYNC" };
        NodoSlimSlider rateSlider { "Rate" };
        NodoSlimSlider phaseSlider { "Phase" };
    };

    std::array<LfoStrip, numLfos> lfoStrips;
    NodoSlimSlider envAttackSlider  { "Env attack" };
    NodoSlimSlider envReleaseSlider { "Env release" };

    struct ModRow
    {
        juce::ComboBox sourceBox, targetBox;
        NodoSlimSlider amountSlider { "Amount" };
    };

    std::array<ModRow, numModSlots> modRows;

    std::array<juce::TextButton, 3> pageButtons;
    Page page { Page::main };

    juce::TextButton syncButton   { "SYNC" };
    juce::TextButton linkButton   { "LINK" };
    juce::TextButton invertButton { "INV" };
    juce::TextButton freezeButton { "FREEZE" };

    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<ButtonAttachment> bypassAttachment, syncAttachment, linkAttachment,
                                      invertAttachment, freezeAttachment, multiTapAttachment;
    std::unique_ptr<ComboAttachment> noteLeftAttachment, noteRightAttachment,
                                     routingAttachment, timeModeAttachment;
    std::array<std::unique_ptr<ButtonAttachment>, numLfos> lfoSyncAttachments;
    std::array<std::unique_ptr<ComboAttachment>, numLfos> lfoShapeAttachments, lfoNoteAttachments;
    std::array<std::unique_ptr<ComboAttachment>, numModSlots> modSourceAttachments, modTargetAttachments;
    std::unique_ptr<juce::ParameterAttachment> syncWatcher, linkWatcher, multiTapWatcher;

    juce::Rectangle<int> timeCaption, loopCaption, characterCaption, outputCaption,
                         groupDivider, tempoArea, lfoCaption, matrixCaption;
    std::array<juce::Rectangle<int>, 3> sourceMeters;
    juce::String tempoNote;

    juce::ComponentBoundsConstrainer constrainer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodoDelayEditor)
};
} // namespace nodo::delay
