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
<link rel="stylesheet" href="../site.css?v=20261109l">
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
      <a href="../docs/"{cur_docs}>Docs</a>
    </nav>
    <div class="nav-right">
      <a href="../install/" class="btn btn-white btn-sm">Install</a>
      <a class="gh" href="https://github.com/descz/delta" aria-label="Delta Agent on GitHub" title="GitHub">
        <svg viewBox="0 0 16 16" aria-hidden="true" focusable="false"><path d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.013 8.013 0 0 0 16 8c0-4.42-3.58-8-8-8Z"/></svg>
      </a>
      <button class="nav-toggle" id="navToggle" aria-label="Open menu"><span></span><span></span><span></span></button>
    </div>
  </div>
  <div class="nav-mobile" id="navMobile">
    <a href="../#overview">Overview</a>
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

<script src="../site.js?v=20261109l" defer></script>
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
          <svg class="osicon" viewBox="0 0 24 24" aria-hidden="true"><path fill="currentColor" d="M12.504 0c-.155 0-.315.008-.48.021-4.226.333-3.105 4.807-3.17 6.298-.076 1.092-.3 1.953-1.05 3.02-.885 1.051-2.127 2.75-2.716 4.521-.278.832-.41 1.684-.287 2.489a.424.424 0 00-.11.135c-.26.268-.45.6-.663.839-.199.199-.485.267-.797.4-.313.136-.658.269-.864.68-.09.189-.136.394-.132.602 0 .199.027.4.055.536.058.399.116.728.04.97-.249.68-.28 1.145-.106 1.484.174.334.535.47.94.601.81.2 1.91.135 2.774.6.926.466 1.866.67 2.616.47.526-.116.97-.464 1.208-.946.587-.003 1.23-.269 2.26-.334.699-.058 1.574.267 2.577.2.025.134.063.198.114.333l.003.003c.391.778 1.113 1.132 1.884 1.071.771-.06 1.592-.536 2.257-1.306.631-.765 1.683-1.084 2.378-1.503.348-.199.629-.469.649-.853.023-.4-.2-.811-.714-1.376v-.097l-.003-.003c-.17-.2-.25-.535-.338-.926-.085-.401-.182-.786-.492-1.046h-.003c-.059-.054-.123-.067-.188-.135a.357.357 0 00-.19-.064c.431-1.278.264-2.55-.173-3.694-.533-1.41-1.465-2.638-2.175-3.483-.796-1.005-1.576-1.957-1.56-3.368.026-2.152.236-6.133-3.544-6.139zm.529 3.405h.013c.213 0 .396.062.584.198.19.135.33.332.438.533.105.259.158.459.166.724 0-.02.006-.04.006-.06v.105a.086.086 0 01-.004-.021l-.004-.024a1.807 1.807 0 01-.15.706.953.953 0 01-.213.335.71.71 0 00-.088-.042c-.104-.045-.198-.064-.284-.133a1.312 1.312 0 00-.22-.066c.05-.06.146-.133.183-.198.053-.128.082-.264.088-.402v-.02a1.21 1.21 0 00-.061-.4c-.045-.134-.101-.2-.183-.333-.084-.066-.167-.132-.267-.132h-.016c-.093 0-.176.03-.262.132a.8.8 0 00-.205.334 1.18 1.18 0 00-.09.4v.019c.002.089.008.179.02.267-.193-.067-.438-.135-.607-.202a1.635 1.635 0 01-.018-.2v-.02a1.772 1.772 0 01.15-.768c.082-.22.232-.406.43-.533a.985.985 0 01.594-.2zm-2.962.059h.036c.142 0 .27.048.399.135.146.129.264.288.344.465.09.199.14.4.153.667v.004c.007.134.006.2-.002.266v.08c-.03.007-.056.018-.083.024-.152.055-.274.135-.393.2.012-.09.013-.18.003-.267v-.015c-.012-.133-.04-.2-.082-.333a.613.613 0 00-.166-.267.248.248 0 00-.183-.064h-.021c-.071.006-.13.04-.186.132a.552.552 0 00-.12.27.944.944 0 00-.023.33v.015c.012.135.037.2.08.334.046.134.098.2.166.268.01.009.02.018.034.024-.07.057-.117.07-.176.136a.304.304 0 01-.131.068 2.62 2.62 0 01-.275-.402 1.772 1.772 0 01-.155-.667 1.759 1.759 0 01.08-.668 1.43 1.43 0 01.283-.535c.128-.133.26-.2.418-.2zm1.37 1.706c.332 0 .733.065 1.216.399.293.2.523.269 1.052.468h.003c.255.136.405.266.478.399v-.131a.571.571 0 01.016.47c-.123.31-.516.643-1.063.842v.002c-.268.135-.501.333-.775.465-.276.135-.588.292-1.012.267a1.139 1.139 0 01-.448-.067 3.566 3.566 0 01-.322-.198c-.195-.135-.363-.332-.612-.465v-.005h-.005c-.4-.246-.616-.512-.686-.71-.07-.268-.005-.47.193-.6.224-.135.38-.271.483-.336.104-.074.143-.102.176-.131h.002v-.003c.169-.202.436-.47.839-.601.139-.036.294-.065.466-.065zm2.8 2.142c.358 1.417 1.196 3.475 1.735 4.473.286.534.855 1.659 1.102 3.024.156-.005.33.018.513.064.646-1.671-.546-3.467-1.089-3.966-.22-.2-.232-.335-.123-.335.59.534 1.365 1.572 1.646 2.757.13.535.16 1.104.021 1.67.067.028.135.06.205.067 1.032.534 1.413.938 1.23 1.537v-.043c-.06-.003-.12 0-.18 0h-.016c.151-.467-.182-.825-1.065-1.224-.915-.4-1.646-.336-1.77.465-.008.043-.013.066-.018.135-.068.023-.139.053-.209.064-.43.268-.662.669-.793 1.187-.13.533-.17 1.156-.205 1.869v.003c-.02.334-.17.838-.319 1.35-1.5 1.072-3.58 1.538-5.348.334a2.645 2.645 0 00-.402-.533 1.45 1.45 0 00-.275-.333c.182 0 .338-.03.465-.067a.615.615 0 00.314-.334c.108-.267 0-.697-.345-1.163-.345-.467-.931-.995-1.788-1.521-.63-.4-.986-.87-1.15-1.396-.165-.534-.143-1.085-.015-1.645.245-1.07.873-2.11 1.274-2.763.107-.065.037.135-.408.974-.396.751-1.14 2.497-.122 3.854a8.123 8.123 0 01.647-2.876c.564-1.278 1.743-3.504 1.836-5.268.048.036.217.135.289.202.218.133.38.333.59.465.21.201.477.335.876.335.039.003.075.006.11.006.412 0 .73-.134.997-.268.29-.134.52-.334.74-.4h.005c.467-.135.835-.402 1.044-.7zm2.185 8.958c.037.6.343 1.245.882 1.377.588.134 1.434-.333 1.791-.765l.211-.01c.315-.007.577.01.847.268l.003.003c.208.199.305.53.391.876.085.4.154.78.409 1.066.486.527.645.906.636 1.14l.003-.007v.018l-.003-.012c-.015.262-.185.396-.498.595-.63.401-1.746.712-2.457 1.57-.618.737-1.37 1.14-2.036 1.191-.664.053-1.237-.2-1.574-.898l-.005-.003c-.21-.4-.12-1.025.056-1.69.176-.668.428-1.344.463-1.897.037-.714.076-1.335.195-1.814.12-.465.308-.797.641-.984l.045-.022zm-10.814.049h.01c.053 0 .105.005.157.014.376.055.706.333 1.023.752l.91 1.664.003.003c.243.533.754 1.064 1.189 1.637.434.598.77 1.131.729 1.57v.006c-.057.744-.48 1.148-1.125 1.294-.645.135-1.52.002-2.395-.464-.968-.536-2.118-.469-2.857-.602-.369-.066-.61-.2-.723-.4-.11-.2-.113-.602.123-1.23v-.004l.002-.003c.117-.334.03-.752-.027-1.118-.055-.401-.083-.71.043-.94.16-.334.396-.4.69-.533.294-.135.64-.202.915-.47h.002v-.002c.256-.268.445-.601.668-.838.19-.201.38-.336.663-.336zm7.159-9.074c-.435.201-.945.535-1.488.535-.542 0-.97-.267-1.28-.466-.154-.134-.28-.268-.373-.335-.164-.134-.144-.333-.074-.333.109.016.129.134.199.2.096.066.215.2.36.333.292.2.68.467 1.167.467.485 0 1.053-.267 1.398-.466.195-.135.445-.334.648-.467.156-.136.149-.267.279-.267.128.016.034.134-.147.332a8.097 8.097 0 01-.69.468zm-1.082-1.583V5.64c-.006-.02.013-.042.029-.05.074-.043.18-.027.26.004.063 0 .16.067.15.135-.006.049-.085.066-.135.066-.055 0-.092-.043-.141-.068-.052-.018-.146-.008-.163-.065zm-.551 0c-.02.058-.113.049-.166.066-.047.025-.086.068-.14.068-.05 0-.13-.02-.136-.068-.01-.066.088-.133.15-.133.08-.031.184-.047.259-.005.019.009.036.03.03.05v.02h.003z"/></svg>
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
<div class="codewrap"><div class="codehdr"><span>linux</span><button class="copybtn" data-copy>Copy</button></div><pre>chmod +x delta-linux-x86_64.run &amp;&amp; ./delta-linux-x86_64.run
<span class="c"># or: tar xzf delta-linux-x86_64.tar.gz &amp;&amp; cd delta-linux-x86_64 &amp;&amp; ./install.sh</span></pre></div>

      <h3 class="reveal" style="margin:26px 0 6px">macOS</h3>
<div class="codewrap"><div class="codehdr"><span>macOS</span><button class="copybtn" data-copy>Copy</button></div><pre>xcode-select --install
unzip delta-macos-source.zip &amp;&amp; cd delta-macos-source &amp;&amp; ./INSTALAR-MAC.command</pre></div>

      <h3 class="reveal" style="margin:26px 0 6px">First run</h3>
<div class="codewrap"><div class="codehdr"><span>first run</span><button class="copybtn" data-copy>Copy</button></div><pre>delta                    <span class="c"># opens the CLI (always a new chat)</span>
/login                   <span class="c"># pick the provider, paste the key</span>
/projects ~/dev/my-app   <span class="c"># where it works (saved)</span>
delta continue           <span class="c"># reopens this folder's last chat</span>
delta update             <span class="c"># installs the newest release</span></pre></div>
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
