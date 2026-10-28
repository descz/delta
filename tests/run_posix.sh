#!/usr/bin/env bash
# tests/run_posix.sh - verificacao do Delta no Linux/macOS (sem PowerShell).
# Uso: bash tests/run_posix.sh
set -u
cd "$(dirname "$0")/.." || exit 1
ROOT="$(pwd)"
# consulta de versao nova no boot: desligada na suite (a secao [12] religa
# apontando para um release falso)
export DELTA_NO_UPDATE_CHECK=1
AR="$ROOT/.posix-tests"
rm -rf "$AR"
mkdir -p "$AR"

FAIL=0
pass() { printf '  \033[32mok\033[0m   %s\n' "$1"; }
fail() { printf '  \033[31mFALHA\033[0m %s\n' "$1"; FAIL=$((FAIL + 1)); }
expect() {
    if grep -qF -- "$2" "$1" 2>/dev/null; then pass "$3"; else fail "$3 (esperava: $2)"; fi
}
reject() {
    if grep -qF -- "$2" "$1" 2>/dev/null; then fail "$3 (nao deveria conter: $2)"; else pass "$3"; fi
}

PY=""
for p in python3 python; do command -v "$p" >/dev/null 2>&1 && { PY="$p"; break; }; done
PORT=$(( (RANDOM % 2000) + 8300 ))
CFG="$HOME/.deltaCore/config.json"
AUTH="$HOME/.deltaCore/auth.json"
MEM="$HOME/.deltaCore/memory.json"
cp "$CFG" "$AR/cfg.bak" 2>/dev/null
cp "$AUTH" "$AR/auth.bak" 2>/dev/null
cp "$MEM" "$AR/mem.bak" 2>/dev/null

echo "============================================================"
echo " Delta Agent - verificacao (POSIX)"
echo "============================================================"

echo
echo "[1] build"
MAKE=make
command -v make >/dev/null 2>&1 || MAKE=gmake
command -v "$MAKE" >/dev/null 2>&1 || { echo "make nao encontrado"; exit 1; }
if "$MAKE" clean >/dev/null 2>&1 && "$MAKE" > "$AR/build.log" 2>&1; then
    pass "compilou bin/delta ($(stat -c%s bin/delta 2>/dev/null || stat -f%z bin/delta) bytes)"
else
    fail "build"; tail -20 "$AR/build.log"; exit 1
fi
BIN="./bin/delta"

echo
echo "[2] testes de nucleo"
if "$MAKE" test > "$AR/unit.log" 2>&1; then
    pass "nucleo (cores/json/largura/tema/markdown)"
else
    fail "testes de unidade"; tail -20 "$AR/unit.log"
fi

echo
echo "[3] comandos da TUI (replay)"
$BIN --replay tests/replay-commands.txt 110 36 > "$AR/cmds.log" 2>&1
expect "$AR/cmds.log" "Mostra os comandos e atalhos" "/help renderiza a tabela"
expect "$AR/cmds.log" "provedor :"                   "/status renderiza"
expect "$AR/cmds.log" "comando desconhecido"         "comando invalido avisa"

echo
echo "[3c] /terminal"
$BIN --replay tests/replay-terminal.txt 100 24 > "$AR/term.log" 2>&1
expect "$AR/term.log" "ola do delta" "comando roda e mostra a saida"
expect "$AR/term.log" "saiu com 0"   "codigo de saida"

echo
echo "[3c2] comandos removidos nesta versao"
printf 'type /copy
key enter
dump copy_removido
' > "$AR/rem.txt"
$BIN --replay "$AR/rem.txt" 80 24 > "$AR/rem.log" 2>&1
expect "$AR/rem.log" "comando desconhecido"  "/copy nao existe mais"
printf 'type /forlimit 3
key enter
dump fl_removido
' > "$AR/rem2.txt"
$BIN --replay "$AR/rem2.txt" 80 24 > "$AR/rem2.log" 2>&1
expect "$AR/rem2.log" "comando desconhecido"  "/forlimit nao existe mais"

echo
echo "[3c3] scroll durante o stream (topo ancorado)"
$BIN --replay tests/replay-scroll-ancora.txt 60 16 > "$AR/scroll.log" 2>&1
TOPO_ANTES=$(sed -n '/--- passo .*: antes ---/{n;p;}' "$AR/scroll.log")
TOPO_DEPOIS=$(sed -n '/--- passo .*: depois ---/{n;p;}' "$AR/scroll.log")
if [ -n "$TOPO_ANTES" ] && [ "$TOPO_ANTES" = "$TOPO_DEPOIS" ]; then
    pass "rolar para tras: o topo nao escorrega quando o agente escreve"
else
    fail "scroll escorregou (\"$TOPO_ANTES\" -> \"$TOPO_DEPOIS\")"
fi

echo
echo "[3d] /projects + ~/.deltaCore"
mkdir -p build/proj-teste
$BIN --replay tests/replay-projects.txt 100 24 > "$AR/proj.log" 2>&1
expect "$AR/proj.log" "build/proj-teste" "define a pasta do projeto"
for d in skills mcps extensions system sessions; do
    if [ -d "$HOME/.deltaCore/$d" ]; then pass "~/.deltaCore/$d existe"; else fail "falta $d"; fi
done
rm -rf build/proj-teste

echo
echo "[3e] raiz dos dados (DELTA_CORE / --core)"
rm -rf build/core-env build/core-flag
DELTA_CORE=build/core-env $BIN --tool mcp '{"op":"list"}' > /dev/null 2>&1
[ -d build/core-env/mcps ] && pass "DELTA_CORE respeitado" || fail "DELTA_CORE ignorado"
$BIN --core build/core-flag --tool mcp '{"op":"list"}' > /dev/null 2>&1
[ -d build/core-flag/skills ] && pass "--core respeitado" || fail "--core ignorado"
rm -rf build/core-env build/core-flag

echo
echo "[3f] instalador (--install sem atalhos)"
rm -rf build/inst-posix
OUT=$(DELTA_INSTALL_NOLINK=1 $BIN --install build/inst-posix 2>&1)
case "$OUT" in
    *'"ok":true'*) pass "instalacao sem interface" ;;
    *)             fail "--install falhou ($OUT)" ;;
esac
if [ -x build/inst-posix/bin/delta ]; then pass "binario instalado e executavel"; else fail "binario nao executavel"; fi
rm -rf build/inst-posix

echo
echo "[3i] delta wiki"
rm -rf build/wiki-posix
mkdir -p build/wiki-posix
cp bin/delta build/wiki-posix/Delta
(cd build/wiki-posix && exec env DELTA_SETUP_NOBROWSER=1 ./Delta wiki > "$AR/wiki-server.log" 2>&1) &
WIKI_PID=$!
WIKI_PORT=""
for _ in $(seq 1 40); do
    WIKI_PORT=$(grep -o "127.0.0.1:[0-9]*" "$AR/wiki-server.log" 2>/dev/null | head -1 | cut -d: -f2)
    [ -n "$WIKI_PORT" ] && break
    sleep 0.25
done
if [ -n "$WIKI_PORT" ]; then
    pass "servidor subiu"
    if curl -s "http://127.0.0.1:$WIKI_PORT/wiki" | grep -q "What Delta is"; then
        pass "documentacao servida em /wiki"
    else fail "documentacao nao veio"; fi
    curl -s -X POST "http://127.0.0.1:$WIKI_PORT/api/quit" > /dev/null 2>&1
else
    fail "servidor nao subiu"
fi
sleep 1; kill "$WIKI_PID" 2>/dev/null; wait "$WIKI_PID" 2>/dev/null
rm -rf build/wiki-posix

if [ -n "$PY" ]; then
    echo
    echo "[4] conversa real (servidor falso)"
    "$PY" tests/mock_openai.py "$PORT" > /dev/null 2>&1 &
    MOCK=$!
    sleep 1.5
    $BIN --replay tests/replay-agent.txt 110 36 --provider openai --model gpt-4o \
        --baseurl "http://127.0.0.1:$PORT/v1" --api-key sk-teste > "$AR/agent.log" 2>&1
    expect "$AR/agent.log" "a resposta e 42" "saida da ferramenta"
    expect "$AR/agent.log" "O arquivo contem: 42" "resposta do assistente"
    expect "$AR/agent.log" 'read path="tests/mock-target.txt"' "caixa da ferramenta"

    echo
    echo "[3h] retomada da sessao da pasta"
    rm -rf build/resume-posix
    mkdir -p build/resume-posix
    cp tests/mock-target.txt build/resume-posix/
    printf 'type leia mock-target.txt\nkey enter\ndump fim\n' > build/resume-posix/r1.txt
    printf 'size 100 40\ndump inicio\n' > build/resume-posix/r2.txt
    (cd build/resume-posix && "../../bin/delta" --replay r1.txt 100 40 \
        --provider openai --baseurl "http://127.0.0.1:$PORT/v1" --api-key sk-teste > /dev/null 2>&1)
    (cd build/resume-posix && "../../bin/delta" --replay r2.txt 100 40 --resume \
        --provider openai --baseurl "http://127.0.0.1:$PORT/v1" --api-key sk-teste > "$AR/resume.log" 2>&1)
    expect "$AR/resume.log" "sessao retomada" "retoma o ultimo chat"
    expect "$AR/resume.log" "leia mock-target.txt" "historico reconstruido"
    rm -rf build/resume-posix

    echo
    echo "[5] modo nao interativo (-p)"
    $BIN -p "leia tests/mock-target.txt" --provider openai --model gpt-4o \
        --baseurl "http://127.0.0.1:$PORT/v1" --api-key sk-teste > "$AR/print.log" 2>&1
    expect "$AR/print.log" "O arquivo contem" "resposta impressa"
    kill "$MOCK" 2>/dev/null
    wait "$MOCK" 2>/dev/null
else
    echo
    echo "  (sem python: pulando os testes com servidor falso)"
fi

echo
echo "[11] redimensionamento"
$BIN --replay tests/replay-resize.txt 80 24 > "$AR/size.log" 2>&1
expect "$AR/size.log" "chave de API" "transcript remontado na largura nova"

echo
echo "[12] delta update (release falso: .tar.gz)"
UPD="$AR/upd"
rm -rf "$UPD"
mkdir -p "$UPD/pkg/delta-linux-x86_64"
printf '#!/bin/sh\necho delta-novo-v99.9.9\n' > "$UPD/pkg/delta-linux-x86_64/delta"
chmod +x "$UPD/pkg/delta-linux-x86_64/delta"
# o pacote tem que passar do tamanho minimo: dados aleatorios (nao comprimem)
head -c 60000 /dev/urandom > "$UPD/pkg/delta-linux-x86_64/pad"
( cd "$UPD/pkg" && tar -czf "$UPD/pkg.tar.gz" delta-linux-x86_64 )
cp "$BIN" "$UPD/delta"
if [ -n "$PY" ]; then
    ( $PY tests/update_fake.py $((PORT + 7)) v99.9.9 delta-linux-x86_64.tar.gz "$UPD/pkg.tar.gz" > /dev/null 2>&1 &
      echo $! > "$UPD/upd.pid" )
    sleep 2
    DELTA_NO_UPDATE_CHECK=0 DELTA_UPDATE_URL="http://127.0.0.1:$((PORT + 7))/latest" DELTA_CORE="$UPD/core" \
        "$UPD/delta" update > "$UPD/upd.log" 2>&1
    expect "$UPD/upd.log" "atualizado para 99.9.9" "update: instala a versao nova"
    if [ -x "$UPD/delta" ] && "$UPD/delta" 2>/dev/null | grep -q "delta-novo-v99.9.9"; then
        pass "update: binario trocado e executavel"
    else fail "update: binario nao trocado"; fi
    # aviso na inicializacao (cache vazio)
    rm -rf "$UPD/core2"
    printf 'update-check\ndump aviso\n' > "$UPD/aviso.txt"
    DELTA_NO_UPDATE_CHECK=0 DELTA_UPDATE_URL="http://127.0.0.1:$((PORT + 7))/latest" DELTA_CORE="$UPD/core2" \
        "$BIN" --replay "$UPD/aviso.txt" 100 30 > "$UPD/aviso.log" 2>&1
    expect "$UPD/aviso.log" "99.9.9" "update: aviso de versao nova na inicializacao"
    kill "$(cat "$UPD/upd.pid")" 2>/dev/null
else
    echo "  (sem python: pulando o release falso)"
fi

# devolve config/auth/memoria do usuario
[ -f "$AR/cfg.bak" ] && cp "$AR/cfg.bak" "$CFG" 2>/dev/null
[ -f "$AR/auth.bak" ] && cp "$AR/auth.bak" "$AUTH" 2>/dev/null
[ -f "$AR/mem.bak" ] && cp "$AR/mem.bak" "$MEM" 2>/dev/null

echo
echo "============================================================"
if [ "$FAIL" -eq 0 ]; then
    printf ' tudo certo.\n'
else
    printf ' \033[31m%d falha(s)\033[0m\n' "$FAIL"
fi
echo "============================================================"
exit "$FAIL"
