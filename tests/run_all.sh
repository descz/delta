#!/usr/bin/env bash
# tests/run_all.sh - verificacao completa do Delta Agent
# Uso: bash tests/run_all.sh
set -u
cd "$(dirname "$0")/.." || exit 1
ROOT="$(pwd)"
# a TUI de verdade consulta releases no boot; nos testes isso fica desligado
# (a secao [19] liga de novo, apontando para um release falso)
export DELTA_NO_UPDATE_CHECK=1
CFG_GLOBAL="$HOME/.deltaCore/config.json"
cp "$CFG_GLOBAL" "$ROOT/build/cfg-global.bak" 2>/dev/null
AUTH_GLOBAL="$HOME/.deltaCore/auth.json"
cp "$AUTH_GLOBAL" "$ROOT/build/auth-global.bak" 2>/dev/null
MEM_GLOBAL="$HOME/.deltaCore/memory.json"
cp "$MEM_GLOBAL" "$ROOT/build/mem-global.bak" 2>/dev/null
# a pasta de projeto do usuario nao pode vazar para os testes (/projects salva
# projectDir na config); zera durante a suite e restaura no fim.
python - <<'PYEOF' 2>/dev/null
import json, io, os
p = os.path.expanduser("~/.deltaCore/config.json")
try:
    d = json.load(io.open(p, encoding="utf-8"))
except Exception:
    raise SystemExit(0)
d["projectDir"] = ""
io.open(p, "w", encoding="utf-8").write(json.dumps(d, indent=2))
PYEOF

PORT=$((RANDOM % 2000 + 8300))
FAIL=0
pass() { printf '  \033[32mok\033[0m   %s\n' "$1"; }
fail() { printf '  \033[31mFALHA\033[0m %s\n' "$1"; FAIL=$((FAIL+1)); }

expect() { # arquivo, substring, descricao
    if grep -qF -- "$2" "$1"; then pass "$3"; else fail "$3 (esperava: $2)"; fi
}
reject() { # arquivo, substring, descricao
    if grep -qF -- "$2" "$1"; then fail "$3 (nao deveria conter: $2)"; else pass "$3"; fi
}

echo "============================================================"
echo " Delta Agent - verificacao"
echo "============================================================"

command -v gcc >/dev/null 2>&1 || { echo "gcc nao encontrado"; exit 1; }

# se sobrou alguma instancia da TUI, o linker nao consegue regravar o exe
taskkill //F //IM delta.exe >/dev/null 2>&1
pkill -f mock_openai.py >/dev/null 2>&1
sleep 1

echo
echo "[1] build"
if cmd //c build.bat > /tmp/delta_build.log 2>&1; then
    pass "build.bat compilou bin/delta.exe ($(stat -c%s bin/delta.exe) bytes)"
else
    fail "build.bat"; cat /tmp/delta_build.log | tail -20; exit 1
fi

echo
echo "[2] testes de unidade"
if cmd //c build.bat test > /tmp/delta_test.log 2>&1; then
    pass "36 verificacoes de nucleo (cores/json/largura/tema/markdown)"
else
    fail "testes de unidade"; tail -20 /tmp/delta_test.log
fi

echo
echo "[3] comandos da TUI (replay, sem terminal)"
./bin/delta.exe --replay tests/replay-commands.txt 110 44 > /tmp/delta_cmds.log 2>&1
expect /tmp/delta_cmds.log "Mostra os comandos e atalhos" "/help renderiza a tabela de comandos"
expect /tmp/delta_cmds.log "salva a chave"         "/help nao corta o ultimo caractere"
expect /tmp/delta_cmds.log "provedor :"            "/status renderiza"
expect /tmp/delta_cmds.log "read - Read file"      "/tools lista as ferramentas"
expect /tmp/delta_cmds.log "comando desconhecido"  "comando invalido avisa no rodape"
expect /tmp/delta_cmds.log "Limpa a conversa"      "bloco multilinha quebra linha"
reject /tmp/delta_cmds.log "esta ajud$"            "nenhuma linha perde o ultimo caractere"

# copiar mensagem (selecao por clique) tem que sair limpa: sem a moldura das
# caixas de ferramenta e sem as quebras do wrap visual
./bin/delta.exe --replay tests/replay-copia.txt 56 24 > /tmp/delta_copia.log 2>&1
sed -n '/--- copiado ---/,/--- fim ---/p' /tmp/delta_copia.log > /tmp/delta_copia.txt
expect /tmp/delta_copia.txt "terminal echo alpha beta"  "copiar: texto do bloco vai inteiro"
reject /tmp/delta_copia.txt "│"                         "copiar: sem moldura de caixa"
reject /tmp/delta_copia.txt "^  terminal \$"             "copiar: sem o titulo decorado da caixa"
expect /tmp/delta_copia.log "  /help                              Mostra os comandos e atalhos" "copiar: linha longa nao chega quebrada"

echo
echo "[3b] fila enquanto o agente trabalha (replay)"
./bin/delta.exe --replay tests/replay-fila.txt 80 24 > /tmp/delta_fila.log 2>&1
expect /tmp/delta_fila.log "Working..."                  "status de trabalho visivel na borda do editor"
expect /tmp/delta_fila.log "2 na fila"                   "contador de mensagens na fila"
expect /tmp/delta_fila.log "Ш na fila"                   "mensagem do usuario aparece no chat, marcada na fila"
expect /tmp/delta_fila.log "leia src/util.c e resuma"    "mensagem foi enfileirada sem sumir"
DRAIN=$(sed -n '/depois_do_drain/,$p' /tmp/delta_fila.log)
if printf '%s' "$DRAIN" | grep -q "Ш"; then
    fail "depois de drenada, a mensagem deveria sair do painel"
else
    pass "no intervalo, a fila e entregue ao agente (sai do painel)"
fi
if printf '%s' "$DRAIN" | grep -q "tambem procure por TODOs"; then
    pass "mensagem aparece no transcript como mensagem do usuario"
else
    fail "mensagem nao virou bloco do usuario no transcript"
fi

echo
echo "[3d] /projects (pasta de trabalho) + ~/.deltaCore"
mkdir -p build/proj-teste
./bin/delta.exe --replay tests/replay-projects.txt 100 24 > /tmp/delta_projects.log 2>&1
expect /tmp/delta_projects.log "Uso: /projects <pasta>"       "sem argumento mostra o projeto atual e o uso"
expect /tmp/delta_projects.log "build/proj-teste"             "define a pasta do projeto"
expect /tmp/delta_projects.log "Projeto atual: "              "consulta mostra a pasta nova"
for d in skills mcps extensions system sessions paste jobs themes; do
    if [ -d "$HOME/.deltaCore/$d" ]; then pass "~/.deltaCore/$d existe"
    else fail "~/.deltaCore/$d nao foi criado"; fi
done
if grep -q '"projectDir"' "$HOME/.deltaCore/config.json" 2>/dev/null; then
    pass "pasta do projeto salva na config"
else
    fail "projectDir nao foi salvo"
fi
rm -rf build/proj-teste

echo
echo "[3e] raiz dos dados: DELTA_CORE / --core"
rm -rf build/core-env build/core-flag
DELTA_CORE=build/core-env ./bin/delta.exe --tool mcp '{"op":"list"}' > /dev/null 2>&1
if [ -d build/core-env/mcps ] && [ -d build/core-env/system ]; then
    pass "DELTA_CORE=... cria a estrutura na pasta escolhida"
else
    fail "DELTA_CORE nao foi respeitado"
fi
./bin/delta.exe --core build/core-flag --tool mcp '{"op":"list"}' > /dev/null 2>&1
if [ -d build/core-flag/skills ] && [ -f build/core-flag/memory.json ]; then
    pass "--core <pasta> usa essa pasta como raiz"
else
    fail "--core nao foi respeitado"
fi
rm -rf build/core-env build/core-flag

echo
echo "[3f] instalador: --install + servidor --setup"
rm -rf build/inst-cli build/inst-http
OUT=$(DELTA_INSTALL_NOLINK=1 ./bin/delta.exe --install build/inst-cli 2>&1)
case "$OUT" in
    *'"ok":true'*) pass "instalacao sem interface cria core + binario" ;;
    *)             fail "--install nao concluiu ($OUT)" ;;
esac
if [ -d build/inst-cli/skills ] && [ -x build/inst-cli/bin/delta.exe ]; then
    pass "arvore + binario dentro da pasta escolhida"
else
    fail "arvore/binario ausentes em build/inst-cli"
fi
case "$OUT" in
    *'--version roda'*) pass "verifica que o delta instalado executa" ;;
    *)                  fail "nao verificou o binario instalado" ;;
esac

# servidor do instalador (sem abrir navegador). Roda de uma copia ISOLADA do
# exe (sem deltalogo.png ao lado) para provar que o binario e autossuficiente.
mkdir -p build/iso
cp bin/delta.exe build/iso/Delta.exe
(cd build/iso && exec env DELTA_SETUP_NOBROWSER=1 DELTA_INSTALL_NOLINK=1 ./Delta.exe --setup > /tmp/delta_setup.log 2>&1) &
SETUP_PID=$!
SETUP_PORT=""
for _ in $(seq 1 40); do
    SETUP_PORT=$(grep -o "127.0.0.1:[0-9]*" /tmp/delta_setup.log 2>/dev/null | head -1 | cut -d: -f2)
    [ -n "$SETUP_PORT" ] && break
    sleep 0.25
done
if [ -n "$SETUP_PORT" ]; then
    pass "servidor do instalador subiu"
    if curl -s "http://127.0.0.1:$SETUP_PORT/" | grep -q "Delta Agent"; then
        pass "pagina HTML servida"
    else fail "pagina nao veio"; fi
    if curl -s "http://127.0.0.1:$SETUP_PORT/api/state" | grep -q '"os"'; then
        pass "/api/state responde"
    else fail "/api/state falhou"; fi
    if curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$SETUP_PORT/logo.png" | grep -q 200; then
        pass "logo embutida servida"
    else fail "logo nao servida"; fi
    LOGO_BYTES=$(curl -s "http://127.0.0.1:$SETUP_PORT/logo.png" | wc -c)
    if [ "$LOGO_BYTES" -gt 20000 ]; then
        pass "logo vem de dentro do exe ($LOGO_BYTES bytes)"
    else
        fail "logo embutida pequena/ausente ($LOGO_BYTES bytes)"
    fi
    if curl -s -X POST -d "{\"dir\":\"$ROOT/build/inst-http\"}" "http://127.0.0.1:$SETUP_PORT/api/install" | grep -q '"ok":true'; then
        pass "/api/install instala pela pagina"
    else fail "/api/install falhou"; fi
    curl -s -X POST "http://127.0.0.1:$SETUP_PORT/api/quit" >/dev/null 2>&1
else
    fail "servidor do instalador nao subiu"
fi
sleep 1
kill $SETUP_PID 2>/dev/null
wait $SETUP_PID 2>/dev/null
rm -rf build/inst-cli build/inst-http build/iso

echo
echo "[3h] retomada: delta abre no ultimo chat da pasta"
python tests/mock_openai.py "$((PORT + 11))" > /dev/null 2>&1 &
RMOCK=$!
sleep 2
rm -rf build/resume
mkdir -p build/resume
cp tests/mock-target.txt build/resume/
printf 'type leia mock-target.txt
key enter
dump fim
' > build/resume/r1.txt
printf 'size 100 40
dump inicio
' > build/resume/r2.txt
(cd build/resume && ../../bin/delta.exe --replay r1.txt 100 40     --provider openai --baseurl "http://127.0.0.1:$((PORT + 11))/v1" --api-key sk-teste     > /dev/null 2>&1)
(cd build/resume && ../../bin/delta.exe --replay r2.txt 100 40 --resume     --provider openai --baseurl "http://127.0.0.1:$((PORT + 11))/v1" --api-key sk-teste     > /tmp/delta_resume.log 2>&1)
expect /tmp/delta_resume.log "sessao retomada"      "retoma a sessao da pasta"
expect /tmp/delta_resume.log "leia mock-target.txt" "historico anterior reconstruido"
(cd build/resume && ../../bin/delta.exe --replay r2.txt 100 40 --new     --provider openai --baseurl "http://127.0.0.1:$((PORT + 11))/v1" --api-key sk-teste     > /tmp/delta_new.log 2>&1)
reject /tmp/delta_new.log "sessao retomada"         "--new comeca uma sessao nova"
kill $RMOCK 2>/dev/null
wait $RMOCK 2>/dev/null
rm -rf build/resume

echo
echo "[3i] delta wiki (documentacao local)"
rm -rf build/wiki
mkdir -p build/wiki
cp bin/delta.exe build/wiki/Delta.exe
(cd build/wiki && exec env DELTA_SETUP_NOBROWSER=1 ./Delta.exe wiki > /tmp/delta_wiki.log 2>&1) &
WIKI_PID=$!
WIKI_PORT=""
for _ in $(seq 1 40); do
    WIKI_PORT=$(grep -o "127.0.0.1:[0-9]*" /tmp/delta_wiki.log 2>/dev/null | head -1 | cut -d: -f2)
    [ -n "$WIKI_PORT" ] && break
    sleep 0.25
done
if [ -n "$WIKI_PORT" ]; then
    pass "delta wiki subiu o servidor"
    if curl -s "http://127.0.0.1:$WIKI_PORT/wiki" | grep -q "What Delta is"; then
        pass "serviu a documentacao em /wiki"
    else fail "documentacao nao veio"; fi
    if grep -q "/wiki" /tmp/delta_wiki.log; then
        pass "abre direto na pagina de docs"
    else fail "url nao aponta para /wiki"; fi
    curl -s -X POST "http://127.0.0.1:$WIKI_PORT/api/quit" >/dev/null 2>&1
else
    fail "delta wiki nao subiu"
fi
sleep 1
kill $WIKI_PID 2>/dev/null
wait $WIKI_PID 2>/dev/null
rm -rf build/wiki

echo
echo "[3g] primeira execucao (usuario novo, core vazio)"
rm -rf build/freshcore
python tests/mock_openai.py "$((PORT + 9))" > /dev/null 2>&1 &
FRESH_MOCK=$!
sleep 2
DELTA_CORE=build/freshcore ./bin/delta.exe --replay tests/replay-firstrun.txt 108 30     --baseurl "http://127.0.0.1:$((PORT + 9))/v1" > /tmp/delta_first.log 2>&1
expect /tmp/delta_first.log "sem chave de API"          "tela inicial avisa que falta a chave"
expect /tmp/delta_first.log "chave de API do OpenAI"    "/login pede a chave do provedor escolhido"
expect /tmp/delta_first.log "chave salva para openai"   "chave gravada num core vazio"
if [ -f build/freshcore/auth.json ] && grep -q "sk-teste" build/freshcore/auth.json; then
    pass "auth.json criado no core novo"
else
    fail "auth.json nao foi criado no core novo"
fi
DELTA_CORE=build/freshcore ./bin/delta.exe -p "leia tests/mock-target.txt"     --provider openai --baseurl "http://127.0.0.1:$((PORT + 9))/v1" > /tmp/delta_first2.log 2>&1
expect /tmp/delta_first2.log "O arquivo contem"         "agente responde usando a chave salva"
kill $FRESH_MOCK 2>/dev/null
wait $FRESH_MOCK 2>/dev/null
rm -rf build/freshcore

echo
echo "[3j] grafo do projeto, undo, /forlimit e MCP stdio"
./bin/delta.exe --tool graph '{"op":"find","name":"tool_graph"}' > /tmp/delta_g1.log 2>&1
expect /tmp/delta_g1.log "tool_graph" "graph: find acha o símbolo"
./bin/delta.exe --tool graph '{"op":"file","path":"src/codegraph.c"}' > /tmp/delta_g2.log 2>&1
expect /tmp/delta_g2.log "graph_open" "graph: símbolos do arquivo"
./bin/delta.exe --tool graph '{"op":"status"}' > /tmp/delta_g3.log 2>&1
expect /tmp/delta_g3.log "arquivos:" "graph: status"

printf 'antes
' > build/undo-test.txt
./bin/delta.exe --tool write '{"path":"build/undo-test.txt","content":"depois
"}' > /dev/null 2>&1
./bin/delta.exe --tool undo '{"op":"restore","path":"build/undo-test.txt"}' > /dev/null 2>&1
if grep -q "antes" build/undo-test.txt; then pass "undo: restaura o conteúdo anterior"; else fail "undo nao restaurou"; fi
rm -f build/undo-test.txt

# tela alta: o dump mostra o fim do transcript e o aviso do /forlimit precisa
# continuar visivel depois dos blocos do /status
printf 'size 100 30
type /forlimit 3
key enter
type /status
key enter
dump a
type /forlimit
key enter
type /status
key enter
dump b
' > /tmp/delta_fl.txt
./bin/delta.exe --replay /tmp/delta_fl.txt 100 30 > /tmp/delta_fl.log 2>&1
expect /tmp/delta_fl.log "teto de ferramentas por turno: 3" "/forlimit liga com teto"
expect /tmp/delta_fl.log "teto de ferramentas por turno: desligado" "/forlimit desliga no segundo uso"

if command -v python >/dev/null 2>&1; then
    rm -rf build/mcpstd
    DELTA_CORE=build/mcpstd ./bin/delta.exe --tool mcp '{"op":"add","name":"std","command":"python","args":"tests/mcp_std.py"}' > /dev/null 2>&1
    DELTA_CORE=build/mcpstd ./bin/delta.exe --tool mcp '{"op":"tools","name":"std"}' > /tmp/delta_m1.log 2>&1
    expect /tmp/delta_m1.log "echo" "mcp stdio: tools/list"
    DELTA_CORE=build/mcpstd ./bin/delta.exe --tool mcp '{"op":"call","name":"std","tool":"echo","args_json":"{\"x\":\"oi\"}"}' > /tmp/delta_m2.log 2>&1
    expect /tmp/delta_m2.log "eco: oi" "mcp stdio: tools/call"
    rm -rf build/mcpstd
else
    echo "  (sem python: pulando mcp stdio)"
fi

echo
echo "[3c] /terminal (roda no shell e mostra a saida)"
./bin/delta.exe --replay tests/replay-terminal.txt 100 24 > /tmp/delta_term.log 2>&1
expect /tmp/delta_term.log "terminal \$ echo ola do delta"  "bloco do terminal mostra o comando"
expect /tmp/delta_term.log "ola do delta"                   "saida do comando aparece"
expect /tmp/delta_term.log "linha1"                        "saida de varias linhas"
expect /tmp/delta_term.log "linha2"                        "segunda linha da saida"
expect /tmp/delta_term.log "saiu com 0"                    "codigo de saida mostrado"

echo
echo "[4] conversa real na TUI (servidor falso no formato OpenAI)"
python tests/mock_openai.py "$PORT" > /dev/null 2>&1 &
MOCK=$!
sleep 2
./bin/delta.exe --replay tests/replay-agent.txt 110 36 \
    --provider openai --model gpt-4o \
    --baseurl "http://127.0.0.1:$PORT/v1" --api-key sk-teste > /tmp/delta_tui.log 2>&1
expect /tmp/delta_tui.log "leia tests/mock-target.txt"  "mensagem do usuario renderiza"
expect /tmp/delta_tui.log 'read path="tests/mock-target.txt"' "caixa de ferramenta com o titulo e os argumentos"
expect /tmp/delta_tui.log "a resposta e 42"               "saida da ferramenta dentro da caixa"
expect /tmp/delta_tui.log "O arquivo contem: 42"          "resposta do assistente na tela"
expect /tmp/delta_tui.log "eco: leia tests/mock-target.txt e diga o numero" "a mensagem do usuario chegou ao modelo"
reject /tmp/delta_tui.log "**42**"                        "negrito renderizado (sem os asteriscos)"
reject /tmp/delta_tui.log "*caminho*"                     "italico renderizado (sem os asteriscos)"
reject /tmp/delta_tui.log "\`tests/mock-target.txt\`"     "inline code renderizado (sem as crases)"
expect /tmp/delta_tui.log "int resposta = 42;"            "bloco de codigo renderizado na tela"
expect /tmp/delta_tui.log "gpt-4o · openai · thinking off" "rodape da resposta: modelo, provedor e thinking"
expect /tmp/delta_tui.log "1.2k in / 56 out"              "rodape da resposta: tokens do turno"
if grep -qE '\|  read path=.*[0-9]+(\.[0-9])?(ms|s)\|' /tmp/delta_tui.log; then
    pass "tempo da tool dentro do bloco"
else
    fail "tempo da tool nao aparece dentro do bloco"
fi

echo
echo "[4b] prompt de sistema (tom serio, formato, chefe)"
for alvo in 'Delta Agent' 'chefe' 'emojis' 'Serious, professional tone' 'paragraphs of 2 to 3 lines' 'fenced blocks'; do
    expect build/mock-last-request.json "$alvo" "prompt contem: $alvo"
done

echo
echo "[5] modo nao interativo (-p)"
./bin/delta.exe -p "leia tests/mock-target.txt" \
    --provider openai --model gpt-4o \
    --baseurl "http://127.0.0.1:$PORT/v1" --api-key sk-teste > /tmp/delta_p.log 2>&1
expect /tmp/delta_p.log "O arquivo contem: **42**" "resposta impressa (markdown cru)"
expect /tmp/delta_p.log "int resposta = 42;"   "bloco de codigo cru (markdown)"
expect /tmp/delta_p.log "eco: leia tests/mock-target.txt" "modo -p envia o prompt"

echo
echo "[5b] erro do provedor (HTTP 400 estilo opencode-go)"
./bin/delta.exe -p "erro400 por favor" --provider openai --model gpt-4o     --baseurl "http://127.0.0.1:$PORT/v1" --api-key sk-teste > /tmp/delta_err.log 2>&1
expect /tmp/delta_err.log "HTTP 400"          "status HTTP reportado"
expect /tmp/delta_err.log "MissingSessionID"  "mensagem extraida do JSON"
reject /tmp/delta_err.log '"type":"error"'    "sem JSON cru na mensagem"

echo
echo "[5c] cabecalhos extras por provedor (config.json)"
CFG_BKP=$(cat "$HOME/.deltaCore/config.json" 2>/dev/null)
cat > "$HOME/.deltaCore/config.json" <<'CFGEOF'
{
  "provider": "openai",
  "model": "gpt-4o",
  "baseUrl": "http://127.0.0.1:PORTA/v1",
  "thinkingLevel": "off",
  "maxTokens": 8192,
  "headers": {"x-opencode-session": "sessao-de-teste"}
}
CFGEOF
sed -i "s/PORTA/$PORT/" "$HOME/.deltaCore/config.json"
./bin/delta.exe -p "leia o arquivo" --api-key sk-teste > /tmp/delta_hdr.log 2>&1
if grep -q "sessao-de-teste" build/mock-headers.json 2>/dev/null; then
    pass "header extra enviado ao provedor"
else
    fail "header extra nao foi enviado"
fi
[ -n "$CFG_BKP" ] && printf '%s' "$CFG_BKP" > "$HOME/.deltaCore/config.json"

echo
echo "[5d] provedores opencode (cabecalho de sessao automatico)"
./bin/delta.exe -p "leia o arquivo" --provider opencode-go --model deepseek-v4-flash     --baseurl "http://127.0.0.1:$PORT/v1" --api-key sk-teste > /dev/null 2>&1
if grep -q "x-opencode-session" build/mock-headers.json 2>/dev/null; then
    pass "x-opencode-session enviado automaticamente"
    if grep -q "x-opencode-client" build/mock-headers.json; then
        pass "x-opencode-client: pi enviado"
    else
        fail "x-opencode-client ausente"
    fi
else
    fail "x-opencode-session nao foi enviado"
fi

echo
echo "[5e] saida de programa do Windows com acentos (pagina OEM)"
mkdir -p build/acentos
: > "build/acentos/nao tem nomecao nao.txt"
./bin/delta.exe -p "liste os acentos" --provider openai --model gpt-4o     --baseurl "http://127.0.0.1:$PORT/v1" --api-key sk-teste > /tmp/delta_acentos.log 2>&1
if python -c "
import json,sys
d=json.load(open('build/mock-last-request.json',encoding='utf-8'))
txt=''.join(m.get('content','') for m in d['messages'] if m.get('role')=='tool')
sys.exit(0 if 'tem nome' in txt else 1)
" 2>/dev/null; then
    pass "JSON valido com a saida do bash (sem codepoint invalido)"
else
    fail "saida do bash quebrou o JSON"
fi
if grep -q "não tem nome" build/mock-last-request.json 2>/dev/null || grep -q "nao tem nomecao" build/mock-last-request.json 2>/dev/null; then
    pass "acentos da saida do bash em UTF-8"
else
    fail "acentos da saida do bash sairam errados"
fi

echo
echo "[5f] retry em 429 (gateway passageiro)"
./bin/delta.exe -p "retry429" --provider openai --model gpt-4o     --baseurl "http://127.0.0.1:$PORT/v1" --api-key sk-teste > "/tmp/delta_retry.log" 2>&1
expect "/tmp/delta_retry.log" "(eco: retry429)"   "429 passageiro: turno recuperou"
./bin/delta.exe -p "erro429" --provider openai --model gpt-4o     --baseurl "http://127.0.0.1:$PORT/v1" --api-key sk-teste > "/tmp/delta_retry2.log" 2>&1
expect "/tmp/delta_retry2.log" "HTTP 429"          "429 persistente: status reportado"
expect "/tmp/delta_retry2.log" "[dica]"            "429 persistente: dica ao usuario"

echo
echo "[6] sessao gravada (JSONL v3)"
mapfile -t SESSIONS < <(find "$HOME/.deltaCore/sessions" -name "*.jsonl" -newermt "-5 minutes" 2>/dev/null)
if [ "${#SESSIONS[@]}" -gt 0 ]; then
    cat "${SESSIONS[@]}" > /tmp/delta_sessions.log
    expect /tmp/delta_sessions.log '"version":3'   "cabecalho v3"
    expect /tmp/delta_sessions.log '"toolResult"'  "resultado de ferramenta registrado"
    expect /tmp/delta_sessions.log '"toolCalls"'   "tool call registrado"
    expect /tmp/delta_sessions.log '"cwd"'         "diretorio de trabalho registrado"
else
    fail "nenhum arquivo de sessao encontrado"
fi

echo
echo "[7] menu de comandos (digitar /)"
./bin/delta.exe --replay tests/replay-menu.txt 100 30 > /tmp/delta_menu.log 2>&1
expect /tmp/delta_menu.log "→ help"   "marcador -> no item selecionado"
expect /tmp/delta_menu.log "(1/23)"            "contador de itens"
expect /tmp/delta_menu.log "/models "         "Tab completa o comando"
expect /tmp/delta_menu.log "Todos os modelos"    "descricao do comando aparece"

echo
echo "[8] catalogo models.dev"
if [ -f "$HOME/.deltaCore/models.v2.json" ]; then
    expect "$HOME/.deltaCore/models.v2.json" '"providers"'  "cache do catalogo gravado"
    expect "$HOME/.deltaCore/models.v2.json" '"ctx"'        "contexto por modelo"

    echo
    echo "[9] /login (provedor + chave)      "
    AUTH_ANTES=$(cat "$HOME/.deltaCore/auth.json" 2>/dev/null)
    ./bin/delta.exe --replay tests/replay-login.txt 108 30 > /tmp/delta_login.log 2>&1
    expect /tmp/delta_login.log "Select provider to configure:"  "lista de provedores"
    expect /tmp/delta_login.log "[API key]"                      "metodo de autenticacao"
    expect /tmp/delta_login.log "env: "                          "mostra a env configurada"
    expect /tmp/delta_login.log "not configured"                 "mostra o que falta configurar"
    expect /tmp/delta_login.log "chave de API do"                "pede a chave do provedor escolhido"
    expect /tmp/delta_login.log "sk-chave-de-teste-123"          "a chave e exibida ao digitar"
    expect /tmp/delta_login.log "provedores conectados"       "seletor lista os provedores conectados"
    expect /tmp/delta_login.log "(nada encontrado - esc cancela)" "filtro vazio avisa"
    ESC_FINAL=$(sed -n '/apos_esc/,$p' /tmp/delta_login.log)
    if printf '%s' "$ESC_FINAL" | grep -q "no total\|modelos ·\|Select provider"; then
        fail "Esc nao fechou o seletor"
    else
        pass "Esc fecha o seletor"
    fi
    if grep -q "sk-chave-de-teste-123" "$HOME/.deltaCore/auth.json" 2>/dev/null; then
        pass "chave gravada em ~/.deltaCore/auth.json (por provedor)"
    else
        fail "chave nao foi gravada no auth.json"
    fi
    if grep -q "apiKey" "$HOME/.deltaCore/config.json" 2>/dev/null; then
        fail "config.json nao deveria guardar a chave"
    else
        pass "config.json sem chave (ela vive no auth.json)"
    fi
    # restaura o auth.json do usuario
    if [ -n "$AUTH_ANTES" ]; then printf '%s' "$AUTH_ANTES" > "$HOME/.deltaCore/auth.json"; fi
else
    fail "cache do catalogo ausente (rode bin/delta.exe uma vez online para baixa-lo)"
fi

echo
echo "[10] /thinking + lista de modelos  "
CFG_ANTES=$(cat "$HOME/.deltaCore/config.json" 2>/dev/null)
AUTH_ANTES2=$(cat "$HOME/.deltaCore/auth.json" 2>/dev/null)
./bin/delta.exe --replay tests/replay-thinking.txt 108 30 > /tmp/delta_think.log 2>&1
expect /tmp/delta_think.log "nao anuncia raciocinio no models.dev" "avisa quando o modelo nao anuncia reasoning"
expect /tmp/delta_think.log "o modelo aceita: low, high, max"      "so os niveis que o modelo aceita"
expect /tmp/delta_think.log "Desliga o raciocinio"                 "niveis listados no seletor"
expect /tmp/delta_think.log "Pensamento curto"                     "descricao de cada nivel"
expect /tmp/delta_think.log "(3/4)"                                "navegacao move o destaque"
expect /tmp/delta_think.log "deepseek/deepseek-flash"              "lista como provedor/modelo"
expect /tmp/delta_think.log "thinking "                            "rodape mostra provedor e thinking"
expect /tmp/delta_think.log "0.75/M"                               "preco total (entrada+saida+cache)"
expect /tmp/delta_think.log "provedores conectados"             "lista so provedores conectados"
if grep -qE '"thinkingLevel": "(off|minimal|low|medium|high|xhigh|max)"' "$HOME/.deltaCore/config.json" 2>/dev/null; then
    pass "nivel de thinking salvo na config"
else
    fail "nivel de thinking nao foi salvo"
fi
[ -n "$CFG_ANTES" ] && printf '%s' "$CFG_ANTES" > "$HOME/.deltaCore/config.json"
[ -n "$AUTH_ANTES2" ] && printf '%s' "$AUTH_ANTES2" > "$HOME/.deltaCore/auth.json"

echo
echo "[11] redimensionamento e tema mono"
./bin/delta.exe --replay tests/replay-resize.txt 80 24 > /tmp/delta_size.log 2>&1
expect /tmp/delta_size.log "API"                     "80 colunas: texto quebrado por palavra"
expect /tmp/delta_size.log "chave de API"            "130 colunas: transcript remontado na largura nova"
expect /tmp/delta_size.log "editor_com_bordas"       "dump do editor"
if grep -qE "^\|│.*│$" /tmp/delta_size.log 2>/dev/null || grep -q "│" /tmp/delta_size.log; then
    pass "editor com bordas laterais preenchidas"
else
    fail "editor sem bordas laterais"
fi
expect /tmp/delta_size.log "tema: dark"              "troca de tema pelo /theme"


echo
echo "[12] rolagem da conversa e contorno do editor"
./bin/delta.exe --replay tests/replay-scroll.txt 76 16 > /tmp/delta_scroll.log 2>&1
expect /tmp/delta_scroll.log "↑ 4 linhas acima"  "setas rolam a conversa"
expect /tmp/delta_scroll.log "ESC volta ao fim"        "dica de volta ao fim"
if grep -q "texto de teste" /tmp/delta_scroll.log; then
    pass "texto digitado aparece no editor"
else
    fail "editor nao mostrou o texto"
fi


echo
echo "[13] /logout (desconectar provedores conectados)"
AUTH_BKP=$(cat "$HOME/.deltaCore/auth.json" 2>/dev/null)
printf '{
  "groq": "gsk-chave-de-teste"
}
' > "$HOME/.deltaCore/auth.json"
./bin/delta.exe --replay tests/replay-logout.txt 92 22 > /tmp/delta_logout.log 2>&1
expect /tmp/delta_logout.log "Select provider to disconnect:" "lista de desconexao"
expect /tmp/delta_logout.log "env: DEEPSEEK_API_KEY"          "mostra como cada um esta conectado"
expect /tmp/delta_logout.log "stored"                         "mostra a chave salva"
expect /tmp/delta_logout.log "(2/2)"                          "roda do mouse move a selecao"
expect /tmp/delta_logout.log "nao ha o que remover"           "chave de ambiente nao some"
if grep -q "groq" "$HOME/.deltaCore/auth.json" 2>/dev/null; then
    fail "groq deveria ter sido desconectado"
else
    pass "provedor desconectado do auth.json"
fi
[ -n "$AUTH_BKP" ] && printf '%s' "$AUTH_BKP" > "$HOME/.deltaCore/auth.json"


echo
echo "[14] quebra de linha no editor e colagem"
./bin/delta.exe --replay tests/replay-wrap.txt 72 16 > /tmp/delta_wrap.log 2>&1
expect /tmp/delta_wrap.log "continuar na linha de baixo em vez"  "texto longo continua embaixo"
expect /tmp/delta_wrap.log "de ser cortada na borda direita"     "segunda linha do texto"
if grep -q "Delta──" /tmp/delta_wrap.log; then
    fail "o rodape invadiu a caixa do editor"
else
    pass "caixa do editor fecha sem invadir o rodape"
fi
./bin/delta.exe --replay tests/replay-paste.txt 72 14 > /tmp/delta_paste.log 2>&1
expect /tmp/delta_paste.log "{paste 250 caracteres}"  "colagem longa vira placeholder"
expect /tmp/delta_paste.log "{paste 18 caracteres}"   "toda colagem de texto vira placeholder"
./bin/delta.exe --replay tests/replay-paste-envio.txt 100 24     --provider openai --model gpt-4o     --baseurl "http://127.0.0.1:$PORT/v1" --api-key sk-teste > /tmp/delta_paste2.log 2>&1
expect /tmp/delta_paste2.log "MARCADOR-FINAL-DA-COLAGEM"  "o texto colado chega inteiro ao modelo"
reject /tmp/delta_paste2.log "(eco: {paste"                "o placeholder nao vai para o modelo"


echo
echo "[15] Ctrl+V (clipboard real do Windows)"
if [ "${DELTA_TEST_CLIPBOARD:-0}" != "1" ]; then
    echo "  (pulado: mexe no seu clipboard - rode com DELTA_TEST_CLIPBOARD=1 para ativar)"
elif command -v powershell >/dev/null 2>&1; then
    # texto
    powershell -STA -NoProfile -Command "Set-Clipboard -Value 'texto colado do clipboard'" >/dev/null 2>&1
    printf 'key ctrl-v
dump c
input
' > /tmp/delta-clip.txt
    bin/delta.exe --replay /tmp/delta-clip.txt 76 12 > /tmp/delta_clip.log 2>&1
    expect /tmp/delta_clip.log "{paste 25 caracteres}"     "Ctrl+V de texto vira placeholder"
    N=$(sed 's/\x1b\[[0-9;]*m//g' /tmp/delta_clip.log | grep 'entrada = ' | grep -o '{paste 25 caracteres}' | wc -l)
    if [ "$N" -eq 1 ]; then pass "Ctrl+V de texto cola uma vez"
    else fail "Ctrl+V de texto colou $N vezes"; fi
    # imagem (CF_DIB de verdade)
    rm -f "$HOME/.deltaCore/paste/"*.png 2>/dev/null
    rm -f /tmp/dib.log
    (powershell -STA -NoProfile -ExecutionPolicy Bypass -File tests/manual/set-dib.ps1 16 16 >/tmp/dib.log 2>&1 &)
    # espera o DIB estar no clipboard de verdade (o powershell pode demorar a subir)
    for _ in $(seq 1 24); do
        grep -q "CF_DIB no clipboard" /tmp/dib.log 2>/dev/null && break
        sleep 0.5
    done
    bin/delta.exe --replay /tmp/delta-clip.txt 76 12 > /tmp/delta_clip2.log 2>&1
    expect /tmp/delta_clip2.log "colado-"          "Ctrl+V cola imagem (CF_DIB): so o caminho"
    N=$(sed 's/\x1b\[[0-9;]*m//g' /tmp/delta_clip2.log | grep 'entrada = ' | grep -o 'colado-' | wc -l)
    if [ "$N" -eq 1 ]; then pass "Ctrl+V de imagem cola uma vez"
    else fail "Ctrl+V de imagem colou $N vezes"; fi
    if ls "$HOME/.deltaCore/paste/"*.png >/dev/null 2>&1; then
        pass "imagem convertida para PNG"
    else
        fail "nenhum PNG gerado"
    fi
else
    echo "  (sem powershell: pulando)"
fi

echo
echo "[15b] Ctrl+V no campo da chave do /login"
if [ "${DELTA_TEST_CLIPBOARD:-0}" != "1" ]; then
    echo "  (pulado: mexe no seu clipboard - rode com DELTA_TEST_CLIPBOARD=1 para ativar)"
elif command -v powershell >/dev/null 2>&1; then
    powershell -STA -NoProfile -Command "Set-Clipboard -Value 'sk-colada-do-clipboard-123'" >/dev/null 2>&1
    AUTH_ANTES3=$(cat "$HOME/.deltaCore/auth.json" 2>/dev/null)
    ./bin/delta.exe --replay tests/replay-login-paste.txt 108 30 > /tmp/delta_keypaste.log 2>&1
    expect /tmp/delta_keypaste.log "sk-colado-em-rajada-77" "rajada de texto cola no campo da chave"
    expect /tmp/delta_keypaste.log "sk-colada-do-clipboard-123" "Ctrl+V cola a chave no campo (nao no editor)"
    if [ -n "$AUTH_ANTES3" ]; then printf '%s' "$AUTH_ANTES3" > "$HOME/.deltaCore/auth.json"; fi
else
    echo "  (sem powershell: pulando)"
fi

echo
echo "[16] Ctrl+V no CONSOLE de verdade (o conhost cola o texto sozinho)"
if [ "${DELTA_TEST_CLIPBOARD:-0}" != "1" ]; then
    echo "  (pulado: abre janela e mexe no clipboard - DELTA_TEST_CLIPBOARD=1 ativa)"
elif command -v powershell >/dev/null 2>&1; then
    powershell -STA -NoProfile -ExecutionPolicy Bypass -File tests/manual/console-paste.ps1         -Root "$ROOT" -Port $((PORT + 1)) > /tmp/delta_console.log 2>&1
    if grep -q "^PASS" /tmp/delta_console.log; then
        pass "texto colado vira {paste N}, expande no envio e envia UMA vez"
    else
        fail "colagem no console real"; cat /tmp/delta_console.log | tail -4
    fi
    if grep -q "PNG criado" /tmp/delta_console.log; then
        pass "imagem colada no console gera PNG"
    else
        fail "imagem colada no console nao gerou PNG"
    fi
else
    echo "  (sem powershell: pulando)"
fi


echo
echo "[17] steer headless (sem janela): mensagem no meio do stream corta a resposta"
# Roda o mock com stream lento e injeta a mensagem pelo gancho do replay
# (steer-at), entao o corte acontece no meio do texto do agente. Nada de
# SendKeys/janela: o teste nao mexe no que voce esta fazendo na tela.
python tests/mock_openai.py "$((PORT + 2))" 0 0.05 > /dev/null 2>&1 &
MOCK_STEER=$!
sleep 2
./bin/delta.exe --replay tests/replay-steer.txt 110 36     --provider openai --model gpt-4o     --baseurl "http://127.0.0.1:$((PORT + 2))/v1" --api-key sk-teste > /tmp/delta_steer.log 2>&1
kill $MOCK_STEER 2>/dev/null
expect /tmp/delta_steer.log "mude o foco para o resumo"        "mensagem do usuario entrou no meio do turno"
expect /tmp/delta_steer.log "eco: mude o foco para o resumo"    "o modelo respondeu a mensagem nova"
reject /tmp/delta_steer.log "eco: leia tests/mock-target.txt"   "a resposta anterior foi cortada antes do fim"
expect /tmp/delta_steer.log "O arquivo contem: 42 e o caminho lido foi" "o texto parcial da resposta cortada ficou no historico"

echo
echo "[18] arsenal de tools (fs/git/shell/web/memory/code/assert/agent/extend)"
CFG0="$HOME/.deltaCore/config.json"
cp "$CFG0" "$ROOT/build/cfg0.bak" 2>/dev/null
AR="$ROOT/build/arsenal"
[ -f "$AR/http.pid" ] && kill "$(cat "$AR/http.pid")" 2>/dev/null

# --- plano no topo da conversa (com backup do plano do usuario)
PLAN2="$HOME/.deltaCore/plan.md"
[ -f "$PLAN2" ] && cp "$PLAN2" "$AR/plan2.bak"
./bin/delta.exe --tool agent '{"op":"plan","plan_markdown":"- [x] fs
- [ ] web
"}' > /dev/null 2>&1
./bin/delta.exe --replay tests/replay-commands.txt 84 22 > "$AR/planui.log" 2>&1
expect "$AR/planui.log" "Plano  1/2" "plano desenhado no topo"
expect "$AR/planui.log" "- [ ] web" "plano lista os itens"
if [ -f "$AR/plan2.bak" ]; then cp "$AR/plan2.bak" "$PLAN2"; else rm -f "$PLAN2"; fi
sleep 1
rm -rf "$AR" 2>/dev/null; mkdir -p "$AR"

# --- registro
N=$(./bin/delta.exe --tools | wc -l | tr -d ' ')
if [ "$N" -ge 16 ]; then pass "$N ferramentas registradas"; else fail "so $N ferramentas"; fi
for T in fs git shell web memory code assert agent extend; do
    if ./bin/delta.exe --tools | grep -q "^$T "; then pass "tool $T registrada"; else fail "tool $T ausente"; fi
done

# --- fs
./bin/delta.exe --tool fs "{\"op\":\"write\",\"relative_path\":\"build/arsenal/a.txt\",\"content\":\"alfa\nbeta\n\"}" > "$AR/fs.log" 2>&1
./bin/delta.exe --tool fs "{\"op\":\"append\",\"relative_path\":\"build/arsenal/a.txt\",\"content\":\"gama\n\"}" >> "$AR/fs.log" 2>&1
./bin/delta.exe --tool fs "{\"op\":\"replace\",\"relative_path\":\"build/arsenal/a.txt\",\"old_text\":\"beta\",\"new_text\":\"BETA\"}" >> "$AR/fs.log" 2>&1
./bin/delta.exe --tool fs "{\"op\":\"read\",\"relative_path\":\"build/arsenal/a.txt\"}" > "$AR/fs-read.log" 2>&1
expect "$AR/fs-read.log" "alfa" "fs: read"
expect "$AR/fs-read.log" "gama" "fs: append"
expect "$AR/fs-read.log" "BETA" "fs: replace"
./bin/delta.exe --tool fs "{\"op\":\"mkdir\",\"relative_path\":\"build/arsenal/sub\"}" > /dev/null 2>&1
./bin/delta.exe --tool fs "{\"op\":\"write\",\"relative_path\":\"build/arsenal/sub/b.c\",\"content\":\"int x;\n\"}" > /dev/null 2>&1
./bin/delta.exe --tool fs "{\"op\":\"glob\",\"relative_path\":\"build/arsenal\",\"pattern\":\"*.c\"}" > "$AR/fs-glob.log" 2>&1
expect "$AR/fs-glob.log" "b.c" "fs: glob"
./bin/delta.exe --tool fs "{\"op\":\"search\",\"relative_path\":\"build/arsenal\",\"query\":\"int x\"}" > "$AR/fs-search.log" 2>&1
expect "$AR/fs-search.log" "b.c" "fs: search"
./bin/delta.exe --tool fs "{\"op\":\"list\",\"relative_path\":\"build/arsenal\"}" > "$AR/fs-list.log" 2>&1
expect "$AR/fs-list.log" "a.txt" "fs: list"
if ./bin/delta.exe --tool fs "{\"op\":\"read\",\"relative_path\":\"../../etc/passwd\"}" > "$AR/fs-sandbox.log" 2>&1; then
    fail "fs: sandbox deixou sair do workspace"
else
    expect "$AR/fs-sandbox.log" "fora do workspace" "fs: sandbox"
fi
./bin/delta.exe --tool fs "{\"op\":\"delete\",\"relative_path\":\"build/arsenal/sub/b.c\"}" > /dev/null 2>&1
if [ -f "$AR/sub/b.c" ]; then fail "fs: delete nao removeu"; else pass "fs: delete"; fi

# --- assert
if ./bin/delta.exe --tool assert '{"op":"equals","condition_description":"2+2","value":"4","expected":"4"}' | grep -q PASS; then
    pass "assert: equals"
else fail "assert: equals"; fi
if ./bin/delta.exe --tool assert '{"op":"contains","condition_description":"x","value":"abc","substring":"zz"}' > /dev/null 2>&1; then
    fail "assert: contains deveria falhar"
else pass "assert: contains falha como esperado"; fi

# --- code
./bin/delta.exe --tool code '{"op":"structure","max_depth":1,"max_entries":40}' > "$AR/code.log" 2>&1
expect "$AR/code.log" "src/" "code: structure"
./bin/delta.exe --tool code '{"op":"symbol","name":"tools_ext_all","max_results":5}' > "$AR/symbol.log" 2>&1
expect "$AR/symbol.log" "tools_ext.c" "code: symbol"

# --- shell (run + jobs num so processo)
./bin/delta.exe --tool shell '{"op":"run","command":"echo arsenal-ok"}' > "$AR/shell.log" 2>&1
expect "$AR/shell.log" "arsenal-ok" "shell: run"
printf '%s\n' \
  'shell {"op":"start","command":"ping -n 4 127.0.0.1"}' \
  'shell {"op":"list"}' \
  'shell {"op":"tail","job_id":1,"max_lines":3}' \
  'shell {"op":"cancel","job_id":1}' \
  'shell {"op":"list"}' > "$AR/jobs.txt"
./bin/delta.exe --tool-script "$AR/jobs.txt" > "$AR/jobs.log" 2>&1
expect "$AR/jobs.log" "Job 1 started" "shell: start"
expect "$AR/jobs.log" "running"       "shell: list"
expect "$AR/jobs.log" "Job 1 cancelled" "shell: cancel"
expect "$AR/jobs.log" "cancelled"     "shell: status cancelado"

# --- memory (com backup do arquivo do usuario)
MEM="$HOME/.deltaCore/memory.json"
[ -f "$MEM" ] && cp "$MEM" "$AR/memory.bak"
./bin/delta.exe --tool memory '{"op":"remember","title":"arsenal-test","content":"memoria do teste","kind":"note","scope":"global"}' > "$AR/mem.log" 2>&1
expect "$AR/mem.log" "Memory stored" "memory: remember"
./bin/delta.exe --tool memory '{"op":"search","query":"arsenal-test"}' > "$AR/mem2.log" 2>&1
expect "$AR/mem2.log" "memoria do teste" "memory: search"
./bin/delta.exe --tool memory '{"op":"list","limit":2000}' > "$AR/mem3.log" 2>&1
expect "$AR/mem3.log" "arsenal-test" "memory: list"
./bin/delta.exe --tool memory '{"op":"insert","table":"nope","data_json":"{}"}' > /dev/null 2>&1
if [ $? -ne 0 ]; then pass "memory: insert recusa tabela invalida"; else fail "memory: insert aceitou tabela invalida"; fi
ID1=$(./bin/delta.exe --tool memory '{"op":"search","query":"arsenal-test"}' | head -1 | cut -d'[' -f2 | cut -d']' -f1)
./bin/delta.exe --tool memory "{\"op\":\"link\",\"from\":\"$ID1\",\"to\":\"outra-$$\"}" > "$AR/link.log" 2>&1
expect "$AR/link.log" "Linked" "memory: link"
./bin/delta.exe --tool memory '{"op":"list","limit":2000}' > "$AR/link2.log" 2>&1
expect "$AR/link2.log" "link:" "memory: link na lista"
./bin/delta.exe --tool memory "{\"op\":\"unlink\",\"from\":\"$ID1\",\"to\":\"outra-$$\"}" > "$AR/link3.log" 2>&1
expect "$AR/link3.log" "Unlinked" "memory: unlink"
# auto-memoria: a mensagem do usuario vira diario + preferencia
printf 'type sempre use gcc e nunca use clang neste projeto
key enter
dump depois
' > "$AR/am.txt"
./bin/delta.exe --replay "$AR/am.txt" 90 12 > /dev/null 2>&1
./bin/delta.exe --tool memory '{"op":"search","query":"clang"}' > "$AR/am.log" 2>&1
expect "$AR/am.log" "clang" "memoria automatica: diario"
if grep -q '"kind":"preference"' "$MEM" 2>/dev/null; then pass "memoria automatica: preferencia"; else fail "memoria automatica: preferencia ausente"; fi
if [ -f "$AR/memory.bak" ]; then cp "$AR/memory.bak" "$MEM"; else rm -f "$MEM"; fi

# --- agent (plan + skill index)
PLAN="$HOME/.deltaCore/plan.md"
[ -f "$PLAN" ] && cp "$PLAN" "$AR/plan.bak"
./bin/delta.exe --tool agent '{"op":"plan","plan_markdown":"- [x] um\n- [ ] dois\n"}' > "$AR/plan.log" 2>&1
expect "$AR/plan.log" "1 pendentes, 1 concluidos" "agent: plan"
if ./bin/delta.exe --tool agent '{"op":"skill"}' > "$AR/skill.log" 2>&1; then
    pass "agent: skill index"
else
    fail "agent: skill index"
fi
if [ -f "$AR/plan.bak" ]; then cp "$AR/plan.bak" "$PLAN"; else rm -f "$PLAN"; fi

# --- code: tests (Makefile temporario)
if command -v make >/dev/null 2>&1 || command -v mingw32-make >/dev/null 2>&1; then
    mkdir -p "$AR/pjt"
    printf 'test:
	@echo TESTS-OK
' > "$AR/pjt/Makefile"
    ./bin/delta.exe --cwd build/arsenal/pjt --tool code '{"op":"tests","timeout_seconds":60}' > "$AR/tests.log" 2>&1
    expect "$AR/tests.log" "TESTS-OK" "code: tests"
else
    echo "  (sem make: pulando code tests)"
fi

# --- agent: subagent contra o mock (config temporaria, com backup)
cp "$HOME/.deltaCore/config.json" "$AR/cfg.bak" 2>/dev/null
cp "$HOME/.deltaCore/auth.json" "$AR/auth.bak" 2>/dev/null
printf '{"provider":"openai","model":"gpt-4o","baseUrl":"http://127.0.0.1:%s/v1"}' "$PORT" > "$HOME/.deltaCore/config.json"
printf '{"openai":"sk-teste"}' > "$HOME/.deltaCore/auth.json"
./bin/delta.exe --tool agent '{"op":"subagent","task":"subagente-teste"}' > "$AR/sub.log" 2>&1
expect "$AR/sub.log" "subagente ok" "agent: subagent"
./bin/delta.exe --tool agent '{"op":"subagent","task":"leia tests/mock-target.txt e diga o numero"}' > "$AR/sub2.log" 2>&1
expect "$AR/sub2.log" "eco: leia tests/mock-target.txt" "agent: subagent usa tools"
if [ -f "$AR/cfg.bak" ]; then cp "$AR/cfg.bak" "$HOME/.deltaCore/config.json"; else rm -f "$HOME/.deltaCore/config.json"; fi
if [ -f "$AR/auth.bak" ]; then cp "$AR/auth.bak" "$HOME/.deltaCore/auth.json"; else rm -f "$HOME/.deltaCore/auth.json"; fi

# --- extend (plugin scaffold)
./bin/delta.exe --cwd build/arsenal --tool extend '{"op":"plugin","request":"deploy check","name_hint":"deploy-check"}' > "$AR/ext.log" 2>&1
expect "$AR/ext.log" "Plugin scaffold" "extend: plugin"
if [ -f "$AR/plugins/deploy-check.json" ]; then pass "extend: manifest gravado"; else fail "extend: manifest ausente"; fi
if ./bin/delta.exe --tool extend '{"op":"mcp","server_url":"http://127.0.0.1:1/mcp","tool_name":"x"}' > /dev/null 2>&1; then
    fail "extend: mcp deveria falhar sem servidor"
else pass "extend: mcp falha sem servidor"; fi

# --- skills: create / list / read / search / delete
./bin/delta.exe --tool skills '{"op":"create","name":"teste-arsenal","description":"skill de teste","content":"# Teste

Sempre rode os testes antes.
"}' > "$AR/sk1.log" 2>&1
expect "$AR/sk1.log" "Skill criada"        "skills: create"
./bin/delta.exe --tool skills '{"op":"list"}' > "$AR/sk2.log" 2>&1
expect "$AR/sk2.log" "teste-arsenal"       "skills: list"
expect "$AR/sk2.log" "skill de teste"      "skills: list mostra descricao"
./bin/delta.exe --tool skills '{"op":"read","name":"teste-arsenal"}' > "$AR/sk3.log" 2>&1
expect "$AR/sk3.log" "Sempre rode os testes" "skills: read"
./bin/delta.exe --tool skills '{"op":"search","query":"antes"}' > "$AR/sk4.log" 2>&1
expect "$AR/sk4.log" "teste-arsenal"       "skills: search"
./bin/delta.exe --tool skills '{"op":"delete","name":"teste-arsenal"}' > "$AR/sk5.log" 2>&1
expect "$AR/sk5.log" "Skill removida"      "skills: delete"
if [ -f "$ROOT/skills/teste-arsenal/SKILL.md" ]; then fail "skills: delete deixou o arquivo"; else pass "skills: delete limpa"; fi
if ./bin/delta.exe --tool skills '{"op":"read","name":"../etc"}' > /dev/null 2>&1; then
    fail "skills: aceitou nome invalido"
else pass "skills: recusa nome com caminho"; fi

# skills aparecem no prompt de sistema e no /skills
./bin/delta.exe --tool skills '{"op":"create","name":"teste-prompt","description":"aparece no prompt","content":"# teste
"}' > /dev/null 2>&1
printf 'type /skills
key enter
dump skills
' > "$AR/sk.txt"
# tela alta o bastante para a lista inteira caber: com muitas skills globais
# instaladas (/help, ~/.agents/skills) o dump de 20 linhas mostrava so' o fim
./bin/delta.exe --replay "$AR/sk.txt" 100 60 > "$AR/skui.log" 2>&1
expect "$AR/skui.log" "teste-prompt"        "/skills lista a skill"
rm -f build/mock-last-request.json
./bin/delta.exe -p "oi" --provider openai --model gpt-4o     --baseurl "http://127.0.0.1:$PORT/v1" --api-key sk-teste > /dev/null 2>&1
if grep -q "teste-prompt" build/mock-last-request.json 2>/dev/null; then
    pass "skills entram no prompt de sistema"
else fail "skills fora do prompt de sistema"; fi
./bin/delta.exe --tool skills '{"op":"delete","name":"teste-prompt"}' > /dev/null 2>&1

# --- mcp: add / list / tools / call / remove (servidor falso)
(python tests/mcp_fake.py $((PORT + 5)) > /dev/null 2>&1 & echo $! > "$AR/mcp.pid")
sleep 2
cp "$HOME/.deltaCore/mcp.json" "$AR/mcp.bak" 2>/dev/null
./bin/delta.exe --tool mcp "{\"op\":\"add\",\"name\":\"fake\",\"url\":\"http://127.0.0.1:$((PORT + 5))/mcp\"}" > "$AR/mcp1.log" 2>&1
expect "$AR/mcp1.log" "MCP registrado"     "mcp: add"
./bin/delta.exe --tool mcp '{"op":"list"}' > "$AR/mcp2.log" 2>&1
expect "$AR/mcp2.log" "fake"               "mcp: list"
./bin/delta.exe --tool mcp '{"op":"tools","name":"fake"}' > "$AR/mcp3.log" 2>&1
expect "$AR/mcp3.log" "echo"               "mcp: tools/list"
expect "$AR/mcp3.log" "soma"               "mcp: tools/list (2)"
./bin/delta.exe --tool mcp '{"op":"call","name":"fake","tool":"echo","args_json":"{\"texto\":\"oi delta\"}"}' > "$AR/mcp4.log" 2>&1
expect "$AR/mcp4.log" "ECHO: oi delta"     "mcp: tools/call"
./bin/delta.exe --tool mcp '{"op":"call","name":"fake","tool":"soma","args_json":"{\"a\":2,\"b\":40}"}' > "$AR/mcp5.log" 2>&1
expect "$AR/mcp5.log" "SOMA: 42"           "mcp: tools/call (args)"
./bin/delta.exe --tool mcp '{"op":"remove","name":"fake"}' > "$AR/mcp6.log" 2>&1
expect "$AR/mcp6.log" "MCP removido"       "mcp: remove"
[ -f "$AR/mcp.bak" ] && cp "$AR/mcp.bak" "$HOME/.deltaCore/mcp.json" || rm -f "$HOME/.deltaCore/mcp.json"
[ -f "$AR/mcp.pid" ] && kill "$(cat "$AR/mcp.pid")" 2>/dev/null
[ -f "$ROOT/build/cfg0.bak" ] && cp "$ROOT/build/cfg0.bak" "$CFG0" 2>/dev/null

# --- extensoes plugaveis: tool dinamica, ligar/desligar, comando e criacao
EXTF="$HOME/.deltaCore/extensions.json"
cp "$EXTF" "$AR/ext.bak" 2>/dev/null
./bin/delta.exe --tool extensions '{"op":"add","kind":"tool","name":"eco-ar","arg":"cmd /c echo","desc":"ecoa","schema":"{\"type\":\"object\",\"properties\":{}}"}' > "$AR/ext1.log" 2>&1
expect "$AR/ext1.log" "Extensao criada"    "extensions: add tool"
if ./bin/delta.exe --tools | grep -q "^eco-ar "; then pass "extensions: tool plugada no agente"; else fail "extensions: tool nao plugada"; fi
./bin/delta.exe --tool eco-ar '{"x":1}' > "$AR/ext2.log" 2>&1
expect "$AR/ext2.log" "x"                  "extensions: tool dinamica roda"
./bin/delta.exe --tool extensions '{"op":"disable","name":"eco-ar"}' > /dev/null 2>&1
if ./bin/delta.exe --tools | grep -q "^eco-ar "; then fail "extensions: disable nao desplugou"; else pass "extensions: disable desplugou"; fi
if grep -q '"name":"eco-ar".*"enabled":false\|"enabled":false[^}]*"name":"eco-ar"' "$EXTF" 2>/dev/null || grep -q '"enabled":false' "$EXTF"; then pass "extensions: estado persistido"; else fail "extensions: estado nao persistido"; fi
./bin/delta.exe --tool extensions '{"op":"enable","name":"eco-ar"}' > /dev/null 2>&1
if ./bin/delta.exe --tools | grep -q "^eco-ar "; then pass "extensions: enable religou"; else fail "extensions: enable falhou"; fi

# tool criada em runtime tem que valer NO MESMO processo (regressao: a tabela
# de tools era cacheada no boot e a tool nova so' aparecia depois de reiniciar)
cat > "$AR/extlive.txt" <<'EOF'
extensions {"op":"add","kind":"tool","name":"eco-live","arg":"echo","desc":"teste live","schema":"{\"type\":\"object\",\"properties\":{\"x\":{\"type\":\"string\"}}}"}
eco-live {"x":"1"}
extensions {"op":"remove","name":"eco-live"}
EOF
./bin/delta.exe --tool-script "$AR/extlive.txt" > "$AR/extlive.log" 2>&1
expect "$AR/extlive.log" '"x":"1"'  "extensions: tool criada em runtime roda no mesmo processo"
reject "$AR/extlive.log" "desconhecida"  "extensions: tool nova achada por tool_find"

# painel /extensions (abre e liga/desliga)
printf 'type /extensions
key enter
dump painel
' > "$AR/extui.txt"
./bin/delta.exe --replay "$AR/extui.txt" 100 26 > "$AR/extui.log" 2>&1
expect "$AR/extui.log" "Extensões"         "/extensions abre o painel"
expect "$AR/extui.log" "eco-ar"            "/extensions lista as extensoes"

# /extensions <pedido> cria via modelo
printf 'type /extensions cria-extensao para mim
key enter
dump criada
' > "$AR/extnew.txt"
./bin/delta.exe --replay "$AR/extnew.txt" 100 24     --provider openai --model gpt-4o --baseurl "http://127.0.0.1:$PORT/v1" --api-key sk-teste > "$AR/extnew.log" 2>&1
expect "$AR/extnew.log" "Extensao criada: teste-criado" "/extensions <pedido> cria extensao"

# comando vindo de extensao
./bin/delta.exe --tool extensions '{"op":"add","kind":"command","name":"resumo-ar","arg":"Resuma o projeto:","desc":"resumo"}' > /dev/null 2>&1
printf 'type /resumo-ar
key enter
dump cmd
' > "$AR/extcmd.txt"
./bin/delta.exe --replay "$AR/extcmd.txt" 100 24 > "$AR/extcmd.log" 2>&1
expect "$AR/extcmd.log" "Resuma o projeto:" "/extensions: comando /resumo-ar"
./bin/delta.exe --tool extensions '{"op":"remove","name":"eco-ar"}' > /dev/null 2>&1
./bin/delta.exe --tool extensions '{"op":"remove","name":"resumo-ar"}' > /dev/null 2>&1
./bin/delta.exe --tool extensions '{"op":"remove","name":"teste-criado"}' > /dev/null 2>&1
[ -f "$AR/ext.bak" ] && cp "$AR/ext.bak" "$EXTF" || rm -f "$EXTF"

# --- /translator: idioma persiste e traduz a interface
CFGL="$HOME/.deltaCore/config.json"
cp "$CFGL" "$AR/lang.bak" 2>/dev/null
printf 'type /translator en
key enter
dump en
' > "$AR/lang.txt"
./bin/delta.exe --replay "$AR/lang.txt" 100 26 > "$AR/lang.log" 2>&1
expect "$AR/lang.log" "language: English" "translator: troca para en"
if grep -q '"language": *"en"' "$CFGL" 2>/dev/null; then pass "translator: idioma salvo"; else fail "translator: idioma nao salvo"; fi
printf 'type /help
key enter
dump help
' > "$AR/langhelp.txt"
./bin/delta.exe --replay "$AR/langhelp.txt" 100 44 > "$AR/langhelp.log" 2>&1
expect "$AR/langhelp.log" "Show commands and shortcuts" "translator: /help em ingles"
[ -f "$AR/lang.bak" ] && cp "$AR/lang.bak" "$CFGL" || rm -f "$CFGL"

# --- ultima sessao: thinking/model/provedor ficam salvos
CFGP="$HOME/.deltaCore/config.json"
cp "$CFGP" "$AR/cfg2.bak" 2>/dev/null
printf 'type /thinking high
key enter
dump th
' > "$AR/th.txt"
./bin/delta.exe --replay "$AR/th.txt" 100 14 > /dev/null 2>&1
if grep -q '"thinkingLevel": *"high"' "$CFGP" 2>/dev/null; then pass "sessao: thinking salvo"; else fail "sessao: thinking nao salvo"; fi
printf 'dump inicio
' > "$AR/th2.txt"
./bin/delta.exe --replay "$AR/th2.txt" 100 14 > "$AR/th2.log" 2>&1
expect "$AR/th2.log" "thinking high"       "sessao: thinking restaurado"
[ -f "$AR/cfg2.bak" ] && cp "$AR/cfg2.bak" "$CFGP" || rm -f "$CFGP"

# --- git (repo temporario; pula sem git)
if command -v git >/dev/null 2>&1; then
    GIT="$AR/gitrepo"
    mkdir -p "$GIT"
    ( cd "$GIT" && git init -q . && git config user.email t@t && git config user.name t && echo a > a.txt && git add -A && git commit -qm inicial ) >/dev/null 2>&1
    git -C "$GIT" init -q . >/dev/null 2>&1
    ./bin/delta.exe --cwd build/arsenal/gitrepo --tool git '{"op":"status"}' > "$AR/git.log" 2>&1
    expect "$AR/git.log" "master" "git: status"
    ./bin/delta.exe --cwd build/arsenal/gitrepo --tool git '{"op":"log","n":5}' > "$AR/gitlog.log" 2>&1
    expect "$AR/gitlog.log" "inicial" "git: log"
    echo b > "$GIT/b.txt"
    ./bin/delta.exe --cwd build/arsenal/gitrepo --tool git '{"op":"checkpoint","message":"add b"}' > "$AR/gitck.log" 2>&1
    expect "$AR/gitck.log" "add b" "git: checkpoint"
else
    echo "  (sem git: pulando git)"
fi

# --- web (servidor local servindo um html)
mkdir -p "$AR/www"
printf '<html><body><h1>Arsenal Web</h1><p>texto</p></body></html>' > "$AR/www/i.html"
( cd "$AR/www" && python -m http.server $((PORT + 3)) > /dev/null 2>&1 & echo $! > "$AR/http.pid" )
sleep 2
./bin/delta.exe --tool web "{\"op\":\"scrape\",\"url\":\"http://127.0.0.1:$((PORT + 3))/i.html\"}" > "$AR/web.log" 2>&1
expect "$AR/web.log" "Arsenal Web" "web: scrape"
./bin/delta.exe --tool web "{\"op\":\"http\",\"url\":\"http://127.0.0.1:$((PORT + 3))/i.html\",\"method\":\"GET\"}" > "$AR/http.log" 2>&1
expect "$AR/http.log" "HTTP 200" "web: http"
printf '<html><head><title>NB Um</title></head><body><p>pagina um</p><a href="nb2.html">ir para dois</a></body></html>' > "$AR/www/nb1.html"
printf '<html><head><title>NB Dois</title></head><body><p>pagina dois</p></body></html>' > "$AR/www/nb2.html"
printf '%s
'   "web {\"op\":\"browser\",\"url\":\"http://127.0.0.1:$((PORT + 3))/nb1.html\"}"   'web {"op":"browser","action":"follow","index":1}'   'web {"op":"browser","action":"back"}' > "$AR/nb.txt"
./bin/delta.exe --tool-script "$AR/nb.txt" > "$AR/nb.log" 2>&1
expect "$AR/nb.log" "TITLE: NB Um"   "web: browser abre e le o titulo"
expect "$AR/nb.log" "TITLE: NB Dois" "web: browser segue link"
expect "$AR/nb.log" "Links:"         "web: browser lista links"

# sessao do micro navegador: cookie, POST, query e find
(python tests/site.py $((PORT + 4)) > /dev/null 2>&1 & echo $! > "$AR/site.pid")
sleep 2
printf '%s
'   "web {\"op\":\"browser\",\"url\":\"http://127.0.0.1:$((PORT + 4))/set\"}"   "web {\"op\":\"browser\",\"url\":\"http://127.0.0.1:$((PORT + 4))/area\"}"   "web {\"op\":\"browser\",\"action\":\"find\",\"text\":\"sessao\"}"   "web {\"op\":\"browser\",\"action\":\"open\",\"url\":\"http://127.0.0.1:$((PORT + 4))/busca\",\"query\":{\"q\":\"delta agent\"}}"   "web {\"op\":\"browser\",\"action\":\"open\",\"url\":\"http://127.0.0.1:$((PORT + 4))/post\",\"method\":\"POST\",\"body\":\"x=42\"}"   "web {\"op\":\"browser\",\"action\":\"back\"}" > "$AR/nb2.txt"
./bin/delta.exe --tool-script "$AR/nb2.txt" > "$AR/nb2.log" 2>&1
expect "$AR/nb2.log" "sessao ok"                 "browser: cookie de sessao"
expect "$AR/nb2.log" "recebi: x=42"              "browser: POST com body"
expect "$AR/nb2.log" "resultado para: delta agent" "browser: query na URL"

# charset (latin-1 -> UTF-8) e modo leitura (tira menu/rodape)
./bin/delta.exe --tool web "{\"op\":\"browser\",\"url\":\"http://127.0.0.1:$((PORT + 4))/latin1\"}" > "$AR/latin1.log" 2>&1
expect "$AR/latin1.log" "Serviço de Manutenção" "browser: converte latin-1 para UTF-8"
./bin/delta.exe --tool web "{\"op\":\"browser\",\"url\":\"http://127.0.0.1:$((PORT + 4))/latin1\",\"readable\":true}" > "$AR/read.log" 2>&1
# paragrafos: linha em branco entre eles
printf 'type paragrafos-teste
key enter
dump paras
' > "$AR/paras.txt"
./bin/delta.exe --replay "$AR/paras.txt" 100 30     --provider openai --model gpt-4o --baseurl "http://127.0.0.1:$PORT/v1" --api-key sk-teste > "$AR/paras.log" 2>&1
if grep -A 1 "Primeiro paragrafo" "$AR/paras.log" | grep -q '^||$'; then
    pass "markdown: linha em branco entre paragrafos"
else
    fail "markdown: paragrafos colados"
fi

expect "$AR/read.log" "MODE: leitura" "browser: modo leitura"
if grep -q "Home Menu" "$AR/read.log"; then fail "modo leitura deixou o menu"; else pass "modo leitura tira o menu"; fi
if grep -q "direitos reservados" "$AR/read.log"; then fail "modo leitura deixou o rodape"; else pass "modo leitura tira o rodape"; fi

# extrator de dados: JSON embutido (Next/JSON-LD/estado) e endpoints
./bin/delta.exe --tool web "{\"op\":\"data\",\"url\":\"http://127.0.0.1:$((PORT + 4))/spa\"}" > "$AR/spa.log" 2>&1
expect "$AR/spa.log" "Camisa Delta"        "data: conteudo do __NEXT_DATA__"
expect "$AR/spa.log" "R\$ 99,90"           "data: preco do JSON"
expect "$AR/spa.log" "Delta Store"         "data: vendedor"
expect "$AR/spa.log" "carrinho.total"      "data: estado inicial"
./bin/delta.exe --tool web "{\"op\":\"data\",\"url\":\"http://127.0.0.1:$((PORT + 4))/spa\",\"filter\":\"preco\"}" > "$AR/spa2.log" 2>&1
expect "$AR/spa2.log" "preco: R\$ 99,90"   "data: filtro por campo"
if grep -q "Delta Store" "$AR/spa2.log"; then fail "data: filtro nao filtrou"; else pass "data: filtro exclui o resto"; fi
./bin/delta.exe --tool web "{\"op\":\"data\",\"url\":\"http://127.0.0.1:$((PORT + 4))/spa\",\"action\":\"api\"}" > "$AR/spa3.log" 2>&1
expect "$AR/spa3.log" "/api/itens?page=2"  "data: endpoint no JS"
[ -f "$AR/site.pid" ] && kill "$(cat "$AR/site.pid")" 2>/dev/null
[ -f "$AR/http.pid" ] && kill "$(cat "$AR/http.pid")" 2>/dev/null

echo
echo "[19] delta update (release falso no GitHub)"
UPD="$AR/upd"
rm -rf "$UPD"
mkdir -p "$UPD"
# versao do binario que os testes vao atualizar (o "ja' atualizado" usa ela)
CURVER=$(./bin/delta.exe --version 2>/dev/null | awk '{print $3}')
[ -n "$CURVER" ] || { fail "update: nao consegui ler a versao atual"; CURVER=0.1.0; }
# binario "novo" falso: PE de mentira, maior que o minimo aceito
python - "$UPD/novo.bin" <<'PYEOF'
import sys
dados = b"MZ" + b"\x90" * 1024 + b"delta-atualizado-v99.9.9\n" + b"\x90" * 40000
open(sys.argv[1], "wb").write(dados)
PYEOF
(python tests/update_fake.py $((PORT + 7)) v99.9.9 Delta.exe "$UPD/novo.bin" > /dev/null 2>&1 &
 echo $! > "$UPD/upd.pid")
(python tests/update_fake.py $((PORT + 8)) "v$CURVER" Delta.exe "$UPD/novo.bin" > /dev/null 2>&1 &
 echo $! > "$UPD/atual.pid")
sleep 2
cp bin/delta.exe "$UPD/Delta.exe"
rm -rf "$UPD/core" "$UPD/core2"
UPD9="http://127.0.0.1:$((PORT + 7))/latest"
UPD1="http://127.0.0.1:$((PORT + 8))/latest"
UPDATE_ON="DELTA_NO_UPDATE_CHECK=0"

# a) aviso na inicializacao (mesmo caminho do boot da TUI)
printf 'update-check\ndump aviso\n' > "$UPD/aviso.txt"
env $UPDATE_ON DELTA_UPDATE_URL="$UPD9" DELTA_CORE="$UPD/core" \
    ./bin/delta.exe --replay "$UPD/aviso.txt" 100 30 > "$UPD/aviso.log" 2>&1
expect "$UPD/aviso.log" "99.9.9"                 "update: aviso de versao nova na inicializacao"
expect "$UPD/aviso.log" "use /update"            "update: aviso ensina o comando"

# b) o cache de 24 h evita nova consulta: com a URL quebrada o aviso continua
env $UPDATE_ON DELTA_UPDATE_URL="http://127.0.0.1:1/latest" DELTA_CORE="$UPD/core" \
    ./bin/delta.exe --replay "$UPD/aviso.txt" 100 30 > "$UPD/aviso2.log" 2>&1
expect "$UPD/aviso2.log" "99.9.9"                 "update: cache dispensa a rede"

# c) --check nao troca nada
cp "$UPD/Delta.exe" "$UPD/Delta.exe.old"          # sobra de outra tentativa
DELTA_UPDATE_URL="$UPD9" DELTA_CORE="$UPD/core" \
    "$UPD/Delta.exe" update --check > "$UPD/check.log" 2>&1
expect "$UPD/check.log" "99.9.9"                  "update --check: mostra a versao nova"
if [ -f "$UPD/Delta.exe.old" ]; then fail "update: nao limpou o .old"; else pass "update: limpa o .old no inicio"; fi
if cmp -s "$UPD/Delta.exe" bin/delta.exe; then pass "update --check: nao trocou o binario"; else fail "update --check trocou o binario"; fi

# d) delta update troca o executavel em uso
DELTA_UPDATE_URL="$UPD9" DELTA_CORE="$UPD/core" \
    "$UPD/Delta.exe" update > "$UPD/upd.log" 2>&1
expect "$UPD/upd.log" "atualizado para 99.9.9"    "update: instala a versao nova"
expect "$UPD/upd.log" "reinicie"                 "update: avisa para reiniciar"
if cmp -s "$UPD/Delta.exe" bin/delta.exe; then fail "update: binario nao foi trocado"; else pass "update: binario trocado"; fi
if [ "$(stat -c%s "$UPD/Delta.exe" 2>/dev/null)" = "$(stat -c%s "$UPD/novo.bin" 2>/dev/null)" ]; then
    pass "update: conteudo do release no lugar"
else fail "update: tamanho diferente do release"; fi

# e) ja' atualizado
DELTA_UPDATE_URL="$UPD1" DELTA_CORE="$UPD/core2" \
    ./bin/delta.exe update > "$UPD/atual.log" 2>&1
expect "$UPD/atual.log" "versao mais nova"        "update: reconhece que ja esta atualizado"

# f) sem binario para a plataforma (release so' com codigo-fonte)
(python tests/update_fake.py $((PORT + 9)) v99.9.9 Delta-macos-source.zip "$UPD/novo.bin" > /dev/null 2>&1 &
 echo $! > "$UPD/mac.pid")
sleep 2
rm -rf "$UPD/core3"
DELTA_UPDATE_URL="http://127.0.0.1:$((PORT + 9))/latest" DELTA_CORE="$UPD/core3" \
    ./bin/delta.exe update > "$UPD/mac.log" 2>&1
expect "$UPD/mac.log" "nao publica binario"      "update: avisa quando nao ha binario"

kill "$(cat "$UPD/upd.pid")" "$(cat "$UPD/atual.pid")" "$(cat "$UPD/mac.pid")" 2>/dev/null
wait "$(cat "$UPD/upd.pid")" 2>/dev/null

[ -f "$ROOT/build/cfg-global.bak" ] && cp "$ROOT/build/cfg-global.bak" "$CFG_GLOBAL" 2>/dev/null
# as chaves do usuario voltam como estavam (os testes usam chaves falsas)
if [ -f "$ROOT/build/auth-global.bak" ]; then
    cp "$ROOT/build/auth-global.bak" "$AUTH_GLOBAL" 2>/dev/null
else
    : > "$AUTH_GLOBAL" 2>/dev/null
fi
# a memoria do usuario volta como estava (os testes escrevem nela)
if [ -f "$ROOT/build/mem-global.bak" ]; then
    cp "$ROOT/build/mem-global.bak" "$MEM_GLOBAL" 2>/dev/null
fi

kill $MOCK 2>/dev/null
wait $MOCK 2>/dev/null

echo
echo "============================================================"
if [ "$FAIL" -eq 0 ]; then
    printf ' tudo certo.\n'
else
    printf ' \033[31m%d falha(s)\033[0m\n' "$FAIL"
fi
echo "============================================================"
exit "$FAIL"
