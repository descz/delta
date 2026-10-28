#!/usr/bin/env python3
"""release falso para testar o `delta update` sem tocar no GitHub.

    python tests/update_fake.py <porta> <tag> <nome-do-asset> <arquivo>

GET /latest    -> JSON no formato da API (api.github.com/repos/.../releases/latest)
GET /redirect  -> 302 para /asset (o GitHub faz isso de verdade)
GET /asset     -> o binario/pacote
"""
import http.server
import json
import socketserver
import sys

PORT = int(sys.argv[1])
TAG = sys.argv[2]
ASSET_NAME = sys.argv[3]
ASSET_PATH = sys.argv[4]
BASE = "http://127.0.0.1:%d" % PORT


class H(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def _send(self, status, ctype, dados):
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(dados)))
        self.end_headers()
        self.wfile.write(dados)

    def do_GET(self):
        if self.path == "/latest":
            corpo = {
                "tag_name": TAG,
                "name": "Delta Agent " + TAG.lstrip("v"),
                "html_url": BASE + "/release",
                "assets": [
                    {"name": ASSET_NAME, "browser_download_url": BASE + "/redirect"},
                ],
            }
            self._send(200, "application/json", json.dumps(corpo).encode())
        elif self.path == "/redirect":
            self.send_response(302)
            self.send_header("Location", BASE + "/asset")
            self.send_header("Content-Length", "0")
            self.end_headers()
        elif self.path == "/asset":
            with open(ASSET_PATH, "rb") as f:
                dados = f.read()
            self._send(200, "application/octet-stream", dados)
        elif self.path == "/release":
            self._send(200, "text/html", b"<title>release falso</title>")
        else:
            self._send(404, "text/plain", b"nao encontrado")


socketserver.TCPServer.allow_reuse_address = True
with socketserver.TCPServer(("127.0.0.1", PORT), H) as srv:
    srv.serve_forever()
