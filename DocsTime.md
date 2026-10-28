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
- Releases: <https://github.com/descz/delta/releases> (`v1.0.0` no momento)
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
| `setup.c` | instalador: servidor HTTP local, página embutida, seletor de pasta, PATH, atalhos, pin |
| `ui.c` | grade de células, markdown, componentes, editor |
| `term.c` | terminal: Win32 (ReadConsoleInput) / POSIX (termios) |
| `theme.c`, `okcolor.c` | temas JSON e cores OkLCH/OkHsl |
| `clipboard.c` | colagem (texto, imagem→PNG, arquivos) |
| `json.c`, `util.c`, `i18n.c`, `extensions.c`, `modelsdev.c` | base |

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
`working`, `drain`.

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

---

## 5. Publicar uma nova versão (GitHub)

Pré-requisito: `gh` autenticado como **descz** (`gh auth status`).

```bash
# 1. versão (aparece em --version, no /status e no release)
#    src/util.h:  #define DELTA_VERSION "1.0.0"   ->  "1.1.0"

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
- [ ] `dist/` regerado, `SHA256SUMS.txt` recalculado
- [ ] `git status` limpo (não commitar `build/`, `bin/`, `dist/`, `.posix-tests/`)
- [ ] release criado/atualizado e **assets com o mesmo hash** do `dist/`
- [ ] testar o `.exe` numa pasta vazia (duplo clique → instalador; depois `delta`)

---

## 7. Armadilhas que já custaram tempo (não repita)

| sintoma | causa / correção |
|---|---|
| teclado morto durante o streaming | `term_read(ev, 0)` retornava sempre 0; poll é `WaitForSingleObject(h, 0)` |
| lag de 78 ms/frame | transcript remontava tudo; use `tr_touch`/`tr_fast_from` |
| lag de 140 ms com `/models` aberto | `auth.json` era lido do disco por provedor; agora tem cache (`auth_forget` invalida) |
| `Ctrl+C` fechava o app | removido: `Ctrl+C` interrompe/limpa, nunca sai |
| colar chave no `/login` não funcionava | rajada de colagem ia para o editor; hoje vai para o campo (`picker_insert_text`) |
| status "Working..." invisível | desenhado **antes** do `screen_hline`; ordem invertida |
| sessões de todas as pastas misturadas | `cwd_slug()` era stub; hoje gera slug real + migração |
| `.exe` parado na tela antiga | faltou regerar `src/installer_html.h` e recompilar |
| C quebrado com `'\\'` | escape perdido ao gerar arquivo via Python/sed; use `chr(92)` |

---

## 8. Contato / dono

Dono do projeto: **descz** (`deszczfc@gmail.com`, GitHub `descz`).
Dúvida de rumo (features, licença, preço) → pergunte a ele antes de implementar.
