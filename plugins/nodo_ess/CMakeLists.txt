nodo_add_plugin(nodo_ess
    PRODUCT_NAME "Nodo Ess"
    PLUGIN_CODE  "Nes1"
    DESCRIPTION  "De-esser with adaptive detection, split or wideband")

target_sources(nodo_ess PRIVATE
    source/EssParameters.cpp
    source/EssEngine.cpp
    source/FactoryPresets.cpp
    source/PluginProcessor.cpp
    source/PluginEditor.cpp
    source/EssDisplay.cpp)

target_include_directories(nodo_ess PRIVATE source)
