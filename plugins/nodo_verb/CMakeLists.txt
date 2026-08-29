nodo_add_plugin(nodo_verb
    PRODUCT_NAME "Nodo Verb"
    PLUGIN_CODE  "Nvb1"
    DESCRIPTION  "Algorithmic reverb with a decay time you can shape per frequency")

target_sources(nodo_verb PRIVATE
    source/VerbParameters.cpp
    source/DecayCurve.cpp
    source/VerbEngine.cpp
    source/FactoryPresets.cpp
    source/PluginProcessor.cpp
    source/PluginEditor.cpp
    source/VerbDisplay.cpp)

target_include_directories(nodo_verb PRIVATE source)
