#!/usr/bin/env python3
"""Servidor falso com UMA ferramenta que cospe saida enorme.

Primeiro turno: tool_call `bash` que imprime 400 KB. Depois: texto final.
Serve para provar que o Delta trunca a saida antes de montar o request (caso
real: `find /c /v ""` no Windows caiu no find do MSYS, listou o disco C inteiro
e gerou um request de 267 MB, que o provedor nunca respondeu).

Fica separado do mock_openai.py de proposito: aquele decide pelo texto da ultima
mensagem do usuario, e o prompt do /dialeto carrega o `git diff` do projeto —
token de teste dentro do diff fazia o mock errado responder.

uso: python tests/mock_grande.py <porta>
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer

PORTA = int(sys.argv[1]) if len(sys.argv) > 1 else 8099


def sse(obj):
    return ("data: " + json.dumps(obj) + "\n\n").encode()


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def do_POST(self):
        bruto = self.rfile.read(int(self.headers.get("Content-Length") or 0)) or b"{}"
        with open("build/mock-last-request.json", "wb") as f:
            f.write(bruto)
        corpo = json.loads(bruto)
        tem_resultado = any(m.get("role") == "tool" for m in corpo.get("messages", []))
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Transfer-Encoding", "chunked")
        self.end_headers()

        def chunk(dados):
            self.wfile.write(b"%X\r\n" % len(dados))
            self.wfile.write(dados)
            self.wfile.write(b"\r\n")
            self.wfile.flush()

        if not tem_resultado:
            args = json.dumps({"command": 'python -c "print(chr(65)*400000)"'})
            eventos = [
                {"id": "g", "choices": [{"index": 0, "delta": {"role": "assistant", "tool_calls": [
                    {"index": 0, "id": "call_g", "type": "function",
                     "function": {"name": "bash", "arguments": args}}]}}]},
                {"id": "g", "choices": [{"index": 0, "delta": {}, "finish_reason": "tool_calls"}]},
            ]
        else:
            eventos = [
                {"id": "g", "choices": [{"index": 0, "delta": {
                    "content": "saida grande tratada: recebi o resultado truncado"}}]},
                {"id": "g", "choices": [{"index": 0, "delta": {}, "finish_reason": "stop"}]},
            ]
        for e in eventos:
            chunk(sse(e))
        chunk(b"data: [DONE]\n\n")
        self.wfile.write(b"0\r\n\r\n")
        self.wfile.flush()


print("mock grande na porta %d" % PORTA, flush=True)
HTTPServer(("127.0.0.1", PORTA), Handler).serve_forever()
