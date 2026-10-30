#!/usr/bin/env python
"""tests/concurrency.py - varias instancias gravando o mesmo memory.json.

Abre varios processos do proprio Delta escrevendo memorias ao mesmo tempo,
enquanto um leitor valida memory.json sem parar — o mesmo padrao de leitura do
file_read() (5 tentativas de 5 ms para atravessar o rename).

Antes do file_write_atomic() isso dava dezenas de leituras cortadas (JSON
invalido); na TUI, uma sessao que lia o config/memory pela metade perdia
provedor/chave e parava de responder — o "para do nada" com dois terminais.

Uso: python tests/concurrency.py [binario] [procs] [por_proc]
Sai com 1 se alguma leitura saiu corrompida.
"""
import json
import os
import subprocess
import sys
import threading
import time

BIN = sys.argv[1] if len(sys.argv) > 1 else os.path.join("bin", "delta.exe")
PROCS = int(sys.argv[2]) if len(sys.argv) > 2 else 6
POR_PROC = int(sys.argv[3]) if len(sys.argv) > 3 else 20

MEM = os.path.expanduser("~/.deltaCore/memory.json")
BACKUP = open(MEM, "rb").read() if os.path.exists(MEM) else None


def main():
    corrompidas = [0]
    nao_abriu = [0]
    exemplos = []
    para = threading.Event()

    def ler_uma():
        # mesmo padrao do file_read(): 20 tentativas de 5 ms
        for _ in range(20):
            try:
                with open(MEM, encoding="utf-8") as f:
                    json.load(f)
                return None
            except json.JSONDecodeError as e:
                return e
            except (PermissionError, FileNotFoundError):
                time.sleep(0.005)
        return "nao abriu"

    def leitor():
        while not para.is_set():
            r = ler_uma()
            if r is None:
                continue
            if r == "nao abriu":
                nao_abriu[0] += 1
            else:
                corrompidas[0] += 1
                if len(exemplos) < 2:
                    exemplos.append(str(r))

    leitura = threading.Thread(target=leitor, daemon=True)
    leitura.start()

    def escritor(i):
        for k in range(POR_PROC):
            corpo = json.dumps({"op": "remember", "title": "stress-%d-%d" % (i, k),
                                "content": "x" * 400})
            subprocess.run([BIN, "--tool", "memory", corpo],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    t0 = time.time()
    escritores = [threading.Thread(target=escritor, args=(i,)) for i in range(PROCS)]
    for t in escritores:
        t.start()
    for t in escritores:
        t.join()
    dt = time.time() - t0

    para.set()
    leitura.join(timeout=2)

    print("  %d escritas concorrentes em %.1fs (%.1f/s)"
          % (PROCS * POR_PROC, dt, (PROCS * POR_PROC) / dt if dt > 0 else 0))
    if exemplos:
        print("  exemplos de leitura corrompida: %s" % "; ".join(exemplos))
    print("  leituras corrompidas: %d | sem abrir apos 20 tentativas: %d"
          % (corrompidas[0], nao_abriu[0]))
    return 1 if (corrompidas[0] or nao_abriu[0]) else 0


try:
    rc = main()
finally:
    if BACKUP is not None:
        with open(MEM, "wb") as f:
            f.write(BACKUP)
sys.exit(rc)
