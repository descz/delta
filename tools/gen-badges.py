#!/usr/bin/env python3
"""Gera os badges do README como SVG do proprio repositorio.

Motivo: img.shields.io falhava de forma intermitente no GitHub (o camo cacheia o
erro e sobra o alt quebrado). Como o README e' a vitrine, os badges viraram
arquivos versionados aqui: asset local, sem terceiro na cadeia.

O badge de versao sai de DELTA_VERSION (src/util.h), entao package.bat/package.sh
chamam este script antes de empacotar — o README nunca anuncia versao errada.
O texto usa textLength/lengthAdjust, entao nao corta seja qual for a fonte que o
navegador escolher (SVG em <img> nao herda a fonte da pagina).

Uso: python tools/gen-badges.py [--check]
     --check nao escreve; sai com 1 se algum arquivo estiver diferente.
"""
import io
import os
import re
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEST = os.path.join(RAIZ, "assets", "buttons")

ALTURA = 28
RAIO = 13.5
PAD = 10
FONTE = 'Inter, &quot;Segoe UI&quot;, -apple-system, Roboto, Helvetica, Arial, sans-serif'

# largura media por caractere (px) a 12 px. O textLength corrige a diferenca;
# isto e' so' para o retangulo nao ficar apertado no pior fallback.
def largura(txt, negrito=True):
    return len(txt) * (6.4 if negrito else 6.0)


def pill(label, valor, cor_valor="#ffffff"):
    wl = largura(label, False) + PAD * 2
    wv = largura(valor, True) + PAD * 2
    total = wl + wv
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{total:.0f}" height="{ALTURA}" '
        f'viewBox="0 0 {total:.0f} {ALTURA}" role="img" aria-label="{label}: {valor}">\n'
        f'  <rect x="0.5" y="0.5" width="{total - 1:.0f}" height="{ALTURA - 1}" rx="{RAIO}" '
        f'fill="#0d0d0d" stroke="#2a2a2a"/>\n'
        f'  <text x="{PAD}" y="18" font-family="{FONTE}" font-size="12" font-weight="500" '
        f'fill="#8a8a8a" textLength="{wl - PAD * 2:.1f}" lengthAdjust="spacingAndGlyphs">{label}</text>\n'
        f'  <text x="{wl + PAD}" y="18" font-family="{FONTE}" font-size="12" font-weight="600" '
        f'fill="{cor_valor}" textLength="{wv - PAD * 2:.1f}" lengthAdjust="spacingAndGlyphs">{valor}</text>\n'
        f'</svg>\n'
    )


def versao():
    h = io.open(os.path.join(RAIZ, "src", "util.h"), encoding="utf-8").read()
    m = re.search(r'#define\s+DELTA_VERSION\s+"([^"]+)"', h)
    if not m:
        sys.exit("DELTA_VERSION nao encontrado em src/util.h")
    return m.group(1)


def main():
    check = "--check" in sys.argv
    v = versao()
    badges = {
        "version.svg": pill("release", "v" + v),
        "c11.svg": pill("C", "11"),
        "deps.svg": pill("dependencies", "0"),
        "license.svg": pill("license", "source-available · noncommercial"),
    }
    ruim = 0
    for nome, corpo in badges.items():
        caminho = os.path.join(DEST, nome)
        atual = ""
        if os.path.exists(caminho):
            atual = io.open(caminho, encoding="utf-8").read()
        if atual == corpo:
            print("ok    " + nome)
            continue
        if check:
            print("DIFERE " + nome)
            ruim += 1
            continue
        io.open(caminho, "w", encoding="utf-8", newline="\n").write(corpo)
        print("gerado " + nome)
    if check:
        return 1 if ruim else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
