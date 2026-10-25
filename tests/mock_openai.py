#!/usr/bin/env python3
"""Servidor falso compativel com OpenAI, para testar o Delta de ponta a ponta.

Decide pelo historico enviado, sem estado:
  - ultima mensagem do usuario contem "erro400"  -> HTTP 400 (estilo opencode-go)
  - ainda nao ha resultado de ferramenta          -> tool_call de `read` em streaming
  - caso contrario                                -> texto em streaming
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8099
DELAY = float(sys.argv[2]) if len(sys.argv) > 2 else 0.0
VISTOS = {}   # contagem por texto (teste de retry)
CHUNK = float(sys.argv[3]) if len(sys.argv) > 3 else 0.0   # atraso entre chunks do stream


def sse(obj):
    return ("data: " + json.dumps(obj) + "\n\n").encode()


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        sys.stderr.write("[mock] " + (args[0] % args[1:]) + "\n")

    # ---- respostas -------------------------------------------------
    def responder_erro(self, status, corpo):
        dados = json.dumps(corpo).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(dados)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(dados)
        self.wfile.flush()
        self.close_connection = True

    def responder_eventos(self, eventos):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Transfer-Encoding", "chunked")
        self.end_headers()

        def chunk(data):
            self.wfile.write(b"%x\r\n" % len(data) + data + b"\r\n")
            self.wfile.flush()

        for ev in eventos:
            if CHUNK:
                import time
                time.sleep(CHUNK)
            chunk(sse(ev))
        chunk(b"data: [DONE]\n\n")
        self.wfile.write(b"0\r\n\r\n")
        self.wfile.flush()

    # ---- POST ------------------------------------------------------
    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        body = json.loads(self.rfile.read(length) or b"{}")

        with open("build/mock-last-request.json", "w", encoding="utf-8") as f:
            json.dump(body, f, indent=2, ensure_ascii=False)
        with open("build/mock-headers.json", "w", encoding="utf-8") as f:
            json.dump(dict(self.headers), f, indent=2)

        auth = self.headers.get("Authorization", "")
        assert auth.startswith("Bearer "), "faltou o header Authorization"

        if DELAY:
            import time
            time.sleep(DELAY)

        ultimo = ""
        for m in reversed(body.get("messages", [])):
            if m.get("role") == "user" and isinstance(m.get("content"), str):
                ultimo = m["content"]
                break

        if "retry429" in ultimo or "erro429" in ultimo:
            VISTOS[ultimo] = VISTOS.get(ultimo, 0) + 1
            if "erro429" in ultimo or VISTOS[ultimo] == 1:
                self.responder_erro(429, {"type": "error", "error": {
                    "type": "server_error",
                    "message": "Upstream request failed: Endpoint is unavailable."}})
                return

        if "erro400" in ultimo:
            self.responder_erro(400, {"type": "error", "error": {
                "type": "MissingSessionID",
                "message": "Request is missing x-opencode-session and cannot be routed efficiently. "
                           "Please see https://opencode.ai/docs/go/#where-can-i-use-it"}})
            return

        tem_resultado = any(m.get("role") == "tool" for m in body.get("messages", []))

        if "acentos" in ultimo and not tem_resultado:
            # pede um comando cuja saida tem acentos (pagina OEM no Windows)
            self.responder_eventos([
                {"id": "1", "choices": [{"index": 0, "delta": {"tool_calls": [
                    {"index": 0, "id": "call_b", "type": "function",
                     "function": {"name": "bash", "arguments": '{"command": "dir build\\acentos"}'}}]}}]},
                {"id": "1", "choices": [{"index": 0, "delta": {}, "finish_reason": "tool_calls"}]},
            ])
            return

        if "cria-extensao" in ultimo and not tem_resultado:
            spec = ('{"kind":"command","name":"teste-criado",'
                    '"desc":"criado pelo modelo","arg":"Diga ola ao chefe"}')
            self.responder_eventos([
                {"id": "e", "choices": [{"index": 0, "delta": {"content": spec}}]},
                {"id": "e", "choices": [{"index": 0, "delta": {}, "finish_reason": "stop"}]},
            ])
            return

        if "paragrafos-teste" in ultimo and not tem_resultado:
            self.responder_eventos([
                {"id": "p", "choices": [{"index": 0, "delta": {"content": "Primeiro paragrafo.\n\nSegundo paragrafo.\n"}}]},
                {"id": "p", "choices": [{"index": 0, "delta": {}, "finish_reason": "stop"}]},
            ])
            return

        if "subagente-teste" in ultimo and not tem_resultado:
            self.responder_eventos([
                {"id": "s", "choices": [{"index": 0, "delta": {"content": "subagente ok"}}]},
                {"id": "s", "choices": [{"index": 0, "delta": {}, "finish_reason": "stop"}]},
            ])
            return

        if not tem_resultado:
            self.responder_eventos([
                {"id": "1", "choices": [{"index": 0, "delta": {"role": "assistant", "tool_calls": [
                    {"index": 0, "id": "call_1", "type": "function",
                     "function": {"name": "read", "arguments": ""}}]}}]},
                {"id": "1", "choices": [{"index": 0, "delta": {"tool_calls": [
                    {"index": 0, "function": {"arguments": '{"path":'}}]}}]},
                {"id": "1", "choices": [{"index": 0, "delta": {"tool_calls": [
                    {"index": 0, "function": {"arguments": '"tests/mock-target.txt"}'}}]}}]},
                {"id": "1", "choices": [{"index": 0, "delta": {}, "finish_reason": "tool_calls"}]},
            ])
            return

        # ecoa a mensagem do usuario: prova que ela chegou ao "modelo"
        eventos = []
        for piece in ["O arquivo contem: ", "**42**", " e o *caminho* lido foi ",
                      "`tests/mock-target.txt`.", "\n\n```c\n", "int resposta = 42;\n",
                      "```\n", "\n\n(eco: " + ultimo + ")\n"]:
            eventos.append({"id": "2", "choices": [{"index": 0, "delta": {"content": piece}}]})
        eventos.append({"id": "2", "choices": [{"index": 0, "delta": {}, "finish_reason": "stop"}]})
        eventos.append({"id": "2", "choices": [], "usage": {
            "prompt_tokens": 1234, "completion_tokens": 56,
            "prompt_tokens_details": {"cached_tokens": 1024}}})
        self.responder_eventos(eventos)


if __name__ == "__main__":
    HTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
