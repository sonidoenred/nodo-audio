nodo_add_plugin(nodo_limit
    PRODUCT_NAME "Nodo Limit"
    PLUGIN_CODE  "Nlm1"
    DESCRIPTION  "True peak limiter with lookahead, loudness metering and dither")

target_sources(nodo_limit PRIVATE
    source/LimitParameters.cpp
    source/LimitEngine.cpp
    source/FactoryPresets.cpp
    source/PluginProcessor.cpp
    source/PluginEditor.cpp
    source/LimitDisplay.cpp)

target_include_directories(nodo_limit PRIVATE source)
