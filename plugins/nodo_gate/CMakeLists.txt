nodo_add_plugin(nodo_gate
    PRODUCT_NAME "Nodo Gate"
    PLUGIN_CODE  "Ngt1"
    DESCRIPTION  "Gate and expander with hysteresis, ducking and MIDI trigger"
    NEEDS_MIDI)

target_sources(nodo_gate PRIVATE
    source/GateParameters.cpp
    source/GateEngine.cpp
    source/FactoryPresets.cpp
    source/PluginProcessor.cpp
    source/PluginEditor.cpp
    source/GateDisplay.cpp)

target_include_directories(nodo_gate PRIVATE source)
