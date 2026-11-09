"""Gera a pagina /docs/ com layout de documentacao (sidebar + artigo + TOC).

Separado do gen-site.py para nao inflar: aqui vive so' a montagem das secoes.
Reaproveita INSTALL/PERGUNTAS/DOCS_HTML do gen-site para nao duplicar texto.
"""
import io
import os
import re
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(RAIZ, "tools"))
import importlib.util

spec = importlib.util.spec_from_file_location("gensite", os.path.join(RAIZ, "tools", "gen-site.py"))
gs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gs)          # carrega constantes sem rodar main() (guard __main__)

DOCS = os.path.join(RAIZ, "docs")

# (id, titulo, grupo) — ordem do sidebar e do TOC
SECOES = [
    ("intro", "Intro", "START"),
    ("install", "Install", "START"),
    ("config", "Config", "REFERENCE"),
    ("tools", "Tools", "REFERENCE"),
    ("sessions", "Sessions", "REFERENCE"),
    ("ci", "CI surface", "REFERENCE"),
    ("extensions", "Extensions", "CONFIGURE"),
    ("hooks", "Hooks", "CONFIGURE"),
    ("security", "Security", "TRUST"),
    ("limits", "Limits", "TRUST"),
]


def toc():
    linhas = ['    <nav class="doctoc">', '      <p class="docnav-h">ON THIS PAGE</p>']
    for sid, titulo, _ in SECOES:
        linhas.append('      <a href="#%s" data-sec="%s">%s</a>' % (sid, sid, titulo))
    linhas.append('    </nav>')
    return "\n".join(linhas)


def esc(t):
    return t.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def bloco(codigo, titulo):
    return ('<div class="codewrap"><div class="codehdr"><span>%s</span>'
            '<button class="copybtn" data-copy>Copy</button></div>'
            '<pre>%s</pre></div>' % (esc(titulo), esc(codigo)))


def artigo():
    a = []
    a.append('''
    <article class="docmain">
      <p class="kicker">docs</p>
      <h1>Delta Agent — documentation</h1>
      <p class="lead">What the agent does, exactly: the loop, the tools, the session format, the CI
        surface, the extension contract. The same text ships inside the binary as <code>delta docs</code>.</p>

      <h2 id="intro">Intro</h2>
      <p>Delta is a terminal coding agent: one C11 binary, no runtime dependencies, working with the
        model you choose. You ask in your own words; it reads and edits files by exact blocks, runs
        commands, handles git, searches the web, keeps memory and speaks MCP.</p>
      <p>A turn is <code>prompt → provider (streaming) → tool calls → results → next step</code> until
        the model answers without asking for a tool. Delta owns the loop and the tools; the model only asks.
        On Windows the whole agent is <b>829 KB</b> and about 1.7 MB of private memory. The source is
        available under a non-commercial license: study and personal/internal use are allowed, selling,
        paid access and cloning are not (<a href="https://github.com/descz/delta/blob/main/LICENSE">LICENSE</a>).</p>
''' + bloco('''DELTA_MAX_STEPS      200      steps in one turn
tool output          200 KB   per tool result (process killed at the cap)
context              8 MB     per request; above it, emergency prune (4 outputs x 20 KB)
empty provider turn  6 nudges after an HTTP 200 with nothing inside, then a real error''',
            'limits · env overrides') + '''
      <h2 id="install">Install</h2>
      <p>Every download <b>is</b> the installer: it asks for the data folder, copies the binary, puts
        <code>delta</code> on your PATH and creates the shortcut. No admin, no Node, no Python.</p>
''' + bloco('''# windows 10/11 x64 - double click, or the zip
Delta.exe

# linux x86_64 - self-extracting installer (needs curl at runtime)
chmod +x delta-linux-x86_64.run && ./delta-linux-x86_64.run

# macOS 11+ - source package, builds locally
xcode-select --install
unzip delta-macos-source.zip && cd delta-macos-source && ./INSTALAR-MAC.command''',
            'releases · per platform') + '''
      <p>You need an API key from a provider you use (OpenAI, Anthropic, OpenRouter, DeepSeek, opencode
        and others) — <code>/login</code> asks once and stores it per provider. Without network the CLI,
        sessions and local tools still work; the model, obviously, does not.</p>
      <p>Windows may warn on SmartScreen the first time (not code-signed): <i>More info</i> then
        <i>Run anyway</i>. The installer reopens with <code>delta --setup</code>. Linux was verified in
        WSL — the desktop layer (<code>.desktop</code>, GNOME favorites, clipboard) was not exercised on
        a real desktop. macOS ships source only; the native parts were never run by the author.</p>
''' + bloco('''delta                    # opens the CLI (always a new chat)
/login                   # pick the provider, paste the key
/projects ~/dev/my-app   # where it works (saved for next time)
read src/main.c and explain the flow

delta continue           # reopens this folder's last chat, thinking included
delta update --check     # only reports; `delta update` installs''',
            'first run') + '''
      <h2 id="config">Config</h2>
      <p>Everything lives in one folder, chosen at install time (default <code>~/.deltaCore</code>).
        <code>config.json</code> holds provider, model, language, thinking level, verbosity and the
        project folder; <code>auth.json</code> holds one API key per provider.</p>
''' + bloco('''{
  "provider": "opencode-go",
  "model": "deepseek-v4.1-flash",
  "baseUrl": "https://opencode.ai/inference/go/openai/v1",
  "language": "pt-BR",
  "thinkingLevel": "max",
  "verbosity": "low",
  "maxTokens": 8192,
  "projectDir": "",
  "updateCheck": false
}''', 'config.json') + '''
      <p>Slash commands that matter here: <code>/login</code>, <code>/models</code> (catalog from
        models.dev), <code>/thinking</code> (reasoning effort), <code>/verbosity</code> (how much prose),
        <code>/projects</code>, <code>/status</code>, <code>/docs</code>. Any server that speaks the
        OpenAI chat API works as a local model through <code>baseUrl</code>. The footer shows the real
        context window of the current model, read from the catalog, with 200k as fallback.</p>

      <h2 id="tools">Tools</h2>
      <p><code>read write edit undo bash ls find grep graph</code> plus the unified families
        <code>fs shell git web memory code assert agent extend skills mcp extensions</code>, each taking
        an <code>op</code>. Every write keeps an undo snapshot; <code>bash</code> has a timeout,
        converts Windows OEM output to UTF-8 and is killed when ESC/Ctrl+C asks to stop (exit 130).</p>

      <h2 id="sessions">Sessions</h2>
      <p>One JSONL per session, per folder. <code>delta</code> always starts a new chat,
        <code>delta continue</code> reopens the last one with the full history and the model's thinking.</p>
''' + bloco('''~/.deltaCore/sessions/--<folder-slug>--/<date>_<id>.jsonl
{"type":"session","version":3,"id":"...","cwd":"..."}
{"type":"message","message":{"role":"user|assistant|tool","content":"...","thinking":"..."}}

delta export            # markdown transcript of the last session
delta export --json     # NDJSON of the same messages''',
            'sessions · export') + '''
      <h2 id="ci">CI surface</h2>
      <p>One JSON event per line, ready for <code>jq</code> or a CI step. Exit codes: <code>0</code>
        ok, <code>1</code> agent error, <code>124</code> command timeout.</p>
''' + bloco('''delta -p "review src/" --json

{"type":"text","text":"..."}
{"type":"thinking","text":"..."}
{"type":"tool_call","name":"bash","args":"{...}"}
{"type":"tool_result","name":"bash","ok":true,"seconds":1.1,"bytes":10}
{"type":"turn_end","in":1234,"out":456}
{"type":"error","message":"..."}''', 'ndjson events') + '''
      <h2 id="extensions">Extensions</h2>
      <p>One manifest can change several parts of the harness at once. Files with a single
        <code>kind</code> (tool, skill, command, mcp, hook) keep working.</p>
''' + bloco('''[
  {
    "name": "guardrails",
    "enabled": true,
    "prompt":   "rules appended to the system prompt",
    "tools":    [{"name": "x", "arg": "cmd", "desc": "...", "schema": "{...}"}],
    "commands": [{"name": "deploy", "arg": "optional prompt", "exec": "cmd"}],
    "hooks":    [{"event": "tool_call", "arg": "cmd"}],
    "ui":       {"status": "cmd"}
  }
]''', '~/.deltaCore/extensions.json') + '''
      <p>The agent can create one itself: <code>extensions</code> tool, <code>op=add</code> for a
        single capability or <code>op=bundle</code> for a whole manifest. It takes effect on the next
        tool call. MCP servers ride the same folder (<code>mcps/</code>, JSON-RPC over HTTP or stdio), and
        skills are <code>SKILL.md</code> files indexed in the system prompt and read on demand.</p>

      <h2 id="hooks">Hooks</h2>
      <p>A hook is code, not a prompt: the harness runs it, so a blocked call never happens. The
        payload arrives as a JSON file path in <code>argv[1]</code>; the reply is JSON on stdout.
        5 s timeout, 8 KB cap, fail-open. Paths with spaces must be quoted inside <code>arg</code>.</p>
''' + bloco('''tool_call      {"block":true,"reason":"..."} | {"args":"{...}"}
tool_result    {"output":"..."} | {"append":"..."}
user_message   {"block":true,"reason":"..."}     before any request leaves
turn_start     ignored reply                     before each provider call
turn_end       ignored reply
session_start  once per process''', 'events') + '''
      <h2 id="security">Security</h2>
      <p>Requests go only to the provider you configure. History, keys, memory and skills stay in
        your local folder, and every file is written to a temporary and swapped in one step, so two
        windows never read half a config.</p>
      <p><b>Honest limits:</b> keys are plain text in <code>auth.json</code> (the OS keyring is not
        wired yet); there is no system sandbox — hooks and extensions run with the same permissions as
        Delta; only install extensions you trust.</p>

      <h2 id="limits">Limits</h2>
      <p>macOS is source-only, with the native layer untested. Linux desktop integration is untested.
        The UI is not extensible: no in-process plugin, so no widgets, keys or overlays from an
        extension — what an extension can change is behaviour (prompt, tools, commands, hooks, footer
        status), not pixels.</p>
''' + '''
    </article>''')
    return "\n".join(a)


HEAD = '''<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Delta Agent docs — the specification</title>
<meta name="description" content="Delta Agent documentation: the turn loop and its ceilings, install per platform, config, tools, sessions and export, the NDJSON surface for CI, extensions, hooks and the honest limits.">
<meta name="robots" content="index, follow, max-image-preview:large">
<link rel="canonical" href="https://descz.github.io/delta/docs/">
<meta name="theme-color" content="#000000">
<meta property="og:type" content="article">
<meta property="og:site_name" content="Delta Agent">
<meta property="og:title" content="Delta Agent docs — the specification">
<meta property="og:description" content="The loop, install, config, tools, sessions, CI surface, extensions and hooks — and the honest limits.">
<meta property="og:url" content="https://descz.github.io/delta/docs/">
<meta property="og:image" content="https://descz.github.io/delta/logo-512.png">
<link rel="icon" href="../icon-96.png" type="image/png" sizes="96x96">
<link rel="apple-touch-icon" href="../apple-touch-icon.png" sizes="180x180">
<link rel="preload" href="../fonts/inter.woff2" as="font" type="font/woff2" crossorigin>
<link rel="stylesheet" href="../site.css?v=20261109l">
<script type="application/ld+json">
{"@context":"https://schema.org","@type":"BreadcrumbList","itemListElement":[
 {"@type":"ListItem","position":1,"name":"Delta Agent","item":"https://descz.github.io/delta/"},
 {"@type":"ListItem","position":2,"name":"Docs","item":"https://descz.github.io/delta/docs/"}]}
</script>
</head>
<body>

<header class="navbar scrolled" id="navbar">
  <div class="nav-inner">
    <a href="../" class="brand" aria-label="Delta Agent">
      <img class="brand-mark" src="../icon-96.png" alt="" width="26" height="26" decoding="async">
      <span class="brand-name">Delta <span class="brand-dim">Agent</span></span>
    </a>
    <nav class="nav-links">
      <a href="../#overview">Overview</a>
      <a href="./" aria-current="page">Docs</a>
    </nav>
    <div class="nav-right">
      <a href="../install/" class="btn btn-white btn-sm">Install</a>
      <a class="gh" href="https://github.com/descz/delta" aria-label="Delta Agent on GitHub" title="GitHub">
        <svg viewBox="0 0 16 16" aria-hidden="true" focusable="false"><path d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.013 8.013 0 0 0 16 8c0-4.42-3.58-8-8-8Z"/></svg>
      </a>
    </div>
  </div>
</header>

'''

WRAP_INI = '''<main class="docwrap" id="top">
'''

WRAP_FIM = '''
</main>

<footer>
  <div class="container foot">
    <div class="brand"><img class="brand-mark" src="../icon-96.png" alt="" width="26" height="26" decoding="async"><span class="brand-name">Delta <span class="brand-dim">Agent</span></span></div>
    <div style="font-size:12.5px;color:#5f5f5f">
      <a href="../">home</a> &middot;
      <a href="../install/">install</a> &middot;
      <a href="../docs/">docs</a> &middot;
      <a href="https://github.com/descz/delta">github</a> &middot;
      <a href="https://github.com/descz/delta/releases/latest">releases</a>
    </div>
  </div>
</footer>

<script src="../site.js?v=20261109l" defer></script>
</body>
</html>
'''


def main():
    html = HEAD + WRAP_INI + artigo() + toc() + WRAP_FIM
    destino = os.path.join(DOCS, "docs")
    os.makedirs(destino, exist_ok=True)
    io.open(os.path.join(destino, "index.html"), "w", encoding="utf-8", newline="\n").write(html)
    print("gerado docs/docs/index.html")


if __name__ == "__main__":
    main()
