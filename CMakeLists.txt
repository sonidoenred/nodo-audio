cmake_minimum_required(VERSION 3.22)

# Universal binary on Apple silicon + Intel, single arch elsewhere.
#
# These two have to come BEFORE project(). CMake reads them while it works out
# the toolchain, which is what project() does, so setting them afterwards is
# accepted, cached, and ignored — the first build of the project comes out
# single architecture and the second one, run against the now-populated cache,
# comes out universal. That is exactly what happened the first time this was
# built on a Mac: "Arquitecturas arm64" on a machine that had been told to make
# both.
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")

# La version que se publica en la web tiene que ser la misma que el plugin le
# dice al anfitrion: cuando alguien reporte un fallo, el numero que lee en la
# cabecera del plugin es el unico dato fiable que va a mandar.
project(NodoAudio VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_subdirectory(libs/JUCE)

include(cmake/NodoPlugin.cmake)

# Shared modules used by every plugin in the suite.
juce_add_module(${CMAKE_CURRENT_SOURCE_DIR}/shared/nodo_core)
juce_add_module(${CMAKE_CURRENT_SOURCE_DIR}/shared/nodo_ui)

add_subdirectory(plugins/nodo_eq)
add_subdirectory(plugins/nodo_comp)
add_subdirectory(plugins/nodo_limit)
add_subdirectory(plugins/nodo_ess)
add_subdirectory(plugins/nodo_gate)
add_subdirectory(plugins/nodo_delay)
add_subdirectory(plugins/nodo_verb)

option(NODO_BUILD_TESTS "Build the DSP verification console app" ON)

if(NODO_BUILD_TESTS)
    add_subdirectory(tests)
endif()
