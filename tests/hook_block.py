#!/usr/bin/env python3
"""Hook de exemplo (kind=hook, evento tool_call): bloqueia `bash` e marca read.

Recebe o caminho do payload JSON em argv[1] e imprime JSON no stdout. Serve de
modelo para o teste da secao [23] e de documentacao viva do protocolo.

uso no extensions.json:
  {"kind":"hook","name":"tool_call","arg":"python tests/hook_block.py",
   "desc":"bloqueia bash no teste","enabled":true}
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

if ev.get("event") != "tool_call":
    sys.exit(0)

if ev.get("tool") == "bash":
    print(json.dumps({"block": True, "reason": "bash proibido pelo hook do teste"}))
