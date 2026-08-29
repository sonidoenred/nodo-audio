nodo_add_plugin(nodo_delay
    PRODUCT_NAME "Nodo Delay"
    PLUGIN_CODE  "Ndl1"
    DESCRIPTION  "Stereo delay with tape time behaviour, drive and lo-fi in the loop")

target_sources(nodo_delay PRIVATE
    source/DelayParameters.cpp
    source/TapPattern.cpp
    source/DelayEngine.cpp
    source/FactoryPresets.cpp
    source/PluginProcessor.cpp
    source/PluginEditor.cpp
    source/DelayDisplay.cpp)

target_include_directories(nodo_delay PRIVATE source)
