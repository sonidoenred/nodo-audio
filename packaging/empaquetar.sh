#!/usr/bin/env bash
#
# Prepara los ZIP que se descargan desde sonidoenred.com.
#
#   ./packaging/empaquetar.sh              todos los plugins ya compilados
#   ./packaging/empaquetar.sh nodo_eq      solo uno
#
# Un ZIP por plugin y por sistema, no uno con la suite entera: cada plugin
# tiene su propia ficha en la web y su propia version, y quien entra a por el
# ecualizador no tiene por que bajarse siete.
#
# El ZIP no lleva instalador ni nada ejecutable aparte del propio plugin: eso
# es lo que mantiene a SmartScreen y a los antivirus fuera de la conversacion.

set -euo pipefail

cd "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

VERSION="$(sed -n 's/^project(NodoAudio VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
[ -n "$VERSION" ] || { echo "No he podido leer la version de CMakeLists.txt"; exit 1; }

case "$(uname -s)" in
    Darwin) SYSTEM=macOS  ;;
    Linux)  SYSTEM=Linux  ;;
    MINGW*|MSYS*|CYGWIN*) SYSTEM=Windows ;;
    *) echo "Sistema desconocido: $(uname -s)"; exit 1 ;;
esac

PLUGINS=("$@")
if [ ${#PLUGINS[@]} -eq 0 ]; then
    PLUGINS=(nodo_eq nodo_comp nodo_limit nodo_ess nodo_gate nodo_delay nodo_verb)
fi

OUTDIR="dist"
mkdir -p "$OUTDIR"

echo "==> Nodo $VERSION para $SYSTEM"

for DIR in "${PLUGINS[@]}"; do
    ART="build/plugins/$DIR/${DIR}_artefacts/Release"

    if [ ! -d "$ART" ]; then
        echo "    $DIR: sin compilar, lo salto"
        continue
    fi

    VST3="$(find "$ART/VST3" -maxdepth 1 -name "*.vst3" | head -1)"
    [ -n "$VST3" ] || { echo "    $DIR: no encuentro el VST3, lo salto"; continue; }

    PRODUCT="$(basename "$VST3" .vst3)"        # "Nodo EQ"
    SLUG="${PRODUCT// /-}"                     # "Nodo-EQ"
    STAGE="$OUTDIR/$PRODUCT $VERSION"

    rm -rf "$STAGE"
    mkdir -p "$STAGE"

    cp -R "$VST3" "$STAGE/"

    # El Audio Unit solo existe en macOS, y Logic solo carga eso.
    if [ -d "$ART/AU" ]; then
        cp -R "$ART/AU/$PRODUCT.component" "$STAGE/" 2>/dev/null || true
    fi

    # La aplicacion suelta: .app en macOS, .exe en Windows, binario pelado en
    # Linux. Se copia si esta; no es la razon por la que nadie se baja esto.
    for CANDIDATE in "$ART/Standalone/$PRODUCT.app" \
                     "$ART/Standalone/$PRODUCT.exe" \
                     "$ART/Standalone/$PRODUCT"; do
        if [ -e "$CANDIDATE" ]; then
            cp -R "$CANDIDATE" "$STAGE/"
            break
        fi
    done

    # Las instrucciones, en los dos idiomas, con el nombre del plugin metido.
    fill() {
        sed -e "s/%PRODUCTO%/$PRODUCT/g" -e "s/%VERSION%/$VERSION/g" "$1" > "$2"
    }
    fill "packaging/plantillas/INSTALACION_$SYSTEM.txt" "$STAGE/INSTALACION.txt"
    fill "packaging/plantillas/INSTALL_$SYSTEM.txt"     "$STAGE/INSTALL.txt"

    [ -f LICENSE.txt ] && cp LICENSE.txt "$STAGE/"

    ZIP="$OUTDIR/$SLUG-$VERSION-$SYSTEM.zip"
    rm -f "$ZIP"

    # -X para no meter los metadatos de macOS: sin eso el ZIP lleva un __MACOSX
    # que en Windows aparece como una carpeta de basura al descomprimir.
    #
    # Los runners de Windows no siempre traen zip, pero siempre traen 7z. El
    # orden importa: zip conserva el bit de ejecucion y los enlaces simbolicos
    # del .app de macOS, y 7z solo se usa donde no hay ninguna de las dos cosas
    # que conservar.
    if command -v zip >/dev/null 2>&1; then
        (cd "$OUTDIR" && zip -qrX "$(basename "$ZIP")" "$(basename "$STAGE")")
    elif command -v 7z >/dev/null 2>&1; then
        (cd "$OUTDIR" && 7z a -tzip -bso0 -bsp0 "$(basename "$ZIP")" "$(basename "$STAGE")")
    else
        echo "    hace falta zip o 7z para empaquetar"; exit 1
    fi
    rm -rf "$STAGE"

    SIZE="$(du -h "$ZIP" | cut -f1)"
    if command -v shasum >/dev/null 2>&1; then
        SUM="$(shasum -a 256 "$ZIP" | cut -c1-16)"
    else
        SUM="$(sha256sum "$ZIP" | cut -c1-16)"
    fi

    echo "    $ZIP  ($SIZE, sha256 $SUM...)"
done

echo
echo "Listo. Los ZIP estan en $OUTDIR/"
