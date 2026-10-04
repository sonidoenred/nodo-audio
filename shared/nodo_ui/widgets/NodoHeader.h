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

    /** Optional companion to setLevelSource, with the same index order. Given
        both, the meters also print the peak reached and the running RMS.
    */
    void setRmsSource (std::function<float (int)> source);

    /** Con esto puesto, pulsar el nombre del plugin abre un menu con «Report a
        problem». Sin esto, el nombre es solo texto.

        Lo da el editor y no la cabecera porque el informe lo arma el procesador,
        que es quien sabe a que frecuencia de muestreo esta trabajando y en que
        anfitrion esta cargado.
    */
    void setProblemReportSource (std::function<juce::String()> source);

    void setSlot (bool isB);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshPresetDisplay();
    void showPresetMenu();
    void showBrandMenu();
    void showProblemReport();
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

    std::function<juce::String()> problemReportSource;
    juce::Rectangle<int> wordmarkArea;
    bool wordmarkHovered { false };

    std::unique_ptr<juce::AlertWindow> nameWindow;
    std::unique_ptr<juce::AlertWindow> reportWindow;

    /*  El cuadro de texto del informe se crea a mano y se le pasa a la ventana
        como componente propio. Los editores que monta AlertWindow por su cuenta
        son de una sola linea y no hay forma de estirarlos: el informe salia
        como una raya azul de una linea.
    */
    std::unique_ptr<juce::TextEditor> reportEditor;
    bool slotIsB { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodoHeader)
};
} // namespace nodo
