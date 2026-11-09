#!/usr/bin/env python3
"""Hook de exemplo (kind=hook, evento tool_result): gruda uma marca na saida.

Mostra o outro lado do protocolo: o hook nao bloqueia, ele reescreve o que o
modelo ve'. `append` concatena, `output` substitui.

uso no extensions.json:
  {"kind":"hook","name":"tool_result","arg":"python tests/hook_append.py",
   "desc":"marca a saida","enabled":true}
"""
import json
import sys

if len(sys.argv) < 2:
    sys.exit(0)

try:
    with open(sys.argv[1], encoding="utf-8") as f:
        ev = json.load(f)
except Exception:
    sys.exit(0)

if ev.get("event") == "tool_result":
    print(json.dumps({"append": " [hook ok]"}))
