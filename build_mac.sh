#!/usr/bin/env bash
#
# Compila la suite Nodo en macOS y la deja instalada para tu usuario.
# Un solo comando:  ./build_mac.sh
#
# No hace falta firmar nada: lo que compilas en tu propio Mac no lleva la
# marca de cuarentena que macOS le pone a lo que se descarga de internet.

set -euo pipefail

cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "==> Comprobando herramientas"

if ! xcode-select -p >/dev/null 2>&1; then
    echo "Faltan las Command Line Tools de Xcode (son gratis)."
    echo "Ejecuta:  xcode-select --install"
    echo "y vuelve a lanzar este script cuando termine la instalacion."
    exit 1
fi

if ! command -v cmake >/dev/null 2>&1; then
    echo "Falta CMake. Instalalo con:  brew install cmake"
    echo "Si no tienes Homebrew:  https://cmake.org/download/  (instalador .dmg)"
    exit 1
fi

if [ ! -d libs/JUCE/modules ]; then
    echo "==> Descargando JUCE"
    ./setup.sh
fi

JOBS="$(sysctl -n hw.ncpu)"

echo "==> Configurando"
cmake -B build -DCMAKE_BUILD_TYPE=Release

echo "==> Compilando con $JOBS nucleos (la primera vez tarda un rato)"
cmake --build build --config Release -j "$JOBS"

echo
echo "==> Comprobando el DSP"
./build/tests/nodo_tests_artefacts/Release/nodo_tests | tail -3

echo
echo "==> Listo"

# Cada plugin de la suite, con su carpeta de fuentes y su nombre de producto.
PLUGINS=(
    "nodo_eq:Nodo EQ"
    "nodo_comp:Nodo Comp"
    "nodo_limit:Nodo Limit"
    "nodo_ess:Nodo Ess"
    "nodo_gate:Nodo Gate"
    "nodo_delay:Nodo Delay"
    "nodo_verb:Nodo Verb"
)

for ENTRY in "${PLUGINS[@]}"; do
    DIR="${ENTRY%%:*}"
    NAME="${ENTRY#*:}"

    VST3="$HOME/Library/Audio/Plug-Ins/VST3/$NAME.vst3"
    AU="$HOME/Library/Audio/Plug-Ins/Components/$NAME.component"
    APP="build/plugins/$DIR/${DIR}_artefacts/Release/Standalone/$NAME.app"

    echo
    echo "  $NAME"
    [ -d "$VST3" ] && echo "    VST3        $VST3"
    [ -d "$AU" ]   && echo "    AU          $AU"
    [ -d "$APP" ]  && echo "    Standalone  $APP"
    [ -d "$VST3" ] && echo "    Arquitecturas  $(lipo -archs "$VST3/Contents/MacOS/$NAME" 2>/dev/null || echo '?')"
done

echo
echo "Ahora abre Reaper y vuelve a escanear los plugins:"
echo "  Preferences > Plug-ins > VST > Re-scan"
echo "Aparecen todos bajo  SonidoenRed:"
