#!/usr/bin/env bash
# package.sh - gera os instaladores para Linux e macOS.
#
#   ./package.sh            -> dist/delta-linux-<arch>.tar.gz + .run   (Linux)
#                              dist/Delta-<arch>.app + .zip [+ .dmg]   (macOS)
#   ./package.sh --no-build -> usa o bin/delta ja compilado
#
# Windows tem o proprio empacotador: package.bat -> dist/Delta.exe (arquivo unico).
set -euo pipefail
cd "$(dirname "$0")"

OS="$(uname -s)"
ARCH="$(uname -m)"
BUILD=1
for a in "$@"; do [ "$a" = "--no-build" ] && BUILD=0; done

MAKE=make
command -v make >/dev/null 2>&1 || MAKE=gmake

if [ "$BUILD" = "1" ]; then
    echo "==> compilando ($OS/$ARCH)"
    $MAKE clean >/dev/null 2>&1 || true
    $MAKE >/dev/null
fi

[ -x bin/delta ] || { echo "erro: bin/delta nao existe (rode ./build.sh)"; exit 1; }
VER="$(./bin/delta --version 2>/dev/null | awk '{print $3}')"
[ -n "$VER" ] || VER="0.0.0"
mkdir -p dist

README_TXT='Delta Agent
===========

Um agente de codigo de terminal. Este pacote contem o executavel e um
instalador que cria a pasta de dados, poe `delta` no PATH e cria o atalho.

Instalar com a tela (abre no navegador):
    ./install.sh

Instalar sem tela:
    ./install.sh --headless ~/.deltaCore

Depois:
    delta                 # abre o CLI (retoma o ultimo chat desta pasta)
    delta wiki            # documentacao no navegador
    /login                # dentro do CLI: colar a chave do provedor

Precisa de curl em runtime (para falar com o provedor do modelo).
'

if [ "$OS" = "Linux" ]; then
    NAME="delta-linux-$ARCH"
    STAGE="build/pkg/$NAME"
    rm -rf "$STAGE"; mkdir -p "$STAGE"
    cp bin/delta "$STAGE/delta"
    chmod 755 "$STAGE/delta"
    printf '%s' "$README_TXT" > "$STAGE/LEIA-ME.txt"

    cat > "$STAGE/install.sh" <<'SH'
#!/bin/sh
# install.sh - instala o Delta (Linux). Sem tela: --headless <pasta>
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
if [ "${1:-}" = "--headless" ]; then
    exec "$HERE/delta" --install "${2:-$HOME/.deltaCore}"
fi
exec "$HERE/delta" --setup
SH
    chmod 755 "$STAGE/install.sh"

    ( cd build/pkg && tar czf "../../dist/$NAME.tar.gz" "$NAME" )
    echo "==> dist/$NAME.tar.gz"

    # auto-extraivel: ./delta-linux-x86_64.run
    RUN="dist/$NAME.run"
    {
        cat <<'SH'
#!/bin/sh
# Instalador auto-extraivel do Delta Agent.
set -e
LINE=$(awk '/^__PAYLOAD__$/ { print NR + 1; exit 0 }' "$0")
TMP=$(mktemp -d "${TMPDIR:-/tmp}/delta.XXXXXX")
trap 'rm -rf "$TMP"' EXIT
echo "==> extraindo em $TMP"
tail -n +"$LINE" "$0" | tar xz -C "$TMP"
cd "$TMP"/*
if [ "$1" = "--headless" ]; then
    exec ./delta --install "${2:-$HOME/.deltaCore}"
fi
exec ./delta --setup
__PAYLOAD__
SH
        cat "dist/$NAME.tar.gz"
    } > "$RUN"
    chmod 755 "$RUN"
    echo "==> $RUN"

elif [ "$OS" = "Darwin" ]; then
    APP="dist/Delta.app"
    rm -rf "$APP"
    mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
    cp bin/delta "$APP/Contents/MacOS/delta-bin"
    chmod 755 "$APP/Contents/MacOS/delta-bin"
    # app de console: abrir pelo Finder precisa de um Terminal
    cat > "$APP/Contents/MacOS/Delta" <<'LAUNCH'
#!/bin/sh
HERE=$(cd "$(dirname "$0")" && pwd)
osascript -e "tell application \"Terminal\" to do script \"'$HERE/delta-bin'\"" -e 'tell application "Terminal" to activate'
LAUNCH
    chmod 755 "$APP/Contents/MacOS/Delta"
    cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>Delta</string>
  <key>CFBundleDisplayName</key><string>Delta Agent</string>
  <key>CFBundleIdentifier</key><string>dev.delta.agent</string>
  <key>CFBundleExecutable</key><string>Delta</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>$VER</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
</dict>
</plist>
PLIST
    printf '%s' "$README_TXT" > "$APP/Contents/Resources/LEIA-ME.txt"

    # um binario "cru" tambem, para quem prefere o terminal
    mkdir -p "build/pkg"
    cp bin/delta "build/pkg/delta"
    chmod 755 "build/pkg/delta"
    printf '%s' "$README_TXT" > "build/pkg/LEIA-ME.txt"

    ZIP="dist/Delta-macos-$ARCH.zip"
    rm -f "$ZIP"
    ( cd dist && zip -qry "../$ZIP" "Delta.app" )
    echo "==> $ZIP"

    if command -v hdiutil >/dev/null 2>&1; then
        rm -rf build/dmg && mkdir -p build/dmg
        cp -R "$APP" build/dmg/
        ln -s /Applications build/dmg/Applications
        DMG="dist/Delta-macos-$ARCH.dmg"
        rm -f "$DMG"
        hdiutil create -quiet -volname "Delta Agent" -srcfolder build/dmg \
            -ov -format UDZO "$DMG"
        echo "==> $DMG"
    else
        echo "  (hdiutil nao encontrado: gerando so o .zip)"
    fi

else
    echo "SO nao suportado por este script: $OS (use package.bat no Windows)"
    exit 1
fi

echo
ls -la dist/ | tail -6
