#!/usr/bin/env bash
# build.sh - compila o Delta no Linux e no macOS.
#   ./build.sh          -> bin/delta
#   ./build.sh --dist   -> dist/Delta
#   ./build.sh --test   -> compila e roda os testes de nucleo
#
# Requisitos: gcc ou clang (macOS: xcode-select --install) e curl em runtime.
set -euo pipefail
cd "$(dirname "$0")"

MAKE=make
command -v make >/dev/null 2>&1 || MAKE="gmake"
command -v "$MAKE" >/dev/null 2>&1 || { echo "erro: make nao encontrado"; exit 1; }

CC_USE="${CC:-}"
if [ -z "$CC_USE" ]; then
    for c in cc gcc clang; do command -v "$c" >/dev/null 2>&1 && { CC_USE="$c"; break; }; done
fi
[ -n "$CC_USE" ] || { echo "erro: nenhum compilador C encontrado (instale gcc ou clang)"; exit 1; }

OS="$(uname -s)"
echo "==> $OS · compilador $CC_USE"
command -v curl >/dev/null 2>&1 || echo "aviso: curl nao encontrado (necessario para falar com os provedores)"

TARGETS=""
for a in "$@"; do
    case "$a" in
        --dist) TARGETS="$TARGETS dist" ;;
        --test) TARGETS="$TARGETS test" ;;
        *) ;;
    esac
done
[ -n "$TARGETS" ] || TARGETS="all"

# shellcheck disable=SC2086
$MAKE CC="$CC_USE" $TARGETS

echo
if [ -x bin/delta ]; then
    echo "pronto: bin/delta"
    echo "  instalar:  ./bin/delta --install \"\$HOME/.deltaCore\"   (ou duplo clique no gerenciador de arquivos)"
    echo "  docs:      ./bin/delta wiki"
    echo "  rodar:     ./bin/delta"
fi
