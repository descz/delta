#!/usr/bin/env python3
"""Embute installer/installer.html em src/installer_html.h (string C).

Injeta tambem as fontes (assets/fonts/*.woff2) em base64, para a pagina
ficar autossuficiente dentro do executavel.

Uso:  python tools/embed-installer.py > src/installer_html.h
"""
import base64
import io
import os
import sys

BS = chr(92)
NQ = chr(34)
NL = chr(10)


def face(family, path, weight):
    if not os.path.exists(path):
        return ""
    b = base64.b64encode(open(path, "rb").read()).decode()
    return ("@font-face{font-family:'" + family + "';font-style:normal;font-weight:"
            + weight + ";font-display:swap;src:url(data:font/woff2;base64," + b
            + ") format('woff2')}")


css = (face("Orbitron", "assets/fonts/orbitron.woff2", "700 900")
       + face("Space Mono", "assets/fonts/spacemono.woff2", "400 700"))

src = io.open("installer/installer.html", encoding="utf-8").read()
if css:
    src = src.replace("<head>", "<head>" + NL + "<style>" + css + "</style>", 1)

out = []
out.append("/* installer_html.h - GERADO de installer/installer.html (nao editar a mao).")
out.append("   Regenerar: python tools/embed-installer.py > src/installer_html.h */")
out.append("#ifndef DELTA_INSTALLER_HTML_H")
out.append("#define DELTA_INSTALLER_HTML_H")
out.append("")
out.append("static const char INSTALLER_HTML[] =")
for line in src.split(NL):
    esc = line.replace(BS, BS + BS).replace(NQ, BS + NQ)
    out.append("    " + NQ + esc + BS + "n" + NQ)
out.append(";")
out.append("")
out.append("#endif /* DELTA_INSTALLER_HTML_H */")
data = NL.join(out) + NL
sys.stdout.buffer.write(data.encode("utf-8"))
