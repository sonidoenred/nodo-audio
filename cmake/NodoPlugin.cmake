# NodoPlugin.cmake
#
# Every plugin in the suite is declared through nodo_add_plugin() so that the
# company fields, formats, compile definitions and shared module links stay
# identical across Nodo EQ, Comp, Delay and Verb. Change a policy here and all
# four plugins inherit it.

set(NODO_COMPANY_NAME    "SonidoenRed")
set(NODO_COMPANY_WEBSITE "https://sonidoenred.com")
set(NODO_COMPANY_COPY    "SonidoenRed")
set(NODO_MANUFACTURER_CODE "Sred")
set(NODO_BUNDLE_PREFIX   "com.sonidoenred")

function(nodo_add_plugin target)
    cmake_parse_arguments(NODO "NEEDS_MIDI" "PRODUCT_NAME;PLUGIN_CODE;DESCRIPTION" "" ${ARGN})

    # Only the gate asks for MIDI, for its trigger. A host that is told a plugin
    # takes no MIDI will never offer to route any to it, so this has to be
    # declared at build time rather than switched on with a parameter.
    set(_needs_midi FALSE)
    if(NODO_NEEDS_MIDI)
        set(_needs_midi TRUE)
    endif()

    # AU is Apple-only; VST3 and Standalone build everywhere.
    set(_formats VST3 Standalone)
    if(APPLE)
        list(APPEND _formats AU)
    endif()

    juce_add_plugin(${target}
        COMPANY_NAME                "${NODO_COMPANY_NAME}"
        COMPANY_WEBSITE             "${NODO_COMPANY_WEBSITE}"
        COMPANY_COPYRIGHT           "${NODO_COMPANY_COPY}"
        BUNDLE_ID                   "${NODO_BUNDLE_PREFIX}.${target}"
        PLUGIN_MANUFACTURER_CODE    "${NODO_MANUFACTURER_CODE}"
        PLUGIN_CODE                 "${NODO_PLUGIN_CODE}"
        PRODUCT_NAME                "${NODO_PRODUCT_NAME}"
        DESCRIPTION                 "${NODO_DESCRIPTION}"
        FORMATS                     ${_formats}
        IS_SYNTH                    FALSE
        NEEDS_MIDI_INPUT            ${_needs_midi}
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD     TRUE
        VST3_CATEGORIES             "Fx" "EQ"
        AU_MAIN_TYPE                "kAudioUnitType_Effect")

    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        # JUCE 9 does not draw a splash screen at all and warns that this flag is
        # ignored. It stays at 1 on purpose: it costs nothing, it is what the
        # free tier asked for in JUCE 7 and 8, and if a future version brings
        # the requirement back the build is already honest about it.
        JUCE_DISPLAY_SPLASH_SCREEN=1
        JUCE_REPORT_APP_USAGE=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1)

    target_link_libraries(${target} PRIVATE
        nodo_core
        nodo_ui
        juce::juce_audio_utils
        juce::juce_dsp
        PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
endfunction()
