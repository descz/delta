#!/usr/bin/env python3
"""Gera as paginas /install/ e /faq/ do site e ajusta a home (links + sitemap).

Rodar de qualquer lugar: python tools/gen-site.py
Os arquivos gerados sao estaticos (sem build no deploy); este script existe para
manter cabecalho/rodape identicos entre as paginas.
"""
import io
import json
import os
import re
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS = os.path.join(RAIZ, "docs")
BASE = "https://descz.github.io/delta/"

HEADER = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">

<title>{title}</title>
<meta name="description" content="{desc}">
<meta name="robots" content="index, follow, max-image-preview:large">
<link rel="canonical" href="{base}{slug}/">
<meta name="theme-color" content="#000000">
<meta property="og:type" content="article">
<meta property="og:site_name" content="Delta Agent">
<meta property="og:title" content="{title}">
<meta property="og:description" content="{desc}">
<meta property="og:url" content="{base}{slug}/">
<meta property="og:image" content="{base}logo-512.png">
<meta name="twitter:card" content="summary">
<meta name="twitter:image" content="{base}logo-512.png">
<link rel="icon" href="../icon-96.png" type="image/png" sizes="96x96">
<link rel="apple-touch-icon" href="../apple-touch-icon.png" sizes="180x180">
<link rel="preload" href="../fonts/inter.woff2" as="font" type="font/woff2" crossorigin>
<link rel="stylesheet" href="../site.css">
<script type="application/ld+json">
{breadcrumb}
</script>
{extra_ld}</head>
<body>

<header class="navbar" id="navbar">
  <div class="nav-inner">
    <a href="../" class="brand" aria-label="Delta Agent">
      <img class="brand-mark" src="../icon-96.png" alt="" width="26" height="26" decoding="async">
      <span class="brand-name">Delta <span class="brand-dim">Agent</span></span>
    </a>
    <nav class="nav-links">
      <a href="../#overview">Overview</a>
      <a href="../#features">What it does</a>
      <a href="../install/"{cur_install}>Install</a>
      <a href="../faq/"{cur_faq}>FAQ</a>
    </nav>
    <div class="nav-right">
      <a href="https://github.com/descz/delta/releases/latest" class="btn btn-white btn-sm">Download</a>
      <a class="gh" href="https://github.com/descz/delta" aria-label="Delta Agent on GitHub" title="GitHub">
        <svg viewBox="0 0 16 16" aria-hidden="true" focusable="false"><path d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.013 8.013 0 0 0 16 8c0-4.42-3.58-8-8-8Z"/></svg>
      </a>
      <button class="nav-toggle" id="navToggle" aria-label="Open menu"><span></span><span></span><span></span></button>
    </div>
  </div>
  <div class="nav-mobile" id="navMobile">
    <a href="../#overview">Overview</a>
    <a href="../#features">What it does</a>
    <a href="../install/">Install</a>
    <a href="../faq/">FAQ</a>
    <a href="https://github.com/descz/delta">GitHub</a>
  </div>
</header>

<main id="top">
"""

FOOTER = """
</main>

<footer>
  <div class="container foot">
    <div class="brand"><img class="brand-mark" src="../icon-96.png" alt="" width="26" height="26" decoding="async"><span class="brand-name">Delta <span class="brand-dim">Agent</span></span></div>
    <div style="font-size:12.5px;color:#5f5f5f">
      <a href="../">home</a> &middot;
      <a href="../install/">install</a> &middot;
      <a href="../faq/">faq</a> &middot;
      <a href="https://github.com/descz/delta">github</a> &middot;
      <a href="https://github.com/descz/delta/releases/latest">releases</a>
    </div>
  </div>
</footer>

<script src="../site.js" defer></script>
</body>
</html>
"""

INSTALL = """
  <section class="section" id="requirements" style="padding-top:110px">
    <div class="container">
      <p class="kicker reveal">install</p>
      <h1 class="hero-title reveal" style="text-align:left;margin:14px 0 10px;max-width:none">Install <em>Delta Agent</em></h1>
      <p class="section-sub reveal">
        One file per platform. The download <b>is</b> the installer: it asks for the data folder,
        copies the binary, puts <code>delta</code> on your PATH and creates the shortcut. No admin,
        no Node, no Python, no package manager.
      </p>

      <div class="grid g3" style="margin-top:26px">
        <div class="card os-card reveal"><h3>Windows 10/11 <span class="tag">x64</span></h3>
          <p><code>Delta.exe</code> &mdash; 809 KB, program and installer in one file.
             The reference platform: 353 automated checks.</p></div>
        <div class="card os-card reveal"><h3>Linux <span class="tag">x86_64</span></h3>
          <p><code>delta-linux-x86_64.run</code> &mdash; self-extracting. Needs <code>curl</code> at
             runtime to reach the provider.</p></div>
        <div class="card os-card reveal"><h3>macOS 11+ <span class="tag">source</span></h3>
          <p>Source package with <code>INSTALAR-MAC.command</code>: builds on your machine with the
             Xcode Command Line Tools.</p></div>
      </div>
    </div>
  </section>

  <section class="section" id="windows">
    <div class="container">
      <h2 class="section-title reveal">Windows 10/11 <em>x64</em></h2>
      <p class="section-sub reveal">Download <code>Delta.exe</code> (or
        <code>Delta-windows-x64.zip</code>) from
        <a href="https://github.com/descz/delta/releases/latest">Releases</a> and double click.</p>
      <ol class="reveal" style="color:var(--muted2);line-height:1.9;max-width:70ch">
        <li>Double click <code>Delta.exe</code>. A small install window opens &mdash; an HTML popup
            served by the executable itself, with no browser tab left behind.</li>
        <li>Pick where the data lives (default <code>C:\\Users\\&lt;you&gt;\\.deltaCore</code>).</li>
        <li>The installer copies the binary, adds <code>delta</code> to your user <code>PATH</code>
            and creates the shortcut. No administrator rights.</li>
        <li>Open a <b>new</b> terminal and run <code>delta</code>.</li>
      </ol>
<pre class="reveal"><span class="c"># SmartScreen on the first run</span>
"Windows protected your PC" -&gt; More info -&gt; Run anyway
<span class="c"># the binary is not code-signed; that is the whole story, nothing else</span></pre>
      <p class="section-sub reveal">The installer reopens at any time with <code>delta --setup</code>.
        To uninstall: delete the data folder and remove <code>delta</code> from <code>PATH</code>
        &mdash; there is no registry entry and no service.</p>
    </div>
  </section>

  <section class="section" id="linux">
    <div class="container">
      <h2 class="section-title reveal">Linux <em>x86_64</em></h2>
      <p class="section-sub reveal">Two shapes: the self-extracting installer or the plain tarball.</p>
<pre class="reveal"><span class="c"># self-extracting installer (installs and puts delta on PATH)</span>
chmod +x delta-linux-x86_64.run &amp;&amp; ./delta-linux-x86_64.run

<span class="c"># or the tarball</span>
tar xzf delta-linux-x86_64.tar.gz &amp;&amp; cd delta-linux-x86_64 &amp;&amp; ./install.sh

<span class="c"># check</span>
delta --version</pre>
      <p class="section-sub reveal">Verified on WSL with gcc 13: build, CLI, TUI, real streaming and
        self-update. The desktop layer &mdash; <code>.desktop</code> entry, GNOME favorites and the
        clipboard through <code>wl-copy</code>/<code>xclip</code> &mdash; was not exercised on a
        desktop session, so treat that part as untested. <code>curl</code> must be installed.</p>
    </div>
  </section>

  <section class="section" id="macos">
    <div class="container">
      <h2 class="section-title reveal">macOS 11+ <em>source</em></h2>
      <p class="section-sub reveal">There is no prebuilt Mach-O: the package ships the source and a
        script that builds it locally.</p>
<pre class="reveal">xcode-select --install          <span class="c"># free, once</span>
unzip delta-macos-source.zip
cd delta-macos-source &amp;&amp; ./INSTALAR-MAC.command</pre>
      <p class="section-sub reveal">The script checks the Xcode Command Line Tools, builds and opens
        the installer. The native parts (<code>osascript</code>, <code>.app</code>, Dock) were written
        but never executed by the author &mdash; no Mac available. This is the least supported of the
        three paths.</p>
    </div>
  </section>

  <section class="section" id="first-run">
    <div class="container">
      <h2 class="section-title reveal">First <em>run</em></h2>
<pre class="reveal">delta                    <span class="c"># opens the CLI (always a new chat)</span>
/login                   <span class="c"># pick the provider, paste the key</span>
/projects ~/dev/my-app   <span class="c"># where it works (saved for next time)</span>
read src/main.c and explain the flow

delta continue           <span class="c"># reopens this folder's last chat, thinking included</span>
delta update --check     <span class="c"># only reports; `delta update` installs</span></pre>
      <p class="section-sub reveal">Keys are stored per provider in <code>auth.json</code> inside the
        data folder. Nothing leaves the machine except the requests to the provider you configured.</p>
    </div>
  </section>

  <section class="section" id="data">
    <div class="container">
      <h2 class="section-title reveal">Where your data <em>lives</em></h2>
      <p class="section-sub reveal">One folder, chosen at install time (default
        <code>~/.deltaCore</code>). Every file is written to a temporary and swapped in one step, so a
        second window never reads half a config.</p>
<pre class="reveal">.deltaCore/
  config.json        provider, model, language, thinking, projectDir
  auth.json          one API key per provider
  sessions/          --&lt;folder-slug&gt;--/&lt;date&gt;_&lt;id&gt;.jsonl
  skills/            &lt;name&gt;/SKILL.md
  mcps/ extensions/  servers and pluggable extensions (tool, skill, command, mcp, hook)
  memory.json        durable facts, searchable
  graphs/            project graph cache (symbols and imports)
  system/prompt.md   team rules, re-read every turn</pre>
      <p class="section-sub reveal">Backup is copying the folder. Migration is the same: point the
        installer at the new path.</p>
    </div>
  </section>
"""

PERGUNTAS = [
    ("Getting started", [
        ("Do I need an API key?",
         "Yes, from a provider you use (OpenAI, Anthropic, OpenRouter, DeepSeek, opencode and others). "
         "<code>/login</code> asks for the key once, stores it per provider in <code>auth.json</code> "
         "and picks a default model."),
        ("Does it run offline?",
         "The program runs, the model does not. Without network the CLI, sessions and the local tools "
         "still work, but the agent cannot talk to the provider."),
        ("Which platforms are supported?",
         "Windows 10/11 x64 (the reference platform), Linux x86_64 (builds and runs; desktop integration "
         "untested) and macOS from source. There is no prebuilt macOS binary."),
        ("Is there a Windows installer?",
         "The download is the installer: double click <code>Delta.exe</code>, choose the data folder, and "
         "it copies the binary, sets <code>PATH</code> and creates the shortcut. No admin rights."),
        ("How big is it?",
         "809 KB single executable on Windows, about 1.7 MB of private memory, zero runtime dependencies."),
    ]),
    ("Models and providers", [
        ("Which models can I use?",
         "Any model in the models.dev catalog from the providers you connected &mdash; Anthropic, OpenAI, "
         "OpenRouter, DeepSeek, opencode and others. <code>/models</code> lists what your keys allow."),
        ("Can I use a local model?",
         "If the server exposes the OpenAI-compatible chat API, point the OpenAI provider at its base URL "
         "(config <code>baseUrl</code> or <code>--baseurl</code>)."),
        ("How do I change the model or the thinking level?",
         "<code>/models</code> switches the model, <code>/thinking</code> sets the reasoning effort and "
         "<code>/verbosity</code> sets how much prose the reply carries. All saved in the config."),
        ("What is the context window shown in the footer?",
         "The real <code>limit.context</code> of the current model, read from the catalog (1M for a 1M "
         "model), with 200k as fallback when the catalog does not know the model."),
    ]),
    ("Extending", [
        ("Does it support MCP?",
         "Yes &mdash; JSON-RPC over HTTP or stdio. Servers live as one file each in <code>mcps/</code>, and "
         "their tools appear to the model like the built-in ones."),
        ("Can I add my own tools, commands, skills or hooks?",
         "Yes, and they live in one manifest: an extension can declare <code>tools</code>, "
         "<code>commands</code> (a prompt, or an <code>exec</code> that runs and prints in the chat), "
         "<code>hooks</code> (intercepting <code>tool_call</code>, <code>tool_result</code> and "
         "<code>turn_end</code> to block, rewrite or log), a <code>prompt</code> appended to the system "
         "prompt and <code>ui.status</code> for a third footer line. The agent can create them itself "
         "with the <code>extensions</code> tool."),
        ("Can it write and run code for me?",
         "It reads and edits files by exact blocks, runs shell commands with timeouts and background jobs, "
         "handles git, and can generate code in any language. Two skills ship for that: "
         "<code>deltamotion</code> (video as C code to MP4) and <code>3jsskill</code> (Three.js without a "
         "build step)."),
    ]),
    ("Data, license and limits", [
        ("Where does my data go?",
         "Only to the provider you configure. History, keys, memory and skills stay in your local data "
         "folder; each file is written atomically so parallel windows never corrupt it."),
        ("Is it open source?",
         "Source-available and non-commercial. The code is public for study and personal or internal use, "
         "but the license forbids selling, charging for access and cloning. Sharing the unmodified copy, "
         "non-profit and with credit, is allowed. See the "
         "<a href=\"https://github.com/descz/delta/blob/main/LICENSE\">LICENSE</a>."),
        ("Windows said the app is unknown. Is it safe?",
         "That is SmartScreen because the binary is not code-signed: <i>More info</i>, then "
         "<i>Run anyway</i>. The source in the repository is exactly what builds that binary."),
        ("Was Linux tested on a real desktop?",
         "Partially. Build, CLI, TUI, real streaming and self-update were verified on Linux (WSL, gcc 13). "
         "The desktop integration &mdash; <code>.desktop</code> entry, GNOME favorites, clipboard &mdash; "
         "was not exercised on a desktop session."),
    ]),
]


def breadcrumbs(nome, slug):
    return json.dumps({
        "@context": "https://schema.org",
        "@type": "BreadcrumbList",
        "itemListElement": [
            {"@type": "ListItem", "position": 1, "name": "Delta Agent", "item": BASE},
            {"@type": "ListItem", "position": 2, "name": nome, "item": BASE + slug + "/"},
        ],
    }, ensure_ascii=False, indent=2)


def build(dirname, title, desc, nome, corpo, extra_ld=""):
    html = HEADER.format(
        title=title, desc=desc, slug=dirname, base=BASE,
        breadcrumb=breadcrumbs(nome, dirname),
        extra_ld=extra_ld + ("\n" if extra_ld else ""),
        cur_install=' aria-current="page"' if dirname == "install" else "",
        cur_faq=' aria-current="page"' if dirname == "faq" else "",
    )
    destino = os.path.join(DOCS, dirname)
    os.makedirs(destino, exist_ok=True)
    io.open(os.path.join(destino, "index.html"), "w", encoding="utf-8", newline="\n").write(html + corpo + FOOTER)
    print("gerado", os.path.join("docs", dirname, "index.html"))


def faq_body():
    out = ['  <section class="section" id="faq" style="padding-top:110px">',
           '    <div class="container">',
           '      <p class="kicker reveal">faq</p>',
           '      <h1 class="hero-title reveal" style="text-align:left;margin:14px 0 10px;max-width:none">Short answers, <em>no marketing</em></h1>',
           '      <p class="section-sub reveal">Everything below is what the code actually does. '
           'If an answer says "untested", it is untested.</p>']
    for grupo, itens in PERGUNTAS:
        out.append('      <h2 class="section-title reveal" style="font-size:22px;margin:38px 0 6px">%s</h2>' % grupo)
        for q, a in itens:
            out.append('      <h3 class="reveal" style="font-size:16px;margin:18px 0 4px">%s</h3>' % q)
            out.append('      <p class="section-sub reveal" style="margin:0">%s</p>' % a)
    out.append('      <p class="section-sub reveal" style="margin-top:34px">Still stuck? Open an issue in the '
               '<a href="https://github.com/descz/delta/issues">repository</a>.</p>')
    out.append('    </div>')
    out.append('  </section>')
    return "\n".join(out)


def faq_ld():
    itens = []
    for _, pares in PERGUNTAS:
        for q, a in pares:
            itens.append({
                "@type": "Question",
                "name": q,
                "acceptedAnswer": {"@type": "Answer", "text": re.sub(r"<[^>]+>", "", a).strip()},
            })
    return ('<script type="application/ld+json">\n'
            + json.dumps({"@context": "https://schema.org", "@type": "FAQPage", "mainEntity": itens},
                         ensure_ascii=False, indent=2)
            + '\n</script>')


def main():
    build("install",
          "Install Delta Agent — Windows, Linux, macOS",
          "Install Delta Agent on Windows 10/11, Linux x86_64 or macOS: one file per platform, no admin, "
          "no Node, no Python. Step by step, PATH, first run, update and data folder.",
          "Install", INSTALL)
    build("faq",
          "Delta Agent FAQ — API key, models, MCP, license",
          "Answers about Delta Agent: API key and providers, local models, context window, MCP, "
          "extensions and hooks, where data lives and what the license allows.",
          "FAQ", faq_body(), faq_ld())
    print("ok")


if __name__ == "__main__":
    main()
