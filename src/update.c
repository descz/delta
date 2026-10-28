/* update.c - "delta update": consulta os releases do GitHub, baixa o binario
 * da plataforma e troca o proprio executavel. Sem dependencias.
 *
 *   Windows: renomeia o exe em execucao (o Windows permite renomear, so' nao
 *            deixa apagar), poe o novo no lugar e apaga o .old no proximo
 *            inicio (update_init).
 *   Linux:   o release vem como .tar.gz; extrai com tar e troca com rename.
 *   macOS:   o release publica so' o codigo-fonte -> avisa e nao troca nada.
 *
 * DELTA_UPDATE_URL aponta a consulta para outro lugar (usado pelos testes com
 * um release falso local).
 */
#include "update.h"
#include "json.h"
#include "provider.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <unistd.h>
#  include <sys/stat.h>
#endif

#define UPDATE_MIN_BYTES 20000     /* abaixo disso o download esta truncado */

static char g_argv0[512] = "delta";

/* ================================================================== */
/* Caminhos                                                            */
/* ================================================================== */
static char *cache_path(void)
{
    char *base = delta_home();
    char *p = path_join(base, "update.json");
    free(base);
    return p;
}

/* Caminho absoluto do executavel em execucao. */
static char *self_path(void)
{
#ifdef _WIN32
    wchar_t w[2048];
    DWORD n = GetModuleFileNameW(NULL, w, (DWORD)(sizeof w / sizeof w[0]));
    if (n == 0 || n >= (DWORD)(sizeof w / sizeof w[0])) return str_dup(g_argv0);
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (len <= 0) return str_dup(g_argv0);
    char *out = (char *)malloc((size_t)len);
    if (!out) return str_dup(g_argv0);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out, len, NULL, NULL);
    return out;
#else
    char buf[2048];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n > 0) {
        buf[n] = 0;
        return str_dup(buf);
    }
    if (g_argv0[0] == '/') return str_dup(g_argv0);
    {
        char *r = realpath(g_argv0, NULL);
        if (r) return r;
    }
    return str_dup(g_argv0);
#endif
}

void update_init(const char *argv0)
{
    if (argv0 && *argv0) snprintf(g_argv0, sizeof g_argv0, "%s", argv0);
#ifdef _WIN32
    /* o exe em uso nao pode ser apagado: a troca deixa um .old para tras */
    {
        char *exe = self_path();
        Str velho; str_init(&velho);
        str_addf(&velho, "%s.old", exe);
        if (file_exists(velho.data)) DeleteFileA(velho.data);
        str_free(&velho);
        free(exe);
    }
#endif
}

/* ================================================================== */
/* Versao                                                              */
/* ================================================================== */
int version_cmp(const char *a, const char *b)
{
    const char *pa = a ? a : "";
    const char *pb = b ? b : "";
    if (*pa == 'v' || *pa == 'V') pa++;
    if (*pb == 'v' || *pb == 'V') pb++;

    for (int parte = 0; parte < 4; parte++) {
        long na = 0, nb = 0;
        while (*pa >= '0' && *pa <= '9') na = na * 10 + (*pa++ - '0');
        while (*pb >= '0' && *pb <= '9') nb = nb * 10 + (*pb++ - '0');
        if (na != nb) return na > nb ? 1 : -1;
        /* separador: ".1" continua; qualquer sufixo ("-beta", "+build") encerra */
        if (*pa == '.') pa++;
        else if (*pa) pa = "";
        if (*pb == '.') pb++;
        else if (*pb) pb = "";
    }
    return 0;
}

/* ================================================================== */
/* Cache (~/.deltaCore/update.json)                                    */
/* ================================================================== */
static void copia(char *dst, size_t n, const char *src)
{
    if (!dst || n == 0) return;
    snprintf(dst, n, "%s", src ? src : "");
}

static void cache_save(const UpdateInfo *u, long long checked)
{
    Str o; str_init(&o);
    str_addf(&o, "{\"checked\":%lld,\"latest\":", checked);
    json_write_string(&o, u->latest, strlen(u->latest));
    str_adds(&o, ",\"asset\":");
    json_write_string(&o, u->asset, strlen(u->asset));
    str_adds(&o, ",\"url\":");
    json_write_string(&o, u->url, strlen(u->url));
    str_adds(&o, ",\"page\":");
    json_write_string(&o, u->page, strlen(u->page));
    str_adds(&o, "}\n");
    char *p = cache_path();
    file_write(p, o.data, o.len);
    free(p);
    str_free(&o);
}

/* Le o cache. Nao mexe em current/available. */
static int cache_load(UpdateInfo *out, long long *checked)
{
    if (checked) *checked = 0;
    char *p = cache_path();
    size_t len = 0;
    char *txt = file_read(p, &len);
    free(p);
    if (!txt) return 0;
    JValue *v = json_parse(txt, len);
    free(txt);
    if (!v) return 0;
    copia(out->latest, sizeof out->latest, json_str(json_get(v, "latest")));
    copia(out->asset, sizeof out->asset, json_str(json_get(v, "asset")));
    copia(out->url, sizeof out->url, json_str(json_get(v, "url")));
    {
        const char *pagina = json_str(json_get(v, "page"));
        if (*pagina) copia(out->page, sizeof out->page, pagina);
    }
    if (checked) *checked = (long long)json_num(json_get(v, "checked"), 0);
    json_free(v);
    return out->latest[0] != 0;
}

int update_cached(UpdateInfo *out)
{
    if (!out) return 0;
    memset(out, 0, sizeof *out);
    copia(out->current, sizeof out->current, DELTA_VERSION);
    copia(out->page, sizeof out->page, DELTA_RELEASES_PAGE);
    int tinha = cache_load(out, NULL);
    out->available = tinha && version_cmp(out->latest, out->current) > 0;
    return tinha;
}

/* ================================================================== */
/* Release do GitHub                                                   */
/* ================================================================== */
/* Escolhe o asset que serve nesta plataforma (duas passadas: nome exato e
   depois qualquer coisa aceitavel). */
static void escolher_asset(JValue *assets, UpdateInfo *out)
{
    if (!assets || assets->kind != J_ARR) return;

    for (int passada = 0; passada < 2; passada++) {
        for (size_t i = 0; i < json_count(assets); i++) {
            JValue *a = json_at(assets, i);
            const char *nome = json_str(json_get(a, "name"));
            const char *url = json_str(json_get(a, "browser_download_url"));
            if (!*nome || !*url) continue;
            int serve = 0;
#ifdef _WIN32
            serve = passada == 0 ? str_ieq(nome, "Delta.exe")
                                 : str_ends(nome, ".exe");
#elif defined(__APPLE__)
            (void)nome;
            (void)url;
            serve = 0;      /* o release do macOS e' so' o codigo-fonte */
#else
            serve = passada == 0
                        ? (str_istarts(nome, "delta-linux") && str_ends(nome, ".tar.gz"))
                        : (str_find(nome, "linux") && str_ends(nome, ".tar.gz"));
#endif
            if (!serve) continue;
            copia(out->asset, sizeof out->asset, nome);
            copia(out->url, sizeof out->url, url);
            return;
        }
    }
}

int update_check(UpdateInfo *out, int cache_ok)
{
    if (!out) return 0;
    memset(out, 0, sizeof *out);
    copia(out->current, sizeof out->current, DELTA_VERSION);
    copia(out->page, sizeof out->page, DELTA_RELEASES_PAGE);

    long long checked = 0;
    int tinha = cache_load(out, &checked);
    time_t agora = time(NULL);

    if (cache_ok && tinha && checked > 0 &&
        (long long)agora - checked < (long long)UPDATE_TTL_SECONDS) {
        out->available = version_cmp(out->latest, out->current) > 0;
        return 1;
    }

    const char *url = getenv("DELTA_UPDATE_URL");
    if (!url || !*url) url = DELTA_RELEASES_API;

    Str corpo; str_init(&corpo);
    Str erro; str_init(&erro);
    int ok = http_get_text_to(url, &corpo, &erro, 8);
    if (!ok) {
        copia(out->err, sizeof out->err, erro.len ? erro.data : "falha na consulta");
        str_free(&corpo);
        str_free(&erro);
        /* sem rede o cache antigo ainda vale como aviso; grava o carimbo para
           nao tentar de novo a cada inicializacao */
        if (tinha) out->available = version_cmp(out->latest, out->current) > 0;
        cache_save(out, (long long)agora);
        return 0;
    }
    str_free(&erro);

    JValue *v = json_parse_cstr(corpo.data ? corpo.data : "");
    str_free(&corpo);
    if (!v) {
        copia(out->err, sizeof out->err, "resposta invalida do GitHub");
        return 0;
    }

    const char *tag = json_str(json_get(v, "tag_name"));
    const char *pagina = json_str(json_get(v, "html_url"));
    if (!*tag) {
        copia(out->err, sizeof out->err, "release sem tag_name");
        json_free(v);
        return 0;
    }
    if (*pagina) copia(out->page, sizeof out->page, pagina);
    if (*tag == 'v' || *tag == 'V') tag++;
    copia(out->latest, sizeof out->latest, tag);
    escolher_asset(json_get(v, "assets"), out);
    json_free(v);

    out->available = version_cmp(out->latest, out->current) > 0;
    out->err[0] = 0;
    cache_save(out, (long long)agora);
    return 1;
}

char *update_notice(int allow_network)
{
    UpdateInfo u;
    if (allow_network) update_check(&u, 1);
    else               update_cached(&u);
    if (!u.available || !u.latest[0]) return NULL;

    Str s; str_init(&s);
    str_addf(&s, "nova versao %s disponivel (voce esta na %s) \u00b7 use /update ou `delta update`",
             u.latest, u.current);
    return str_steal(&s);
}

/* ================================================================== */
/* Troca do executavel                                                 */
/* ================================================================== */
#ifdef _WIN32
static int trocar_exe(const char *exe, const char *novo, Str *msg)
{
    Str velho; str_init(&velho);
    str_addf(&velho, "%s.old", exe);

    if (file_exists(velho.data)) DeleteFileA(velho.data);

    /* 1. tira o exe em uso do caminho (renomear pode, apagar nao) */
    if (!MoveFileExA(exe, velho.data, MOVEFILE_REPLACE_EXISTING)) {
        str_addf(msg, "nao consegui renomear o executavel em uso (erro %lu)",
                 (unsigned long)GetLastError());
        str_free(&velho);
        return 0;
    }
    /* 2. poe o novo no lugar */
    if (!MoveFileExA(novo, exe, MOVEFILE_REPLACE_EXISTING)) {
        MoveFileExA(velho.data, exe, MOVEFILE_REPLACE_EXISTING);   /* volta atras */
        str_addf(msg, "nao consegui gravar o novo executavel (erro %lu)",
                 (unsigned long)GetLastError());
        str_free(&velho);
        return 0;
    }
    /* 3. se o .old nao sair agora (em uso), update_init limpa no proximo inicio */
    DeleteFileA(velho.data);
    str_free(&velho);
    return 1;
}
#else
/* Procura o binario delta dentro do pacote extraido. */
static char *acha_binario(const char *dir)
{
    static const char *const rel[] = { "delta", "Delta", "bin/delta", "bin/Delta", NULL };
    for (int i = 0; rel[i]; i++) {
        char *p = path_join(dir, rel[i]);
        if (file_exists(p) && !path_is_dir(p)) return p;
        free(p);
    }
    /* um nivel abaixo (delta-linux-x86_64/delta) */
    size_t n = 0;
    char **subs = dir_list(dir, &n);
    char *achado = NULL;
    for (size_t i = 0; i < n && !achado; i++) {
        char *sub = path_join(dir, subs[i]);
        if (path_is_dir(sub)) {
            char *p = path_join(sub, "delta");
            if (file_exists(p) && !path_is_dir(p)) achado = p;
            else {
                free(p);
                p = path_join(sub, "Delta");
                if (file_exists(p) && !path_is_dir(p)) achado = p;
                else free(p);
            }
        }
        free(sub);
    }
    dir_list_free(subs, n);
    return achado;
}

static int trocar_exe(const char *exe, const char *novo, Str *msg)
{
    size_t len = 0;
    char *dados = file_read(novo, &len);
    if (!dados) { str_adds(msg, "nao consegui ler o binario baixado"); return 0; }

    Str dest; str_init(&dest);
    str_addf(&dest, "%s.new", exe);
    int ok = file_write(dest.data, dados, len);
    free(dados);
    if (!ok) {
        str_addf(msg, "sem permissao para gravar em %s", dest.data);
        str_free(&dest);
        return 0;
    }
    chmod(dest.data, 0755);
    if (rename(dest.data, exe) != 0) {
        str_addf(msg, "nao consegui trocar o executavel em %s", exe);
        remove(dest.data);
        str_free(&dest);
        return 0;
    }
    str_free(&dest);
    return 1;
}
#endif

int update_apply(const UpdateInfo *info, Str *msg)
{
    if (!info) return 0;
    if (!msg) return 0;
    str_clear(msg);

    if (!info->available) {
        str_adds(msg, "ja esta na versao mais nova.");
        return 0;
    }
    if (!info->url[0]) {
        str_addf(msg, "o release %s nao publica binario para esta plataforma; baixe em %s",
                 info->latest, info->page);
        return 0;
    }

    Str corpo; str_init(&corpo);
    Str erro; str_init(&erro);
    if (!http_get_text_to(info->url, &corpo, &erro, 300)) {
        str_addf(msg, "download falhou: %s", erro.len ? erro.data : "sem conexao");
        str_free(&corpo);
        str_free(&erro);
        return 0;
    }
    str_free(&erro);

    if (corpo.len < UPDATE_MIN_BYTES) {
        str_addf(msg, "download incompleto (%lu bytes)", (unsigned long)corpo.len);
        str_free(&corpo);
        return 0;
    }

    char *exe = self_path();
    char *novo = NULL;
    int ok = 0;

#ifdef _WIN32
    /* sanity check: tem que ser um PE ("MZ") */
    if (corpo.data[0] != 'M' || corpo.data[1] != 'Z') {
        str_adds(msg, "o arquivo baixado nao parece um executavel do Windows");
        goto fim;
    }
    novo = (char *)malloc(strlen(exe) + 5);
    if (!novo) { str_adds(msg, "sem memoria"); goto fim; }
    sprintf(novo, "%s.new", exe);
    if (!file_write(novo, corpo.data, corpo.len)) {
        str_addf(msg, "sem permissao para gravar em %s (rode como administrador)", exe);
        goto fim;
    }
    ok = trocar_exe(exe, novo, msg);
    if (!ok) remove(novo);
#else
    /* o release do Linux vem como .tar.gz */
    if ((unsigned char)corpo.data[0] != 0x1f || (unsigned char)corpo.data[1] != 0x8b) {
        str_adds(msg, "o pacote baixado nao e' um .tar.gz");
        goto fim;
    }
    {
        Str tmp; str_init(&tmp);
        str_addf(&tmp, "/tmp/delta-upd-%lu", (unsigned long)getpid());
        dir_make(tmp.data);
        char *pkg = path_join(tmp.data, "pkg.tar.gz");
        if (!file_write(pkg, corpo.data, corpo.len)) {
            str_adds(msg, "nao consegui gravar o pacote em /tmp");
            free(pkg);
            str_free(&tmp);
            goto fim;
        }
        Str cmd; str_init(&cmd);
        str_addf(&cmd, "tar -xzf '%s' -C '%s' 2>/dev/null", pkg, tmp.data);
        int rc = system(cmd.data);
        str_free(&cmd);
        if (rc != 0) {
            str_adds(msg, "tar falhou ao abrir o pacote");
            remove(pkg);
            free(pkg);
            str_free(&tmp);
            goto fim;
        }
        novo = acha_binario(tmp.data);
        if (!novo) {
            str_adds(msg, "o pacote nao tem o binario delta");
            remove(pkg);
            free(pkg);
            str_free(&tmp);
            goto fim;
        }
        chmod(novo, 0755);
        ok = trocar_exe(exe, novo, msg);
        remove(novo);
        remove(pkg);
        free(novo);
        novo = NULL;
        str_free(&tmp);
    }
#endif

fim:
    free(novo);
    free(exe);
    str_free(&corpo);

    if (ok) {
        str_clear(msg);
        str_addf(msg, "atualizado para %s (reinicie o delta para usar a versao nova)",
                 info->latest);
    } else if (msg->len == 0) {
        str_adds(msg, "nao consegui atualizar");
    }
    return ok;
}

/* ================================================================== */
/* CLI: delta update [--check]                                         */
/* ================================================================== */
int update_cli(int argc, char **argv)
{
    int so_checar = 0;
    for (int i = 0; i < argc; i++) {
        if (!argv[i]) continue;
        if (str_eq(argv[i], "--check") || str_eq(argv[i], "-c")) so_checar = 1;
        else if (str_eq(argv[i], "--help") || str_eq(argv[i], "-h")) {
            printf("uso: delta update [--check]\n\n"
                   "  consulta o release mais novo em github.com/" DELTA_REPO " e troca este\n"
                   "  executavel pela versao baixada (reinicie o delta depois).\n"
                   "  --check                   so' mostra se ha' versao nova\n"
                   "  DELTA_NO_UPDATE_CHECK=1   desliga o aviso automatico na TUI\n"
                   "  updateCheck:false         o mesmo, no config.json\n");
            return 0;
        }
    }

    printf("Delta Agent %s - procurando versoes novas...\n", DELTA_VERSION);
    fflush(stdout);

    UpdateInfo u;
    int ok = update_check(&u, 0);
    if (!ok) {
        if (u.available)
            printf("aviso: sem conexao agora, mas a ultima consulta viu a versao %s\n", u.latest);
        fprintf(stderr, "delta update: %s\n", u.err[0] ? u.err : "nao consegui consultar os releases");
        return 1;
    }
    if (!u.available) {
        printf("ja' esta na versao mais nova (%s).\n", u.current);
        return 0;
    }

    printf("nova versao: %s (a sua: %s)\n", u.latest, u.current);
    if (so_checar) {
        printf("pagina do release: %s\n", u.page);
        return 0;
    }
    if (u.url[0]) printf("baixando %s...\n", u.asset);

    Str msg; str_init(&msg);
    int trocou = update_apply(&u, &msg);
    if (msg.len) printf("%s\n", msg.data);
    if (!trocou && u.page[0]) printf("pagina do release: %s\n", u.page);
    str_free(&msg);
    return trocou ? 0 : 1;
}
