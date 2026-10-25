import http.server, socketserver, sys, urllib.parse

PORT = int(sys.argv[1])


class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def _html(self, corpo, cookie=None):
        dados = corpo.encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(dados)))
        if cookie:
            self.send_header("Set-Cookie", cookie)
        self.end_headers()
        self.wfile.write(dados)

    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        if u.path == "/set":
            self._html("<title>Login</title><p>bem-vindo</p><a href='/area'>area</a>",
                       cookie="sid=abc123; Path=/; HttpOnly")
        elif u.path == "/area":
            tem = "sid=abc123" in (self.headers.get("Cookie") or "")
            self._html("<title>Area</title><p>%s</p>" % ("sessao ok" if tem else "sem sessao"))
        elif u.path == "/latin1":
            corpo = ("<html><head><meta charset=\"iso-8859-1\"><title>Servico</title></head><body>"
                     "<nav><a href='/'>Home</a> <a href='/'>Menu</a></nav>"
                     "<h1>Serviço de Manutenção</h1>"
                     "<p>Esta página tem acentos: ação, coração, informação.</p>"
                     "<footer>&copy; 2026 Empresa. Todos os direitos reservados.</footer>"
                     "</body></html>").encode("latin-1")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=iso-8859-1")
            self.send_header("Content-Length", str(len(corpo)))
            self.end_headers()
            self.wfile.write(corpo)
        elif u.path == "/spa":
            self._html("""<html><head><title>Loja</title>
<script id="__NEXT_DATA__" type="application/json">{"props":{"pageProps":{"produto":{"nome":"Camisa Delta","preco":"R$ 99,90"},"vendedor":{"nome":"Delta Store"}}}}</script>
<script>window.__INITIAL_STATE__ = {"carrinho":{"total":"199,80"}};</script>
<script>fetch('/api/itens?page=2');</script>
</head><body><div id="root">Carregando</div></body></html>""")
        elif u.path == "/busca":
            q = urllib.parse.parse_qs(u.query).get("q", [""])[0]
            self._html("<title>Busca</title><p>resultado para: %s</p>" % q)
        elif u.path == "/form":
            self._html("<title>Form</title><form method='post' action='/post'><input name='x'></form>")
        else:
            self._html("<title>Home</title><p>home</p><a href='/set'>login</a>")

    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        corpo = self.rfile.read(n).decode(errors="replace")
        if self.path == "/post":
            self._html("<title>Post</title><p>recebi: %s</p>" % corpo)
        else:
            self._html("<title>Post</title><p>ok</p>")


socketserver.TCPServer.allow_reuse_address = True
with socketserver.TCPServer(("127.0.0.1", PORT), H) as srv:
    srv.serve_forever()
