<div align="center">

<img src="docs/logo.png" width="76" height="76" alt="Delta Agent">

<h3>A coding agent that <em>does</em> the work in your terminal</h3>

<p>Ask in your own words: <strong>read code, edit files, run commands, handle git, search the web</strong>.<br>
Delta executes it in your project, with the model you choose — and remembers the context next time.</p>

<p>
  <a href="https://github.com/descz/delta/releases/latest"><img alt="Download for your system" height="28" src="assets/buttons/download.svg"></a>
  <a href="https://descz.github.io/delta/"><img alt="Live site" height="28" src="assets/buttons/site.svg"></a>
  <a href="https://github.com/descz/delta"><img alt="GitHub" height="28" src="assets/buttons/github.svg"></a>
</p>

<p><sub>Windows · Linux · macOS (source) — one file, no Node, no Python, no admin.</sub></p>

<p>
  <a href="https://github.com/descz/delta/releases/latest"><img alt="release" height="28" src="assets/buttons/version.svg"></a>
  <img alt="C11" height="28" src="assets/buttons/c11.svg">
  <img alt="zero runtime dependencies" height="28" src="assets/buttons/deps.svg">
  <a href="LICENSE"><img alt="license: source-available, noncommercial" height="28" src="assets/buttons/license.svg"></a>
</p>

</div>

> **License:** source is available for study and personal/internal use — **selling or cloning is
> prohibited**, and any public disclosure must be **non-profit** and **credited** to the project.
> See [LICENSE](LICENSE).

A **single executable written in C11, with no dependencies at all**: no Node, Python, npm, pip or
framework installer. On Windows it is **829 KB** and about **1.7 MB of private memory**. It talks to
the provider directly (WinHTTP on Windows, `curl` elsewhere) and keeps everything in one folder.

| size | runtime dependencies | tool operations | installers |
|:--:|:--:|:--:|:--:|
| **829 KB** | **0** | **40+** | **3 systems** |

---

## Table of contents

- [What it does](#what-it-does)
- [Install](#install)
- [First steps](#first-steps)
- [Commands and shortcuts](#commands-and-shortcuts)
- [How it works](#how-it-works)
- [Where your data lives](#where-your-data-lives)
- [Build from source](#build-from-source)
- [Tests](#tests)
- [Project structure](#project-structure)
- [License](#license)

---

## What it does

| | |
|---|---|
| **Files** | reads, creates, edits precisely (exact-block `edit`), searches the project, shows diffs |
| **Shell** | runs commands, keeps background jobs, `/terminal` shows the output in a chat block |
| **Git** | status, diff, log, commit and *checkpoint* before changing anything |
| **Web** | downloads pages, extracts data, reads documentation — and speaks **MCP** over HTTP or stdio |
| **Skills** | saved workflows in `SKILL.md` — **ten ship built in**: `deltamotion` (video as C → BMP → MP4), `3jsskill` (Three.js with no build step), code design, spreadsheets/PDF, docs, automation, verification, systematic debugging, security audit, web design. Indexed in the system prompt and read on demand |
| **Extensions** | one manifest can carry several capabilities at once: `tools`, `commands` (prompt or an `exec` that runs and prints in the chat), `hooks`, `prompt` (appended to the system prompt) and `ui.status` (a third footer line). The agent can create a whole manifest itself with `extensions` op=bundle |
| **Hooks** | change the harness, not your workflow: an extension of kind `hook` intercepts `tool_call` (block it, or rewrite the arguments), `tool_result` (rewrite the output the model sees) and `turn_end`, plus the context events `session_start`, `user_message` (can refuse the message) and `turn_start`. Payload arrives as JSON in a file (`argv[1]`), the reply is JSON on stdout; 5 s timeout and fail-open, so a broken hook never stops the agent. Quote paths with spaces in `arg` |
| **Memory** | durable facts across sessions, searchable by text |
| **Project graph** | indexes symbols and imports once, then answers without re-reading every file |
| **Models** | [models.dev](https://models.dev) catalog with hundreds of models and providers |
| **Sessions** | per-folder history in JSONL with the thinking preserved; `delta` always starts a new chat, `delta continue` reopens that folder's last one |
| **Streaming** | token by token, cut immediately when you send a message mid-turn |

All inside the terminal, with its own TUI: mouse selection and copy, rendered markdown (headings,
lists, code, tables), OkLCH colors, themes, i18n (pt, en, es, zh, ja) and an installer with an HTML
screen served **by the executable itself**.

## Install

Download from [**Releases**](https://github.com/descz/delta/releases/latest). Each system has one file
that already is the installer:

| system | file | how |
|---|---|---|
| **Windows 10/11 x64** | `Delta-windows-x64.zip` (or `Delta.exe`) | double click → install window (HTML popup, native folder picker) |
| **Linux x86_64** | `delta-linux-x86_64.run` | `chmod +x delta-linux-x86_64.run && ./delta-linux-x86_64.run` |
| **macOS 11+** | `delta-macos-source.zip` | unzip and double click `INSTALAR-MAC.command` |

The installer asks **where to keep the data**, creates the folder, copies the binary, puts `delta` on
the `PATH` and creates the shortcut (taskbar on Windows, `.desktop` + GNOME favorites on Linux,
`Delta.app` + Dock on macOS). Windows may warn on SmartScreen the first time (*More info → Run
anyway*) because the binary is not code-signed.

### What has actually been tested

Being explicit, because "compiles" is not "works":

| platform | status |
|---|---|
| **Windows 10/11 x64** | **Tested** — the reference platform: 374 end-to-end checks (TUI, streaming, tools, installer, self-update), plus the installer and `delta update` exercised by hand. |
| **Linux x86_64** | **Builds and runs** (gcc 13, WSL, 32 checks: build, core, replay, real streaming via curl, installer, self-update). **Not exercised on a desktop session**: `.desktop` entry, GNOME favorites and clipboard (`wl-copy`/`xclip`) are untested. |
| **macOS 11+** | **Source only, and not tested** — I have no Mac. The release ships the source plus `INSTALAR-MAC.command`, which checks Xcode Command Line Tools and builds locally; the native layer (`osascript`, `.app`, Dock) was never executed. Treat it as a starting point, not as a supported binary. |

There is no ready-made macOS binary: the package ships the source and a `.command` that checks the
Xcode Command Line Tools (free), builds and opens the installer — there is no reliable cross-compile
to Mach-O.

## First steps

```text
$ delta
sem chave de API · use /login para colar a chave do provedor

/login                          # pick the provider, paste the key, get a default model
/projects D:\dev\my-app         # where it works (saved for next time)
read src/main.c and explain the flow
```

Next time, `delta` starts a **new chat** in that folder. To pick up the previous one — messages, tool calls and the model's
thinking included — run `delta continue`.

Inside the app, `/new` starts a fresh session and `/clear` wipes the screen.

## Commands and shortcuts

| command | what it does |
|---|---|
| `/login` `/logout` | connect/disconnect providers (one key per provider) |
| `/models` | models from connected providers — the catalog is re-downloaded on every run (a model released mid-session shows up) and **only the providers you connected are kept**; `/login` still sees the full list |
| `/thinking` | reasoning effort (off → max, depending on the model) |
| `/verbosity` | how much prose in the reply — `low`, `normal`, `high`; reasoning and the work itself stay untouched |
| `/projects <folder>` | set the working folder (saved in config) |
| `/terminal <cmd>` | runs in your shell and shows the output in the chat |
| `/undo [file]` | restores a file to its content before the last edit |
| `/update` | installs the newest version from GitHub (the app warns at startup when one exists) |
| `/subagentmodel` | model the subagents run on (it asks before creating one) |
| `/deltagroup` | Delta group on Telegram — invite link, copied to the clipboard and clickable |
| `/status` `/session` | configuration, usage and session |
| `/new` `/clear` `/reload` `/quit` | new session, clear, reload, quit |
| `/help` | all of it on screen |

`delta --setup` reopens the installer: a small **HTML window** (Edge/Chrome app mode, no tabs) served by the
executable on `127.0.0.1` with a random token — it closes with the window and never leaves a server behind.
Without a Chromium browser it falls back to a native terminal screen. `delta wiki` just prints the docs address.
`delta update` does the same job without opening the app: `delta update --check` only reports, and
the command replaces the running executable in place (on Windows it is renamed, so the file can be
swapped while it runs). The TUI checks for a new release once a day and shows a notice at startup;
turn that off with `"updateCheck": false` in `config.json` or `DELTA_NO_UPDATE_CHECK=1`.

| key | action |
|---|---|
| `Enter` / `Shift+Enter` | send / new line |
| `Ctrl+C` | with a selection: copies it to the clipboard; otherwise stops the generation (never closes the app) |
| `Ctrl+Q` | returns the last queued message to the editor |
| `Ctrl+V` | paste text, image or file |
| mouse | wheel scrolls the conversation; **drag selects** (white background, black text) and a plain click clears the selection |
| `Esc` | goes back to the end of the conversation |

**Typing while it works is supported**: the message appears in the chat marked *queued* and the agent
**cuts the reply mid-stream** to read it — no waiting for the final message.

## How it works

A turn is: *prompt → model (streaming) → tool calls → result → next step*, until the model stops.
Delta owns the loop and the tools; the model only asks.

- **Own renderer** (`ui.c`): cell grid with OkLCH colors (faithful port of OkLch/OkHsl), markdown →
  `Doc` of styled lines, tool boxes, multi-line editor with cursor. The transcript is rebuilt
  **incrementally**, block by block (`tr_y`/`tr_fast_from`), keeping a frame at ~0.5 ms even in long
  conversations.
- **Terminal** (`term.c`): raw mode with `ReadConsoleInput` on Windows and `termios` on POSIX; mouse
  (click, wheel, drag), paste, resize, alternate screen and modified-key shortcuts.
- **Network** (`provider.c`): WinHTTP on Windows, `curl` on POSIX — SSE read line by line, with
  mid-stream *abort* when you step in.
- **Agent** (`agent.c`): typed history (`user`/`assistant`/`tool`), tool calls with ids, JSONL v3
  sessions per folder, system prompt rebuilt every turn (with skills indexed and the project graph).
- **Project graph** (`codegraph.c`): indexes files, symbols and imports once into
  `~/.deltaCore/graphs/<slug>.graph`; the agent queries it with the `graph` tool instead of reading
  the whole codebase on every input.
- **Tools** (`tools.c`, `tools_ext.c`): 40+ operations grouped in families (`fs`, `shell`, `git`,
  `web`, `skills`, `mcp`, `memory`, `code`, `assert`, `agent`, `extend`), with timeouts, output
  limits and console encoding conversion. Every write keeps an undo snapshot.
- **Installer** (`setup.c`): local HTTP server (Winsock/Berkeley) serving an embedded page
  (~13 KB, fonts in base64), opens the browser and performs the install — native folder picker,
  binary copy, `PATH`, shortcuts and pinning.

The look (themes, color tokens, sizes) follows the same vocabulary as a modern editor: 56 theme
tokens, plain `.json` files in `themes/`.

## Where your data lives

One folder, chosen at install time (default `C:\Users\<you>\.deltaCore`):

```
.deltaCore/
  config.json        provider, model, language, thinking, projectDir
  auth.json          one API key per provider
  models.v2.json     cached models.dev catalog
  memory.json        durable memory
  sessions/          --<folder-slug>--/<date>_<id>.jsonl
  skills/            <name>/SKILL.md
  mcps/              one .json per MCP server (url or command)
  extensions/        pluggable extensions
  hooks/            payload temporario dos hooks (apagado a cada chamada)
  graphs/            project graph cache (symbols/imports)
  update.json        last release check (version, asset, timestamp)
  system/prompt.md   rules it must always follow
  paste/ jobs/ themes/ undo/
```

`system/prompt.md` is re-read every turn: that is where you write your team rules.

**Several windows at once is supported.** Every file in this folder is written to a temporary and
swapped in one step (`file_write_atomic`), so a second `delta` — or a subagent — never reads a
half-written `config.json`, `auth.json`, `memory.json` or catalog. Without that, the instance that
reads a truncated config loses provider and key and its session looks dead.

## Build from source

You only need a C compiler and the system libc.

**Windows** (MinGW-w64 / Git Bash):

```bat
build.bat            :: bin\delta.exe
package.bat          :: dist\Delta.exe + zip
```

**Linux / macOS**:

```bash
./build.sh           # bin/delta
./build.sh --test    # + core tests
./package.sh         # tarball + .run (Linux) | .app + zip + dmg (macOS)
```

On macOS the Xcode Command Line Tools (`xcode-select --install`) are enough. At runtime Linux uses
`curl` to talk to the provider.

## Tests

Two suites, no framework:

```bash
bash tests/run_all.sh     # Windows: 374 checks in 58 sections (TUI replay, provider mock, installer)

# programmatic surface (CI): one JSON event per line
delta -p "review src/" --json | jq -c 'select(.type=="tool_result")'

# the specification the binary carries
delta docs

# export the recent session of this folder (markdown or NDJSON)
delta export > transcript.md
delta export --json > transcript.jsonl
bash tests/run_posix.sh   # Linux/macOS: build, core, replay, real streaming, installer, docs
```

The **replay** mode runs the real application without a terminal (`--replay script.txt 110 36`) and
dumps the screen as text — that is how the TUI is tested in CI. The fake provider
(`tests/mock_openai.py`) covers streaming, tool calls, HTTP errors, retries and mid-stream steering.

## Project structure

```
src/
  main.c        TUI, commands, editor, transcript
  agent.c       agent loop, sessions, system prompt, config/auth
  provider.c    streaming (WinHTTP / curl), tool calls, token usage
  codegraph.c   project graph: files, symbols, imports
  tools.c       fs, shell, git, diff, web, undo
  tools_ext.c   skills, mcp (HTTP/stdio), memory, code, assert, agent, extend
  setup.c       installer (local server + embedded HTML screen)
  ui.c          cell grid, markdown, components
  term.c        terminal (Win32 / POSIX)
  theme.c okcolor.c  themes and OkLCH colors
  clipboard.c   paste (text, image, files)
  json.c util.c i18n.c extensions.c modelsdev.c
installer/      installer page (embedded in the exe)
docs/           landing page (GitHub Pages)
assets/         logo, hero, screenshots
tests/          replay scripts + mocks + suites
```

## License

**Source-available, non-commercial.** The source is public for study and personal/internal use, but
the license **forbids selling, renting, paid access and cloning** — and requires credit to the
product in any public disclosure.

| allowed | forbidden |
|---|---|
| use, study and modify for personal, educational or internal use | sell, rent, charge for access, include in a paid product/service |
| share the **unmodified** copy, non-profit | publish a public fork, mirror, package, binary or **clone/rebrand** |
| cite, write, record videos — **with credit** | removing credits, the name, the Δ symbol or this notice |

Mandatory credit: **Delta Agent — https://github.com/descz/delta**.
Commercial permission, cloning or special redistribution: open an issue in the repository.

<br>

<div align="center">

<img src="docs/logo.png" width="40" height="40" alt="">

<p><strong>Delta Agent</strong> — <a href="https://descz.github.io/delta/">site</a> ·
<a href="https://github.com/descz/delta">github</a> ·
<a href="https://github.com/descz/delta/releases/latest">releases</a> ·
<a href="LICENSE">license</a></p>

<p><sub>C11 · zero dependencies · Windows, Linux, macOS (source)</sub></p>

</div>
