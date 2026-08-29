nodo_add_plugin(nodo_eq
    PRODUCT_NAME "Nodo EQ"
    PLUGIN_CODE  "Neq1"
    DESCRIPTION  "Parametric equaliser with dynamic bands and real-time analyser")

target_sources(nodo_eq PRIVATE
    source/EqBand.cpp
    source/EqEngine.cpp
    source/EqParameters.cpp
    source/FactoryPresets.cpp
    source/PluginProcessor.cpp
    source/PluginEditor.cpp
    source/EqCurveComponent.cpp
    source/BandPanel.cpp
    source/PianoStrip.cpp)

target_include_directories(nodo_eq PRIVATE source)
