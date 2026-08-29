nodo_add_plugin(nodo_comp
    PRODUCT_NAME "Nodo Comp"
    PLUGIN_CODE  "Ncm1"
    DESCRIPTION  "Compressor with selectable styles, external sidechain and lookahead")

target_sources(nodo_comp PRIVATE
    source/CompParameters.cpp
    source/CompEngine.cpp
    source/FactoryPresets.cpp
    source/PluginProcessor.cpp
    source/PluginEditor.cpp
    source/CompDisplay.cpp)

target_include_directories(nodo_comp PRIVATE source)
