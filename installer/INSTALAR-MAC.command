#!/bin/bash
# Delta Agent - instalacao no macOS (duplo clique neste arquivo).
set -e
cd "$(dirname "$0")"

echo
echo "  Delta Agent - macOS"
echo "  ==================="
echo

if ! xcode-select -p >/dev/null 2>&1; then
    echo "  Preciso das Xcode Command Line Tools (compilador C, gratuito)."
    echo "  Vou abrir o instalador da Apple; quando terminar, rode este arquivo de novo."
    echo
    xcode-select --install || true
    read -r -p "  Enter para sair..."
    exit 1
fi

echo "  compilando..."
./build.sh

echo
echo "  abrindo o instalador no navegador..."
./bin/delta --setup
