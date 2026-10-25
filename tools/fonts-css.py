#!/usr/bin/env python3
"""Gera o CSS @font-face com as fontes embutidas em base64.

Uso:  python tools/fonts-css.py > build/fonts.css
"""
import base64
import os
import sys

def face(family, path, weight):
    if not os.path.exists(path):
        return ""
    b = base64.b64encode(open(path, "rb").read()).decode()
    return ("@font-face{font-family:'" + family + "';font-style:normal;font-weight:"
            + weight + ";font-display:swap;src:url(data:font/woff2;base64," + b
            + ") format('woff2')}" + chr(10))

sys.stdout.write(face("Orbitron", "assets/fonts/orbitron.woff2", "700 900")
                 + face("Space Mono", "assets/fonts/spacemono.woff2", "400 700"))
