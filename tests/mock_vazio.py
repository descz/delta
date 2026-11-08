#!/usr/bin/env python3
"""Servidor falso que reproduz o turno VAZIO de um provedor atras de proxy:
HTTP 200, stream SSE fechado com finish_reason stop e nada dentro (sem content,
sem tool_calls).

uso:
  python tests/mock_vazio.py <porta> uma     # primeiro turno vazio; depois responde
  python tests/mock_vazio.py <porta> sempre  # todo turno vazio (provedor insistindo)

Existe separado do mock_openai.py de proposito: aquele decide pelo texto da
ultima mensagem do usuario, e o prompt do /dialeto carrega o `git diff` do
projeto — token de teste dentro do diff fazia o mock errado responder.
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer

PORTA = int(sys.argv[1]) if len(sys.argv) > 1 else 8099
MODO = sys.argv[2] if len(sys.argv) > 2 else "uma"
VAZIOS = [0]


def sse(obj):
    return ("data: " + json.dumps(obj) + "\n\n").encode()


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def do_POST(self):
        corpo = json.loads(self.rfile.read(int(self.headers.get("Content-Length") or 0)) or b"{}")
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
            eventos = [
                {"id": "v", "choices": [{"index": 0, "delta": {"role": "assistant", "tool_calls": [
                    {"index": 0, "id": "call_v", "type": "function",
                     "function": {"name": "read",
                                  "arguments": "{\"path\": \"tests/mock-target.txt\"}"}}]}}]},
                {"id": "v", "choices": [{"index": 0, "delta": {}, "finish_reason": "tool_calls"}]},
            ]
        else:
            VAZIOS[0] += 1
            with open("build/mock-vazio-count.txt", "w", encoding="utf-8") as f:
                f.write(str(VAZIOS[0]))
            if MODO == "uma" and VAZIOS[0] > 1:
                eventos = [
                    {"id": "v", "choices": [{"index": 0, "delta": {
                        "content": "O arquivo contem: 42 e o caminho lido foi tests/mock-target.txt"}}]},
                    {"id": "v", "choices": [{"index": 0, "delta": {}, "finish_reason": "stop"}]},
                ]
            else:
                eventos = [{"id": "v", "choices": [{"index": 0, "delta": {}, "finish_reason": "stop"}]}]
        for e in eventos:
            chunk(sse(e))
        chunk(b"data: [DONE]\n\n")
        self.wfile.write(b"0\r\n\r\n")
        self.wfile.flush()


print("mock vazio (%s) na porta %d" % (MODO, PORTA), flush=True)
HTTPServer(("127.0.0.1", PORTA), Handler).serve_forever()
