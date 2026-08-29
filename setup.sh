#!/usr/bin/env bash
#
# Descarga JUCE en la version exacta con la que se ha probado este proyecto.
# Ejecutalo una sola vez, despues de descomprimir el repositorio.

set -euo pipefail

JUCE_TAG="9.0.1"
JUCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/libs/JUCE"

if [ -d "$JUCE_DIR/modules" ]; then
    echo "JUCE ya esta en libs/JUCE, no hago nada."
    exit 0
fi

echo "Clonando JUCE $JUCE_TAG en libs/JUCE ..."
mkdir -p "$(dirname "$JUCE_DIR")"
git clone --depth 1 --branch "$JUCE_TAG" https://github.com/juce-framework/JUCE.git "$JUCE_DIR"

echo
echo "Listo. Ahora:"
echo "  cmake -B build -DCMAKE_BUILD_TYPE=Release"
echo "  cmake --build build --config Release"
