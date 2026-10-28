# DocsTime — handoff do Delta Agent

> Documento para o **próximo agente** (ou para mim mesmo daqui a três semanas).
> Leia isto antes de encostar no código. Nada de surpresa: o projeto é pequeno, mas tem
> várias decisões "não óbvias" que, se você desfizer, quebram a TUI, o instalador ou os testes.

---

## 1. O que é o Delta

**Delta Agent** é um agente de codificação de terminal escrito em **C11 puro, sem nenhuma
dependência** — um único executável (544 KB no Windows) que fala com o provedor de modelo escolhido
e executa o trabalho: lê e edita arquivos, roda comandos, resolve git, consulta a web, usa skills,
MCP, memória e um grafo do projeto.

- Repositório público: <https://github.com/descz/delta> (branch `main`)
- Site (GitHub Pages, em inglês): <https://descz.github.io/delta/> (`docs/`)
- Releases: <https://github.com/descz/delta/releases> — **`v0.1.0` é o *latest***; o `v1.0.0`
  ficou marcado como **pre-release** (a numeração recomeçou por decisão do dono).
  **Atenção:** `version_cmp` compara números, então `1.0.0 > 0.1.0` — quem já tem o `1.0.0`
  instalado **não** é avisado do `0.1.0` (e `delta update` nele continuaria vendo o `0.1.0` como
  "mais antigo"). Para esses, reinstalar pelo instalador ou `delta --setup`. O `1.0.0` precisa
  continuar como *pre-release*: como release normal ele voltaria a ser o "latest" da API.
- Licença: **source-available, não comercial** — proíbe venda e clone, exige crédito. Ver `LICENSE`.
  **Não troque por MIT.**
- Plataformas: Windows 10/11 x64 (testado), Linux x86_64 (testado no WSL, gcc + clang), macOS 11+
  (compila com clang; a camada nativa `osascript`/Dock não foi executada aqui).

Interface em português por padrão, com `/translator` (pt-BR, pt, en, es, zh, ja). README, site,
página do instalador e `LEIA-ME` estão em **inglês** por decisão do dono.

---

## 2. Mapa do código (`src/`)

| arquivo | papel |
|---|---|
| `main.c` | TUI: eventos, editor, transcript, comandos (`/login`, `/projects`, `/terminal`, `/forlimit`, `/undo`…), replay |
| `agent.c` | loop do agente, sessões JSONL, montagem do prompt de sistema, config/auth, limite de tools por turno |
| `provider.c` | streaming: WinHTTP (Windows) / `curl` (POSIX), SSE, tool calls, uso de tokens |
| `codegraph.c` | grafo do projeto: varre o código uma vez, guarda arquivos/hash/símbolos/imports em `<core>/graphs/<slug>.graph` |
| `tools.c` | ferramentas base: read/write/edit/bash/ls/grep/glob/diff + `graph` + `undo` |
| `tools_ext.c` | famílias unificadas: `fs shell git web memory code assert agent extend skills mcp` |
| `setup.c` | instalador: **janela HTML** (Edge/Chrome em modo app) servida por um servidor loopback com token (`/i/<token>/`), seletor de pasta do sistema, PATH, atalhos, pin; fallback em tela nativa do terminal; `--install` sem interface |
| `update.c` | `delta update` / `/update`: consulta o release no GitHub, baixa o binário da plataforma e troca o executável em execução |
| `ui.c` | grade de células, markdown, componentes, editor |
| `term.c` | terminal: Win32 (ReadConsoleInput) / POSIX (termios) |
| `theme.c`, `okcolor.c` | temas JSON e cores OkLCH/OkHsl |
| `clipboard.c` | colagem (texto, imagem→PNG, arquivos) |
| `skills/` (raiz) | skills embutidas: `skills/<nome>/SKILL.md` — viram `src/skills_seed.h` com `make skills` e nascem em `<core>/skills` no primeiro boot |
| `json.c`, `util.c`, `i18n.c`, `extensions.c`, `modelsdev.c` | base (`util.c` também grava as skills embutidas no bootstrap) |

Gerados (não editar): `src/installer_html.h` (página do instalador + fontes em base64) e
`src/asset_logo.h` (logo). Regenere com `python tools/embed-installer.py > src/installer_html.h` e
`python tools/embed-logo.py > src/asset_logo.h` (ou `make html`).

Fora do `src/`: `installer/installer.html` (fonte da página), `docs/` (landing page), `assets/`
(logo, hero SVG, fontes, ícone), `tests/` (replay + mocks + suítes), `package.sh`, `build.bat/sh`.

---

## 3. Compilar e testar

```bash
# Windows (MinGW-w64 / Git Bash)
build.bat                 # bin\delta.exe
package.bat               # dist\Delta.exe + zip

# Linux / macOS
./build.sh                # bin/delta
./build.sh --test         # + testes de núcleo
./package.sh              # Linux: .tar.gz + .run | macOS: .app + zip (+ .dmg)
```

Testes (sem framework, tudo em bash):

```bash
bash tests/run_all.sh     # Windows: ~70 checagens (replay da TUI, mock de provedor, instalador, steer, grafo, undo, MCP stdio)
bash tests/run_posix.sh   # Linux/macOS: build, núcleo, replay, streaming real via curl, instalador, docs
```

**Regra:** nada é considerado pronto com a suíte vermelha. O `run_all.sh` restaura
`config.json`, `auth.json` e `memory.json` do usuário no fim — mantenha isso.

O coração dos testes é o **modo replay**: roda a aplicação de verdade sem terminal e despeja a tela
como texto.

```text
# tests/replay-qualquercoisa.txt
size 100 24
type /status
key enter
dump depois_do_status
mouse 2 0 click        # seleção por arrasto
steer-at 3 texto novo  # digita no meio do stream (teste headless do steer)
```

Novos comandos de replay usados nos testes: `bench`, `benchmd`, `bencht`, `mouse`, `steer-at`,
`working`, `drain`, `idle <ms>` (espera sem input: teste de maratona) e `update-check` (roda o
mesmo aviso de versão nova do boot da TUI).

---

## 4. Como mexer no código sem quebrar

1. **Zero dependências.** Não adicione bibliotecas. Rede é WinHTTP/`curl`; JSON, markdown e cores são
   nossos.
2. **Windows primeiro, POSIX junto.** Todo código de sistema vai em `#ifdef _WIN32` / `#else`.
   O POSIX precisa compilar com `gcc` e `clang` (`-Wall -Wextra` limpos).
3. **`Str` é o buffer padrão.** `str_init/str_add/str_addf/str_adds/str_steal/str_free`.
   Nunca `strcpy` em buffer fixo sem checar tamanho.
4. **A TUI é uma grade de células.** Escreva com `screen_text/screen_put/screen_para`;
   cores vêm do tema por token (`TK_*`), nunca hex solto.
5. **Transcript é incremental.** `Block.tr_y` + `tr_fast_from`/`tr_touch`: ao mudar um bloco,
   repinte **a partir dele**. Se você chamar remontagem completa em todo frame, o lag volta
   (já foi 78 ms/frame; hoje é ~0,5 ms).
6. **Ordem do protocolo.** Nunca injete mensagem de usuário no meio de um lote de tool results —
   o agente só injeta entre passos (é o que o steer faz).
7. **Sessões por pasta.** `cwd_slug()` gera o diretório; há migração de sessões antigas
   (`--cwd--`). Não simplifique para um diretório só.
8. **Undo antes de escrever.** `tools_undo_snapshot(path)` é chamado em `write`/`edit`;
   se você criar outra ferramenta que grava arquivo, chame também.
9. **Grafo:** `graph_open(cwd, 0)` monta se faltar; o prompt usa `graph_summary`. Não coloque a
   varredura dentro de loop de render — só na inicialização/turno.
10. **Assar a página do instalador:** depois de editar `installer/installer.html`, rode
    `python tools/embed-installer.py > src/installer_html.h` e **recompile**, senão o `.exe`
    continua com a página antiga.
11. **Fontes embutidas** (`assets/fonts/*.woff2`): o `embed-installer.py` injeta `@font-face` em
    base64. Se trocar a fonte, mantenha o build leve (o exe cresce ~1,4× o tamanho dos woff2).
12. **Cuidado ao gerar C com Python/sed.** Escapes (`\n`, `\\`, `\"`) já quebraram o build várias
    vezes. Prefira o tool `write` para blocos grandes de C, ou use `chr(92)`/`chr(10)` explícitos.
13. **Licença:** o produto é não comercial; não introduza dependência com licença incompatível
    nem remova os avisos.
14. **Sessão: `delta` é chat novo, `delta continue` retoma.** Não volte a retomar por padrão:
    o thinking é gravado no JSONL (`thinking`) e reconstruído por `blocks_from_msgs`; o resume
    pula arquivos de sessão vazios (senão ele "retoma" um chat que nunca existiu).
15. **Skills embutidas:** o texto vive em `skills/<nome>/SKILL.md`; `make skills` regenera
    `src/skills_seed.h` (embutido no exe). `delta_bootstrap()` grava o que faltar em
    `<core>/skills` e **nunca sobrescreve** o que o usuário editou. O prompt só lista
    nome + descrição (2400 chars) — o texto completo é lido com `skills op=read`.
16. **Subagentes pedem o modelo.** Sem `model` na chamada, o Delta abre o seletor
    (`/subagentmodel` faz o mesmo) e só depois cria; em `-p`/`--tool` (sem TUI) usa
    `subagentModel` do config e, se não houver, **recusa** — nunca chuta o modelo.
17. **Subagentes:** `agent op=subagent` (um) e `op=subagents` (até 4 em paralelo, saída em
    `<core>/jobs/*.out`). O filho roda com `--no-session` (não polui as sessões da pasta) e
    `DELTA_SUBAGENT=1` (impede recursão). Timeout padrão 900 s; `model`/`cwd`/`max_output`
    configuráveis.
18. **`/models` recarrega o catálogo sempre** (`catalog_load(&c, 1, ...)`) e **guarda só os
    provedores conectados** (`catalog_keep` + `keep_provider_conectado`): o `api.json` do models.dev
    é **um arquivo só** (5,9 MB, 233 provedores), não há endpoint por provedor — o que se ganha é
    memória/lista/mensagem escopadas, não download menor. Como o filtro apaga o resto do catálogo em
    memória, `/login` e `/logout` recarregam a lista completa do cache (`catalog_filtrado`).
    Nos testes, `DELTA_NO_CATALOG_REFRESH=1` usa o cache (sem rede). `/model` é alias silencioso.
19. **Instalador = janela HTML.** A página (`installer/installer.html` → `src/installer_html.h` com
    `make html`) é servida em `127.0.0.1/<porta>/i/<token>/` — **sem o token, 403**; a janela abre em
    modo app (`msedge --app=`, `chrome --app=`), o que dá cara de popup. A página manda `api/ping` a
    cada 2 s: fechou a janela, o servidor sai (15 s sem ping; 60 s se a página nunca carregou).
    `DELTA_SETUP_NOBROWSER=1` serve sem abrir janela (usado nos testes). Sem navegador Chromium cai
    no navegador padrão e, sem nenhum, na tela nativa (`setup_wizard`). `delta wiki` só imprime a URL
    do site. Não remova o token: era o que impedia qualquer processo local de chamar `/api/install`.
    **A página é conteúdo do produto**: ao mexer em comando/atalho, atualize
    `installer/installer.html` e rode `make html` — ela ficou desatualizada uma vez (documentava
    `/copy`, `/forlimit`, `delta wiki` abrindo navegador e `delta` retomando sessão).
    **No duplo clique a janela do console é escondida** (`console_so_nosso()` + `ShowWindow`, logo no
    início do `setup_run`) — sem isso ficava um terminal aberto atrás do popup. Ela reaparece
    (`console_mostra(1)`) só quando o popup não abre e caímos na tela nativa. Nunca esconda quando
    houver shell no mesmo console (rodar `delta --setup` num terminal) nem com
    `DELTA_SETUP_NOBROWSER=1`.
    **Efeito colateral do teste `[3f]`:** ele chama `/api/install`, e o instalador grava o binário na
    pasta padrão (`%LOCALAPPDATA%\Delta`). Rodar a suíte **sobrescreve o delta instalado na máquina**
    com o build local (mesmo código; sem ícone se o `windres` não estiver no PATH). Depois da suíte,
    recopie `dist/Delta.exe` se quiser o binário exato do release.
20. **Links clicáveis = OSC 8.** `Screen` tem `lk` (por célula) + `links[8]`; `screen_para` detecta
    `http(s)://…` e chama `screen_link`; `build_row` (term.c) emite `ESC ]8;;URL ESC \` ao redor das
    células marcadas. **Cuidado ao alocar a grade**: `alloc_grid`, `screen_ensure_rows` (realloc) e
    `screen_free` têm que tratar `lk` — esquecer o realloc do `lk` deu segfault (o teste `[7]` do
    `tests/test_main.c` cobre isso). No replay o dump vem da grade, então OSC 8 não aparece lá.
21. **O teto de ferramentas por turno foi removido inteiro** (comando `/forlimit`, `g_tool_limit` e a
    mensagem que mandava o modelo pedir `/forlimit` ao usuário). Não reintroduza o limite sem o
    comando: sobra código morto e conselho errado na boca do modelo.
22. **Nomes dos assets do release são contrato do updater.** `update.c` escolhe o arquivo por
    plataforma: `Delta.exe` (Windows) e `delta-linux-x86_64.tar.gz` (Linux, com o binário `delta`
    na raiz ou um nível abaixo). Renomear o asset quebra o `delta update` sem quebrar o instalador.
15. **`delta update` troca o executável em uso.** Windows: renomeia o exe para `.old`, grava o
    novo e apaga o `.old` no próximo início (`update_init`) — não "conserte" isso com
    `MoveFileEx(..., DELAY_UNTIL_REBOOT)`. POSIX: `.new` + `rename()` (atômico). Se mexer nisso,
    teste com um release falso (`tests/update_fake.py`), nunca apontando para o GitHub de verdade.

---

## 5. Publicar uma nova versão (GitHub)

Pré-requisito: `gh` autenticado como **descz** (`gh auth status`).

```bash
# 1. versão (aparece em --version, no /status e no release)
#    src/util.h:  #define DELTA_VERSION "0.1.0"   ->  "0.2.0"

# 2. compilar e empacotar (Windows)
taskkill //F //IM delta.exe 2>/dev/null
mingw32-make clean
mingw32-make dist            # -> dist/Delta.exe
powershell -NoProfile -Command "Compress-Archive -Path 'dist\Delta.exe','dist\LEIA-ME.txt' -DestinationPath 'dist\Delta-windows-x64.zip' -Force"

# 3. Linux (no WSL, cópia limpa para não misturar .o)
wsl -d Ubuntu -u root -e bash -lc "cd '/mnt/c/.../Delta CLI' && cp -r src installer assets Makefile build.sh package.sh package.bat /root/delta-build/ && cd /root/delta-build && bash package.sh"
#    copie de volta: /root/delta-build/dist/delta-linux-x86_64.{run,tar.gz}

# 4. macOS (pacote de código — não há cross-compile para Mach-O)
#    gere delta-macos-source.zip com src/ installer/ assets/ tools/ tests/ Makefile build.sh package.sh README.md LICENSE + INSTALAR-MAC.command

# 5. hashes
python - <<'PY'
import hashlib, io, os
d="dist"; files=["Delta.exe","Delta-windows-x64.zip","delta-linux-x86_64.run","delta-linux-x86_64.tar.gz","delta-macos-source.zip","index.html","LEIA-ME.txt"]
lines=[f"{hashlib.sha256(open(os.path.join(d,f),'rb').read()).hexdigest()}  {f}   ({os.path.getsize(os.path.join(d,f))} bytes)" for f in files if os.path.exists(os.path.join(d,f))]
io.open(os.path.join(d,"SHA256SUMS.txt"),"w",encoding="utf-8",newline="\n").write("\n".join(lines)+"\n")
print("\n".join(lines))
PY

# 6. commit + push
git add -A
git -c user.name="descz" -c user.email="deszczfc@gmail.com" commit -m "release: v1.1.0 — <o que mudou>"
git push

# 7. release
gh release create v1.1.0 --repo descz/delta --title "Delta Agent 1.1.0" --notes-file build/RELEASE.md \
  "dist/Delta.exe#Delta.exe (Windows x64)" \
  "dist/Delta-windows-x64.zip#Delta-windows-x64.zip" \
  "dist/delta-linux-x86_64.run#delta-linux-x86_64.run (Linux x86_64)" \
  "dist/delta-linux-x86_64.tar.gz#delta-linux-x86_64.tar.gz" \
  "dist/delta-macos-source.zip#delta-macos-source.zip (macOS)" \
  "dist/SHA256SUMS.txt#SHA256SUMS.txt"

# 8. ou, para refazer os assets de um release existente:
gh release upload v1.0.0 dist/Delta.exe dist/SHA256SUMS.txt --repo descz/delta --clobber
```

Notas do release: escreva em `build/RELEASE.md` (não versionado) — o modelo atual está no release
`v1.0.0`; mantenha o formato (o que é, tabela de downloads, destaques, verificação, requisitos).

**GitHub Pages** já está ligado em `main` / `/docs`; publicar o site é só dar `git push` (leva
~30–60 s). O site usa fontes em `docs/fonts/` e `docs/.nojekyll`.

---

## 6. Checklist antes de dizer "pronto"

- [ ] `bash tests/run_all.sh` → `tudo certo` (Windows) **e** `bash tests/run_posix.sh` (Linux)
- [ ] `mingw32-make clean && mingw32-make` sem warnings novos
- [ ] `installer/installer.html` editado? → `python tools/embed-installer.py > src/installer_html.h`
- [ ] `DELTA_VERSION` atualizado e `--version` conferido
- [ ] assets do release com os mesmos nomes que o `update.c` procura (`Delta.exe`, `delta-linux-x86_64.tar.gz`)
- [ ] `delta update --check` num release falso (`tests/update_fake.py`) sem trocar o binário
- [ ] `dist/` regerado, `SHA256SUMS.txt` recalculado
- [ ] `git status` limpo (não commitar `build/`, `bin/`, `dist/`, `.posix-tests/`)
- [ ] release criado/atualizado e **assets com o mesmo hash** do `dist/`
- [ ] testar o `.exe` numa pasta vazia (duplo clique → instalador; depois `delta`)

---

## 7. Armadilhas que já custaram tempo (não repita)

| sintoma | causa / correção |
|---|---|
| teclado morto durante o streaming | `term_read(ev, 0)` retornava sempre 0; poll é `WaitForSingleObject(h, 0)` |
| spinner de "Working" congelado | **três causas cumulativas**: (1) o quadro vinha de um contador de frames — hoje vem do relógio (`spin_from_clock`); (2) `WinHttpReadData`/`WinHttpReceiveResponse` bloqueiam e o timeout de recepção do WinHTTP **não** interrompe essa espera (medido com servidor lento: 1 s configurado, zero timeouts) — hoje a leitura roda numa thread ("bomba") e a UI drena o buffer a cada 15 ms; (3) quando o usuário tinha rolado para trás, o rótulo "↑ N linhas acima" substituía o status na borda — hoje "Working..." tem prioridade. |
| Ctrl+C/ESC não cortavam a espera | mesma causa (2): nada rodava enquanto o socket dormia. A bomba checa `provider_stream_abort` a cada leitura e o consumidor abandona em ~150 ms (`WaitForSingleObject(th, 150)`); a bomba órfã fecha os próprios handles. Não espere a thread com timeout longo "para garantir" — isso reintroduz o travamento. |
| 4xx repetia 3× com 5 s de espera | `provider_run` tentava de novo qualquer erro; `vale_retry()` só repete 429/5xx/rede. `StreamState.abortado` impede retry depois de Ctrl+C. |
| tool de extensão criada não aparece / lixo no schema | `tools_all()` é **cacheada** em `g_todas`; `ext_reload()` liberava as strings que ela referenciava. `ext_reload()` agora chama `tools_invalidate()`. |
| texto copiado vinha com `│ ─ ╭` e quebrado no meio | copiar da grade pega moldura e as quebras do wrap. Clique seleciona o **bloco** e o texto sai de `Block.text` (só trecho parcial vem da grade, sem moldura). |
| ler a resposta com o agente escrevendo era impossível | `scroll` é medido a partir do fim; cada linha nova empurrava o texto para baixo. No `render()`, o crescimento do transcript é descontado do `scroll` enquanto o usuário está rolado para trás (só se a largura não mudou). |
| `Ctrl+X` nunca funcionava no Linux/macOS | `clipboard_write_text` POSIX era stub (`return 0`); hoje usa wl-copy/xclip/xsel/pbcopy. No Windows: CRLF + retry no `OpenClipboard` (outro processo pode estar segurando). |
| `delta update` baixava 0 bytes / falhava no redirect | o GitHub manda o asset para `objects.githubusercontent.com`; `http_get_text_to` agora liga `WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS` (POSIX já usava `curl -L`) e tem teto de tempo. |
| a TUI fica lenta para abrir | a consulta de release no boot pode custar uma requisição. O cache (`update.json`, 24 h) evita isso depois da primeira vez; `DELTA_NO_UPDATE_CHECK=1` desliga de vez. |
| suíte vermelha só nas seções de rede/console | a TUI de verdade consultaria o GitHub no boot: `run_all.sh`/`run_posix.sh` exportam `DELTA_NO_UPDATE_CHECK=1` e as seções de update religam com `DELTA_UPDATE_URL` para o release falso. |
| `gh release create` falhou com `open build/RELEASE.md: no such file` | `mingw32-make clean` apaga `build/` inteiro — e as notas do release (`build/RELEASE.md`) moram lá, fora do git. Escreva as notas **depois** do build/clean. |
| sessão retomada "perdia" o thinking | o JSONL não gravava `thinking` e o resume escolhia o arquivo mais novo por nome — que podia ser uma sessão vazia. Hoje grava e reconstrói (`blocks_from_msgs`), e o resume pula sessão sem mensagem. |
| subagente aparecia como sessão da pasta / `continue` abria o filho | o filho herdava `session_start`; agora roda com `--no-session`. |
| release publicado com tag antiga no "latest" | o GitHub chama de *latest* o release **não-prerelease mais recente por data**, não o de número maior. Ao recomeçar a numeração, marque o release antigo como *pre-release* (`gh release edit v1.0.0 --prerelease`). |
| lag de 78 ms/frame | transcript remontava tudo; use `tr_touch`/`tr_fast_from` |
| lag de 140 ms com `/models` aberto | `auth.json` era lido do disco por provedor; agora tem cache (`auth_forget` invalida) |
| `Ctrl+C` fechava o app | removido: `Ctrl+C` interrompe/limpa, nunca sai |
| colar chave no `/login` não funcionava | rajada de colagem ia para o editor; hoje vai para o campo (`picker_insert_text`) |
| status "Working..." invisível | desenhado **antes** do `screen_hline`; ordem invertida. Hoje há uma regra a mais: o rótulo de rolagem ("↑ N linhas acima") perde para o status — veja a linha do spinner congelado. |
| sessões de todas as pastas misturadas | `cwd_slug()` era stub; hoje gera slug real + migração |
| `.exe` parado na tela antiga | faltou regerar `src/installer_html.h` e recompilar |
| C quebrado com `'\\'` | escape perdido ao gerar arquivo via Python/sed; use `chr(92)` |

---

## 8. Contato / dono

Dono do projeto: **descz** (`deszczfc@gmail.com`, GitHub `descz`).
Dúvida de rumo (features, licença, preço) → pergunte a ele antes de implementar.
