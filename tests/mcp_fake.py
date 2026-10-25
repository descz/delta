#!/usr/bin/env python3
"""Servidor MCP falso (JSON-RPC sobre HTTP) para os testes do Delta."""
import json
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8600
SESSAO = "sessao-teste-123"


class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def _responde(self, obj, sessao=False):
        dados = json.dumps(obj).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(dados)))
        if sessao:
            self.send_header("Mcp-Session-Id", SESSAO)
        self.end_headers()
        self.wfile.write(dados)

    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        req = json.loads(self.rfile.read(n) or b"{}")
        metodo = req.get("method", "")
        rid = req.get("id")

        if metodo == "initialize":
            self._responde({"jsonrpc": "2.0", "id": rid, "result": {
                "protocolVersion": "2024-11-05",
                "capabilities": {"tools": {}},
                "serverInfo": {"name": "fake-mcp", "version": "1.0"}}}, sessao=True)
        elif metodo == "notifications/initialized":
            self._responde({"jsonrpc": "2.0", "id": None, "result": {}})
        elif metodo == "tools/list":
            ok = self.headers.get("Mcp-Session-Id") == SESSAO
            self._responde({"jsonrpc": "2.0", "id": rid, "result": {"tools": [
                {"name": "echo", "description": "devolve o texto recebido"},
                {"name": "soma", "description": "soma dois numeros"},
            ]} if ok else {"tools": []}})
        elif metodo == "tools/call":
            nome = req.get("params", {}).get("name")
            args = req.get("params", {}).get("arguments", {})
            if nome == "echo":
                texto = "ECHO: " + str(args.get("texto", ""))
            elif nome == "soma":
                texto = "SOMA: %s" % (int(args.get("a", 0)) + int(args.get("b", 0)))
            else:
                self._responde({"jsonrpc": "2.0", "id": rid,
                                "error": {"message": "tool desconhecida: %s" % nome}})
                return
            self._responde({"jsonrpc": "2.0", "id": rid, "result": {
                "content": [{"type": "text", "text": texto}]}})
        else:
            self._responde({"jsonrpc": "2.0", "id": rid,
                            "error": {"message": "metodo desconhecido: %s" % metodo}})


HTTPServer.allow_reuse_address = True
HTTPServer(("127.0.0.1", PORT), H).serve_forever()
