#!/usr/bin/env python3
"""Gera as paginas /install/ do site e ajusta a home (links + sitemap).

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
<link rel="stylesheet" href="../site.css?v=20261109c">
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
      <a href="../docs/"{cur_docs}>Docs</a>
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
    <a href="../docs/">Docs</a>
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
      <a href="../docs/">docs</a> &middot;
      <a href="https://github.com/descz/delta">github</a> &middot;
      <a href="https://github.com/descz/delta/releases/latest">releases</a>
    </div>
  </div>
</footer>

<script src="../site.js?v=20261109c" defer></script>
</body>
</html>
"""

INSTALL = """
  <section class="section" id="install" style="padding-top:110px">
    <div class="container">
      <p class="kicker reveal">install</p>
      <h1 class="hero-title reveal" style="text-align:left;margin:14px 0 10px;max-width:none">Install <em>Delta Agent</em></h1>
      <p class="section-sub reveal" style="margin:0 0 22px">One file per platform. The download is the
        installer: it asks for the data folder, copies the binary, puts <code>delta</code> on your PATH
        and creates the shortcut. No admin, no Node, no Python.</p>
      <p class="reveal" style="margin:0 0 6px">
        <a class="btn btn-white" href="https://github.com/descz/delta/releases/latest">Download the latest release</a>
      </p>

      <div class="osgrid" style="margin-top:30px">
        <div class="oscard reveal">
          <svg class="osicon" viewBox="0 0 24 24" aria-hidden="true"><path fill="currentColor" d="M3 5.4l7.6-1v7.1H3V5.4zm8.6-1.1L21 3v8.5h-9.4V4.3zM3 12.5h7.6v7.1L3 18.6v-6.1zm8.6 0H21V21l-9.4-1.3v-7.2z"/></svg>
          <h3>Windows <span class="tag">x64</span></h3>
          <p>Windows 10/11. Reference platform: 370 automated checks.</p>
          <a class="btn btn-outline btn-sm" href="https://github.com/descz/delta/releases/latest/download/Delta.exe">Download Delta.exe</a>
        </div>
        <div class="oscard reveal">
          <svg class="osicon" viewBox="0 0 24 24" aria-hidden="true"><path fill="currentColor" d="M12 2c-1.2 0-2 .8-2 2 0 .6.2 1.1.5 1.5C8.6 6.2 7 8.4 7 11.3c0 1.2.2 2.3.6 3.3-.8.4-1.6 1.1-1.6 2.1 0 .5.2 1 .5 1.4-.3.5-.5 1.1-.5 1.7 0 .9.6 1.6 1.4 1.9.5.2 1.1.3 1.8.3.9 0 1.7-.2 2.3-.5.5.2 1.2.3 1.9.3 1.9 0 3.6-.8 3.6-2.2 0-.6-.2-1.1-.5-1.6.3-.4.5-.9.5-1.4 0-1-.8-1.7-1.6-2.1.4-1 .6-2.1.6-3.3 0-2.9-1.6-5.1-3.5-5.8.3-.4.5-.9.5-1.5 0-1.2-.8-2-2-2zm-1.4 3.1c.4 0 .7.4.7 1s-.3 1-.7 1-.7-.4-.7-1 .3-1 .7-1zm2.8 0c.4 0 .7.4.7 1s-.3 1-.7 1-.7-.4-.7-1 .3-1 .7-1zM12 7.2l1.6 1.6-1.6.9-1.6-.9L12 7.2z"/></svg>
          <h3>Linux <span class="tag">x86_64</span></h3>
          <p>Self-extracting installer. Needs <code>curl</code> at runtime.</p>
          <a class="btn btn-outline btn-sm" href="https://github.com/descz/delta/releases/latest/download/delta-linux-x86_64.run">Download .run</a>
        </div>
        <div class="oscard reveal">
          <svg class="osicon" viewBox="0 0 24 24" aria-hidden="true"><path fill="currentColor" d="M16.5 12.6c0-1.7 1.4-2.5 1.5-2.6-.8-1.2-2.1-1.4-2.6-1.4-1.1-.1-2.1.6-2.7.6s-1.4-.6-2.3-.6c-1.2 0-2.3.7-2.9 1.8-1.2 2.1-.3 5.2.9 6.9.6.8 1.3 1.8 2.2 1.7.9 0 1.2-.6 2.3-.6s1.4.6 2.3.6c1 0 1.6-.9 2.2-1.7.7-1 1-2 1-2.1 0 0-1.9-.7-1.9-2.6zM14.7 7.3c.5-.6.8-1.4.7-2.3-.7 0-1.6.5-2.1 1.1-.5.5-.9 1.4-.8 2.2.8.1 1.6-.4 2.2-1z"/></svg>
          <h3>macOS <span class="tag">source</span></h3>
          <p>macOS 11+. Source package; builds on your machine.</p>
          <a class="btn btn-outline btn-sm" href="https://github.com/descz/delta/releases/latest/download/delta-macos-source.zip">Download source</a>
        </div>
      </div>
    </div>
  </section>

  <section class="section" id="install-how">
    <div class="container">
      <h2 class="section-title reveal">How each one <em>installs</em></h2>

      <h3 class="reveal" style="margin:22px 0 6px">Windows 10/11</h3>
      <p class="section-sub reveal" style="margin:0">Double click <code>Delta.exe</code>: a small install
        window opens (HTML popup served by the executable), you pick the data folder and it is done.
        SmartScreen warns once because the binary is not code-signed &mdash; <i>More info</i> &rarr;
        <i>Run anyway</i>.</p>

      <h3 class="reveal" style="margin:26px 0 6px">Linux</h3>
<pre class="reveal">chmod +x delta-linux-x86_64.run &amp;&amp; ./delta-linux-x86_64.run
<span class="c"># or: tar xzf delta-linux-x86_64.tar.gz &amp;&amp; cd delta-linux-x86_64 &amp;&amp; ./install.sh</span></pre>

      <h3 class="reveal" style="margin:26px 0 6px">macOS</h3>
<pre class="reveal">xcode-select --install
unzip delta-macos-source.zip &amp;&amp; cd delta-macos-source &amp;&amp; ./INSTALAR-MAC.command</pre>

      <h3 class="reveal" style="margin:26px 0 6px">First run</h3>
<pre class="reveal">delta                    <span class="c"># opens the CLI (always a new chat)</span>
/login                   <span class="c"># pick the provider, paste the key</span>
/projects ~/dev/my-app   <span class="c"># where it works (saved)</span>
delta continue           <span class="c"># reopens this folder's last chat</span>
delta update             <span class="c"># installs the newest release</span></pre>
    </div>
  </section>

  <section class="section" id="install-notes">
    <div class="container">
      <h2 class="section-title reveal">Where each platform <em>stands</em></h2>
      <p class="section-sub reveal" style="max-width:78ch">Linux builds and runs: build, CLI, TUI, real
        streaming and self-update were verified on WSL with gcc 13. The desktop layer &mdash;
        <code>.desktop</code> entry, GNOME favorites and clipboard through <code>wl-copy</code>/<code>xclip</code>
        &mdash; was never exercised on a real desktop session, and <code>curl</code> is required at
        runtime.</p>
      <p class="section-sub reveal" style="max-width:78ch">macOS ships source only: there is no prebuilt
        Mach-O because there is no reliable cross-compile. The package builds locally with the Xcode
        Command Line Tools, but the native parts (<code>osascript</code>, <code>.app</code>, Dock) were
        written and never executed by the author &mdash; expect to fix things.</p>
    </div>
  </section>
"""




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
        cur_docs=' aria-current="page"' if dirname == "docs" else "",
    )
    destino = os.path.join(DOCS, dirname)
    os.makedirs(destino, exist_ok=True)
    io.open(os.path.join(destino, "index.html"), "w", encoding="utf-8", newline="\n").write(html + corpo + FOOTER)
    print("gerado", os.path.join("docs", dirname, "index.html"))


def main():
    build("install",
          "Install Delta Agent — Windows, Linux, macOS",
          "Install Delta Agent on Windows 10/11, Linux x86_64 or macOS: one file per platform, no admin, "
          "no Node, no Python. Step by step, PATH, first run, update and data folder.",
          "Install", INSTALL)
    print("ok")


if __name__ == "__main__":
    main()
