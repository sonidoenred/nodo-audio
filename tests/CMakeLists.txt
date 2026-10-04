juce_add_console_app(nodo_tests PRODUCT_NAME "Nodo Tests")

target_sources(nodo_tests PRIVATE
    DspTests.cpp
    CompTests.cpp
    LimitTests.cpp
    EssTests.cpp
    GateTests.cpp
    DelayTests.cpp
    VerbTests.cpp
    ${CMAKE_SOURCE_DIR}/plugins/nodo_verb/source/VerbEngine.cpp
    ${CMAKE_SOURCE_DIR}/plugins/nodo_verb/source/VerbParameters.cpp
    ${CMAKE_SOURCE_DIR}/plugins/nodo_verb/source/DecayCurve.cpp
    ${CMAKE_SOURCE_DIR}/plugins/nodo_verb/source/FactoryPresets.cpp
    ${CMAKE_SOURCE_DIR}/plugins/nodo_delay/source/DelayEngine.cpp
    ${CMAKE_SOURCE_DIR}/plugins/nodo_delay/source/DelayParameters.cpp
    ${CMAKE_SOURCE_DIR}/plugins/nodo_delay/source/TapPattern.cpp
    ${CMAKE_SOURCE_DIR}/plugins/nodo_delay/source/FactoryPresets.cpp
    ${CMAKE_SOURCE_DIR}/plugins/nodo_gate/source/GateEngine.cpp
    ${CMAKE_SOURCE_DIR}/plugins/nodo_gate/source/GateParameters.cpp
    ${CMAKE_SOURCE_DIR}/plugins/nodo_gate/source/FactoryPresets.cpp
    ${CMAKE_SOURCE_DIR}/plugins/nodo_ess/source/EssEngine.cpp
    ${CMAKE_SOURCE_DIR}/plugins/nodo_ess/source/EssParameters.cpp
    ${CMAKE_SOURCE_DIR}/plugins/nodo_limit/source/LimitEngine.cpp
    ${CMAKE_SOURCE_DIR}/plugins/nodo_limit/source/LimitParameters.cpp
    ${CMAKE_SOURCE_DIR}/plugins/nodo_comp/source/CompEngine.cpp
    ${CMAKE_SOURCE_DIR}/plugins/nodo_comp/source/CompParameters.cpp
    ${CMAKE_SOURCE_DIR}/plugins/nodo_comp/source/FactoryPresets.cpp
    ${CMAKE_SOURCE_DIR}/plugins/nodo_eq/source/EqBand.cpp
    ${CMAKE_SOURCE_DIR}/plugins/nodo_eq/source/EqEngine.cpp
    ${CMAKE_SOURCE_DIR}/plugins/nodo_eq/source/EqParameters.cpp
    ${CMAKE_SOURCE_DIR}/plugins/nodo_eq/source/FactoryPresets.cpp

    # El procesador entero del ecualizador, con su interfaz detras.
    #
    # Cuesta un minuto mas de compilacion y trae media biblioteca grafica a un
    # programa de consola, y aun asi compensa: el delta y la latencia del
    # bypass solo existen dentro de processBlock, que es codigo que ninguna
    # prueba del motor toca. Son justo las dos cosas que se pueden estropear
    # sin que se note hasta que alguien mezcla con ellas.
    ${CMAKE_SOURCE_DIR}/plugins/nodo_eq/source/PluginProcessor.cpp
    ${CMAKE_SOURCE_DIR}/plugins/nodo_eq/source/PluginEditor.cpp
    ${CMAKE_SOURCE_DIR}/plugins/nodo_eq/source/EqCurveComponent.cpp
    ${CMAKE_SOURCE_DIR}/plugins/nodo_eq/source/BandPanel.cpp
    ${CMAKE_SOURCE_DIR}/plugins/nodo_eq/source/PianoStrip.cpp)

target_compile_definitions(nodo_tests PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
    # Lo define el envoltorio del plugin, que aqui no existe. El procesador solo
    # lo usa para responder getName(), y en un programa de consola nadie
    # pregunta.
    JucePlugin_Name="Nodo EQ"
    JucePlugin_VersionString="test")

target_include_directories(nodo_tests PRIVATE ${CMAKE_SOURCE_DIR}/plugins/nodo_eq/source)

target_link_libraries(nodo_tests PRIVATE
    nodo_core
    nodo_ui
    juce::juce_dsp
    juce::juce_audio_utils
    juce::juce_audio_processors
    PUBLIC
    juce::juce_recommended_config_flags)


# Throughput measurement. Not part of the test suite: it reports numbers rather
# than passing or failing, and the numbers depend on the machine.
juce_add_console_app(nodo_bench PRODUCT_NAME "Nodo Bench")

target_sources(nodo_bench PRIVATE
    Benchmark.cpp
    ${CMAKE_SOURCE_DIR}/plugins/nodo_verb/source/VerbEngine.cpp
    ${CMAKE_SOURCE_DIR}/plugins/nodo_verb/source/VerbParameters.cpp
    ${CMAKE_SOURCE_DIR}/plugins/nodo_verb/source/DecayCurve.cpp
    ${CMAKE_SOURCE_DIR}/plugins/nodo_comp/source/CompEngine.cpp
    ${CMAKE_SOURCE_DIR}/plugins/nodo_comp/source/CompParameters.cpp
    ${CMAKE_SOURCE_DIR}/plugins/nodo_eq/source/EqBand.cpp
    ${CMAKE_SOURCE_DIR}/plugins/nodo_eq/source/EqEngine.cpp
    ${CMAKE_SOURCE_DIR}/plugins/nodo_eq/source/EqParameters.cpp)

target_compile_definitions(nodo_bench PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)

target_include_directories(nodo_bench PRIVATE ${CMAKE_SOURCE_DIR}/plugins/nodo_eq/source)

target_link_libraries(nodo_bench PRIVATE
    nodo_core juce::juce_dsp juce::juce_audio_processors
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)
