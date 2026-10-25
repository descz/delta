#!/usr/bin/env python3
"""Embute deltalogo.png em src/asset_logo.h (array de bytes).

Uso:  python tools/embed-logo.py > src/asset_logo.h
"""
import io
import sys

data = open("deltalogo.png", "rb").read()
w = sys.stdout.buffer.write
out = []
out.append("/* asset_logo.h - GERADO de deltalogo.png (nao editar a mao).")
out.append("   Regenerar: python tools/embed-logo.py > src/asset_logo.h */")
out.append("#ifndef DELTA_ASSET_LOGO_H")
out.append("#define DELTA_ASSET_LOGO_H")
out.append("")
out.append("static const unsigned char LOGO_PNG[] = {")
line = []
for i, b in enumerate(data):
    line.append("0x%02x," % b)
    if len(line) == 20:
        out.append("    " + " ".join(line))
        line = []
if line:
    out.append("    " + " ".join(line))
out.append("};")
out.append("static const unsigned long LOGO_PNG_LEN = %dUL;" % len(data))
out.append("")
out.append("#endif /* DELTA_ASSET_LOGO_H */")
w(("\n".join(out) + "\n").encode("utf-8"))
