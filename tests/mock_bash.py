#!/usr/bin/env python3
"""Mock que pede UMA tool `bash` (comando vindo do argv) e depois responde texto.

uso: python tests/mock_bash.py <porta> "<comando>"
Serve para exercitar hooks do harness: se o hook bloquear, o comando nao roda e
o arquivo/marca que ele criaria nao existe.
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer

PORTA = int(sys.argv[1]) if len(sys.argv) > 1 else 8099
COMANDO = sys.argv[2] if len(sys.argv) > 2 else "echo ok"


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
            args = json.dumps({"command": COMANDO})
            eventos = [
                {"id": "b", "choices": [{"index": 0, "delta": {"role": "assistant", "tool_calls": [
                    {"index": 0, "id": "call_b", "type": "function",
                     "function": {"name": "bash", "arguments": args}}]}}]},
                {"id": "b", "choices": [{"index": 0, "delta": {}, "finish_reason": "tool_calls"}]},
            ]
        else:
            eventos = [
                {"id": "b", "choices": [{"index": 0, "delta": {"content": "fim do teste de hook"}}]},
                {"id": "b", "choices": [{"index": 0, "delta": {}, "finish_reason": "stop"}]},
            ]
        for e in eventos:
            chunk(sse(e))
        chunk(b"data: [DONE]\n\n")
        self.wfile.write(b"0\r\n\r\n")
        self.wfile.flush()


print("mock bash na porta %d: %s" % (PORTA, COMANDO), flush=True)
HTTPServer(("127.0.0.1", PORTA), Handler).serve_forever()
