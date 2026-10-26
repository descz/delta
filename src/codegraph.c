/* codegraph.c - "grafo" do projeto: lê o código UMA vez, guarda símbolos e
 * imports, e responde perguntas depois sem reler tudo.
 *
 * Formato em disco (texto, uma linha por registro) em <core>/graphs/<slug>.graph:
 *
 *   DELTA-GRAPH 1
 *   root <caminho>
 *   built <iso>
 *   file <hash> <bytes> <caminho>
 *   sym <kind> <arquivo> <linha> <nome>
 *   imp <arquivo> <importado>
 *
 * kind: f=função, t=tipo, v=variável/global, m=macro, i=import/include
 */
#include "codegraph.h"
#include "util.h"
#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_FILES   4000
#define MAX_SYMS    40000
#define MAX_IMPS    20000
#define MAX_FILE_KB 512

typedef struct { char *path; uint64_t hash; long size; } GFile;
typedef struct { char *name; int file; int line; char kind; } GSym;
typedef struct { int file; char *target; } GImp;

struct CodeGraph {
    char    *root;
    char    *slug;
    GFile   *files; size_t n_files;
    GSym    *syms;  size_t n_syms;
    GImp    *imps;  size_t n_imps;
    int      dirty;
};

/* ================================================================== */
/* utilidades                                                          */
/* ================================================================== */
static uint64_t fnv64(const char *s, size_t n)
{
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) { h ^= (unsigned char)s[i]; h *= 1099511628211ULL; }
    return h;
}

static void slugify(const char *in, char *out, size_t n)
{
    size_t k = 0;
    for (const char *p = in; *p && k + 1 < n; p++) {
        char c = *p;
        if (c == '/' || c == '\\' || c == ':') c = '-';
        if (c == ' ') c = '_';
        out[k++] = c;
    }
    out[k] = 0;
    if (!k) snprintf(out, n, "root");
}

static int skip_dir(const char *name)
{
    static const char *skip[] = {
        ".git", ".hg", ".svn", "node_modules", "build", "bin", "dist", ".deltaCore",
        ".posix-tests", "venv", ".venv", "__pycache__", "target", "out", ".cache",
        "coverage", ".next", ".nuxt", ".idea", ".vscode", "obj", "vendor", NULL
    };
    for (int i = 0; skip[i]; i++) if (str_eq(name, skip[i])) return 1;
    return 0;
}

static int is_code_ext(const char *path)
{
    static const char *ok[] = {
        ".c", ".h", ".cc", ".cpp", ".hpp", ".cxx", ".cs", ".java", ".kt", ".rs",
        ".go", ".py", ".rb", ".php", ".js", ".mjs", ".cjs", ".ts", ".tsx", ".jsx",
        ".lua", ".swift", ".zig", ".ex", ".exs", ".clj", ".scala", ".dart", ".vue",
        ".svelte", NULL
    };
    size_t n = strlen(path);
    for (int i = 0; ok[i]; i++) {
        size_t l = strlen(ok[i]);
        if (n > l && str_ieq(path + n - l, ok[i])) return 1;
    }
    return 0;
}

static int is_text_ext(const char *path)
{
    static const char *ok[] = {
        ".c", ".h", ".cc", ".cpp", ".hpp", ".cs", ".java", ".kt", ".rs", ".go",
        ".py", ".rb", ".php", ".js", ".mjs", ".cjs", ".ts", ".tsx", ".jsx",
        ".lua", ".sh", ".bash", ".zsh", ".ps1", ".bat", ".cmd", ".sql",
        ".json", ".yaml", ".yml", ".toml", ".ini", ".md", ".txt", ".css", ".scss",
        ".html", ".htm", ".xml", ".proto", ".graphql", ".swift", ".m", ".mm",
        ".zig", ".hs", ".ex", ".exs", ".clj", ".scala", ".dart", ".vue", ".svelte",
        NULL
    };
    size_t n = strlen(path);
    for (int i = 0; ok[i]; i++) {
        size_t l = strlen(ok[i]);
        if (n > l && str_ieq(path + n - l, ok[i])) return 1;
    }
    return 0;
}

/* ================================================================== */
/* extração de símbolos                                                */
/* ================================================================== */
static int is_ident(int c) { return isalnum(c) || c == '_'; }

static int graph_icontains(const char *hay, const char *needle)
{
    if (!needle || !*needle) return 1;
    size_t nl = strlen(needle);
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < nl && p[i]) {
            char a = p[i], b = needle[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
            i++;
        }
        if (i == nl) return 1;
    }
    return 0;
}

/* Pega o identificador que vem antes de '(' a partir de `p`. */
static int ident_before(const char *s, size_t i, char *out, size_t n)
{
    size_t e = i;
    while (e > 0 && s[e - 1] == ' ') e--;
    size_t b = e;
    while (b > 0 && is_ident((unsigned char)s[b - 1])) b--;
    size_t l = e - b;
    if (l == 0 || l >= n) return 0;
    memcpy(out, s + b, l);
    out[l] = 0;
    /* descarta palavras que não são nomes */
    static const char *ruim[] = { "if", "for", "while", "switch", "return", "sizeof",
                                  "catch", "elif", "else", "do", "case", "defined",
                                  "memset", "memcpy", "memmove", "strcmp", "strncmp", "strlen",
                                  "printf", "fprintf", "snprintf", "sprintf", "sscanf", "scanf",
                                  "malloc", "calloc", "realloc", "free", "exit", "abort",
                                  "strstr", "strchr", "strrchr", "strcpy", "strncpy", "strcat",
                                  "fopen", "fclose", "fread", "fwrite", "fgets", "fputs",
                                  "atoi", "atof", "assert", "sizeof", "NULL", "main", NULL };
    for (int k = 0; ruim[k]; k++) if (str_eq(out, ruim[k])) return 0;
    return 1;
}

static void add_sym(CodeGraph *g, int file, int line, char kind, const char *name)
{
    if (g->n_syms >= MAX_SYMS || !name || !*name) return;
    /* dedup por (arquivo, nome, kind): mantém a primeira ocorrência */
    for (size_t i = g->n_syms; i-- > 0;) {
        if (g->syms[i].file == file) {
            if (g->syms[i].kind == kind && str_eq(g->syms[i].name, name)) return;
        } else if (g->syms[i].file < file) {
            break;
        }
    }
    g->syms[g->n_syms].name = str_dup(name);
    g->syms[g->n_syms].file = file;
    g->syms[g->n_syms].line = line;
    g->syms[g->n_syms].kind = kind;
    g->n_syms++;
    g->dirty = 1;
}

static void add_imp(CodeGraph *g, int file, const char *target)
{
    if (g->n_imps >= MAX_IMPS || !target || !*target) return;
    g->imps[g->n_imps].file = file;
    g->imps[g->n_imps].target = str_dup(target);
    g->n_imps++;
    g->dirty = 1;
}

/* Extrai símbolos de uma linha. Heurística, sem parser de verdade. */
static void scan_line(CodeGraph *g, int file, int lineno, const char *s, size_t len)
{
    if (!is_code_ext(g->files[file].path)) { (void)lineno; (void)len; return; }
    /* comentários de linha inteira */
    const char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '/' && (p[1] == '/' || p[1] == '*')) return;
    if (*p == '*' ) return;                 /* continuação de bloco */
    if (*p == '#') {
        /* #include "x"  |  #include <x>  |  #define NOME */
        if (str_istarts(p, "#include")) {
            const char *q = strchr(p, '"');
            const char *r = strchr(p, '<');
            if (q) {
                const char *e = strchr(q + 1, '"');
                if (e) { char t[256]; size_t l = (size_t)(e - q - 1); if (l < sizeof t) { memcpy(t, q + 1, l); t[l] = 0; add_imp(g, file, t); } }
            } else if (r) {
                const char *e = strchr(r + 1, '>');
                if (e) { char t[256]; size_t l = (size_t)(e - r - 1); if (l < sizeof t) { memcpy(t, r + 1, l); t[l] = 0; add_imp(g, file, t); } }
            }
        } else if (str_istarts(p, "#define ")) {
            char name[128]; int k = 0;
            const char *q = p + 8;
            while (*q == ' ') q++;
            while (*q && (is_ident((unsigned char)*q)) && k < 127) name[k++] = *q++;
            name[k] = 0;
            if (k) add_sym(g, file, lineno, 'm', name);
        }
        return;
    }

    /* import/use/from (JS/TS/Python/Rust/Go) */
    if (str_istarts(p, "import ") || str_istarts(p, "from ") || str_istarts(p, "use ")) {
        const char *q = strchr(p, '\'');
        if (!q) q = strchr(p, '"');
        if (q) {
            const char *e = strchr(q + 1, *q);
            if (e) { char t[256]; size_t l = (size_t)(e - q - 1); if (l < sizeof t) { memcpy(t, q + 1, l); t[l] = 0; add_imp(g, file, t); } }
        }
    }
    /* def / class / func / fn / struct / enum / type / interface */
    {
        static const struct { const char *kw; char kind; } K[] = {
            { "def ", 'f' }, { "class ", 't' }, { "func ", 'f' }, { "fn ", 'f' },
            { "struct ", 't' }, { "enum ", 't' }, { "union ", 't' }, { "interface ", 't' },
            { "type ", 't' }, { "typedef ", 't' }, { "function ", 'f' },
            { NULL, 0 }
        };
        for (int i = 0; K[i].kw; i++) {
            size_t kl = strlen(K[i].kw);
            if (str_istarts(p, K[i].kw)) {
                const char *q = p + kl;
                while (*q == ' ') q++;
                /* nome: identificador (para "type X struct" pega X) */
                char name[128]; int k = 0;
                while (*q && is_ident((unsigned char)*q) && k < 127) name[k++] = *q++;
                name[k] = 0;
                /* função/método pode ter prefixo de tipo: "char *foo(" */
                if (k && (K[i].kind == 't' || K[i].kind == 'v')) add_sym(g, file, lineno, K[i].kind, name);
                break;
            }
        }
    }
    /* chamada/definição: "tipo nome(" em coluna 0 (definição de topo) */
    if (p == s && is_code_ext(g->files[file].path)) {
        const char *paren = strchr(p, '(');
        if (paren && paren > p) {
            /* ignora linhas que terminam com ';' depois do ')' (protótipo conta) */
            char name[128];
            if (ident_before(p, (size_t)(paren - p), name, sizeof name)) {
                /* só conta se a linha parece declaração: tem tipo antes e { ou ; depois */
                const char *close = strchr(paren, ')');
                int decl = 0;
                if (close) {
                    const char *t = close + 1;
                    while (*t == ' ') t++;
                    if (*t == '{' || *t == ';' || *t == '\0') decl = 1;
                }
                if (decl && strchr(p, '*') == NULL ? 1 : 1) {
                    /* evita pegar atribuições/macros comuns */
                    if (!str_istarts(p, "return") && !str_istarts(p, "else"))
                        add_sym(g, file, lineno, 'f', name);
                }
            }
        }
    }
    (void)len;
}

/* ================================================================== */
/* build                                                               */
/* ================================================================== */
static void add_file_rec(CodeGraph *g, const char *root, const char *rel, int depth)
{
    if (g->n_files >= MAX_FILES || depth > 12) return;
    char *dir = rel && *rel ? path_join(root, rel) : str_dup(root);
    size_t n = 0;
    char **names = dir_list(dir, &n);
    for (size_t i = 0; names && i < n; i++) {
        const char *nm = names[i];
        if (nm[0] == '.') {
            if (!str_eq(nm, ".env")) continue;
        }
        char *r = (rel && *rel) ? path_join(rel, nm) : str_dup(nm);
        char *full = path_join(root, r);
        if (path_is_dir(full)) {
            if (!skip_dir(nm)) add_file_rec(g, root, r, depth + 1);
        } else if (is_text_ext(r)) {
            size_t len = 0;
            char *txt = file_read(full, &len);
            if (txt && len <= (size_t)MAX_FILE_KB * 1024) {
                int idx = (int)g->n_files;
                g->files[g->n_files].path = str_dup(r);
                g->files[g->n_files].hash = fnv64(txt, len);
                g->files[g->n_files].size = (long)len;
                g->n_files++;
                /* linhas */
                int line = 1;
                size_t pos = 0;
                while (pos < len) {
                    size_t eol = pos;
                    while (eol < len && txt[eol] != '\n') eol++;
                    scan_line(g, idx, line, txt + pos, eol - pos);
                    pos = eol + 1;
                    line++;
                }
            }
            free(txt);
        }
        free(full);
        free(r);
    }
    if (names) dir_list_free(names, n);
    free(dir);
}

void graph_free(CodeGraph *g)
{
    if (!g) return;
    for (size_t i = 0; i < g->n_files; i++) free(g->files[i].path);
    for (size_t i = 0; i < g->n_syms; i++) free(g->syms[i].name);
    for (size_t i = 0; i < g->n_imps; i++) free(g->imps[i].target);
    free(g->files); free(g->syms); free(g->imps);
    free(g->root); free(g->slug);
    free(g);
}

static CodeGraph *graph_new(const char *root)
{
    CodeGraph *g = (CodeGraph *)calloc(1, sizeof *g);
    g->root = str_dup(root);
    char sl[256]; slugify(root, sl, sizeof sl);
    g->slug = str_dup(sl);
    g->files = (GFile *)calloc(MAX_FILES, sizeof(GFile));
    g->syms = (GSym *)calloc(MAX_SYMS, sizeof(GSym));
    g->imps = (GImp *)calloc(MAX_IMPS, sizeof(GImp));
    return g;
}

char *graph_path_for(const char *root)
{
    char sl[256]; slugify(root, sl, sizeof sl);
    char *base = delta_home();
    char *dir = path_join(base, "graphs");
    dir_make(dir);
    char *p = path_join(dir, sl);
    free(dir); free(base);
    /* p + ".graph" */
    Str s; str_init(&s);
    str_adds(&s, p);
    str_adds(&s, ".graph");
    free(p);
    return str_steal(&s);
}

static int graph_save(CodeGraph *g)
{
    char *p = graph_path_for(g->root);
    Str o; str_init(&o);
    str_adds(&o, "DELTA-GRAPH 1\nroot ");
    str_adds(&o, g->root);
    str_adds(&o, "\nbuilt ");
    { char iso[32]; iso_now(iso); str_adds(&o, iso); }
    str_addc(&o, '\n');
    for (size_t i = 0; i < g->n_files; i++) {
        str_addf(&o, "file %llu %ld %s\n", (unsigned long long)g->files[i].hash,
                 g->files[i].size, g->files[i].path);
    }
    for (size_t i = 0; i < g->n_syms; i++) {
        str_addf(&o, "sym %c %s %d %s\n", g->syms[i].kind,
                 g->files[g->syms[i].file].path, g->syms[i].line, g->syms[i].name);
    }
    for (size_t i = 0; i < g->n_imps; i++) {
        str_addf(&o, "imp %s %s\n", g->files[g->imps[i].file].path, g->imps[i].target);
    }
    int ok = file_write(p, o.data, o.len);
    str_free(&o);
    free(p);
    g->dirty = 0;
    return ok;
}

int graph_index_file(CodeGraph *g, const char *path)
{
    size_t len = 0;
    char *txt = file_read(path, &len);
    if (!txt) return 0;
    uint64_t h = fnv64(txt, len);
    int idx = -1;
    for (size_t i = 0; i < g->n_files; i++)
        if (str_eq(g->files[i].path, path)) { idx = (int)i; break; }
    int changed = 0;
    if (idx >= 0) {
        if (g->files[idx].hash == h) { free(txt); return 0; }
        changed = 1;
    } else {
        if (g->n_files >= MAX_FILES) { free(txt); return 0; }
        idx = (int)g->n_files;
        g->files[g->n_files].path = str_dup(path);
        g->n_files++;
    }
    g->files[idx].hash = h;
    g->files[idx].size = (long)len;
    int line = 1;
    size_t pos = 0;
    while (pos < len) {
        size_t eol = pos;
        while (eol < len && txt[eol] != '\n') eol++;
        scan_line(g, idx, line, txt + pos, eol - pos);
        pos = eol + 1;
        line++;
    }
    free(txt);
    g->dirty = 1;
    return changed + 1;
}

CodeGraph *graph_open(const char *root, int rebuild)
{
    CodeGraph *g = graph_new(root);
    char *p = graph_path_for(root);
    int ok = 0;
    if (!rebuild && file_exists(p)) {
        size_t len = 0;
        char *txt = file_read(p, &len);
        if (txt) {
            size_t pos = 0;
            /* índice de arquivo por caminho */
            while (pos < len) {
                size_t eol = pos;
                while (eol < len && txt[eol] != '\n') eol++;
                const char *l = txt + pos;
                size_t ll = eol - pos;
                if (ll > 5 && !memcmp(l, "file ", 5)) {
                    unsigned long long h = 0; long sz = 0;
                    char pathbuf[512];
                    if (sscanf(l + 5, "%llu %ld %511[^\n]", &h, &sz, pathbuf) == 3) {
                        if (g->n_files < MAX_FILES) {
                            g->files[g->n_files].path = str_dup(pathbuf);
                            g->files[g->n_files].hash = (uint64_t)h;
                            g->files[g->n_files].size = sz;
                            g->n_files++;
                        }
                    }
                } else if (ll > 4 && !memcmp(l, "sym ", 4)) {
                    char kind = 0, pathbuf[512], name[128]; int line = 0;
                    if (sscanf(l + 4, " %c %511s %d %127s", &kind, pathbuf, &line, name) == 4) {
                        int fi = -1;
                        for (size_t k = 0; k < g->n_files; k++)
                            if (str_eq(g->files[k].path, pathbuf)) { fi = (int)k; break; }
                        if (fi >= 0 && g->n_syms < MAX_SYMS) {
                            g->syms[g->n_syms].name = str_dup(name);
                            g->syms[g->n_syms].file = fi;
                            g->syms[g->n_syms].line = line;
                            g->syms[g->n_syms].kind = kind;
                            g->n_syms++;
                        }
                    }
                } else if (ll > 4 && !memcmp(l, "imp ", 4)) {
                    char a[512], b[512];
                    if (sscanf(l + 4, "%511s %511s", a, b) == 2) {
                        int fi = -1;
                        for (size_t k = 0; k < g->n_files; k++)
                            if (str_eq(g->files[k].path, a)) { fi = (int)k; break; }
                        if (fi >= 0 && g->n_imps < MAX_IMPS) {
                            g->imps[g->n_imps].file = fi;
                            g->imps[g->n_imps].target = str_dup(b);
                            g->n_imps++;
                        }
                    }
                }
                pos = eol + 1;
            }
            free(txt);
            ok = g->n_files > 0;
        }
    }
    free(p);
    if (!ok) {
        /* varredura completa */
        if (g->n_files) { /* limpa */ }
        add_file_rec(g, root, "", 0);
        graph_save(g);
    }
    return g;
}

/* Atualiza só o que mudou (mtime/hash). Retorna quantos arquivos mudaram. */
int graph_refresh(CodeGraph *g)
{
    int mudou = 0;
    for (size_t i = 0; i < g->n_files; i++) {
        char *full = path_join(g->root, g->files[i].path);
        size_t len = 0;
        char *txt = file_read(full, &len);
        if (!txt) { free(full); continue; }
        uint64_t h = fnv64(txt, len);
        free(txt);
        if (h != g->files[i].hash) mudou += graph_index_file(g, g->files[i].path);
        free(full);
    }
    if (mudou) graph_save(g);
    return mudou;
}

/* ================================================================== */
/* consultas                                                           */
/* ================================================================== */
static const char *kind_name(char k)
{
    switch (k) {
        case 'f': return "função";
        case 't': return "tipo";
        case 'v': return "var";
        case 'm': return "macro";
        default:  return "?";
    }
}

char *graph_summary(const CodeGraph *g, size_t max_chars)
{
    Str s; str_init(&s);
    /* conta arquivos por diretório de primeiro nível */
    typedef struct { char dir[96]; int n; } Dir;
    Dir dirs[128]; int nd = 0;
    for (size_t i = 0; i < g->n_files; i++) {
        const char *p = g->files[i].path;
        const char *sl = strchr(p, '/');
        if (!sl) sl = strchr(p, '\\');
        char key[96];
        if (sl) { size_t l = (size_t)(sl - p); if (l > 95) l = 95; memcpy(key, p, l); key[l] = 0; }
        else snprintf(key, sizeof key, "(raiz)");
        int k = -1;
        for (int j = 0; j < nd; j++) if (str_eq(dirs[j].dir, key)) { k = j; break; }
        if (k < 0 && nd < 128) { k = nd++; snprintf(dirs[k].dir, sizeof dirs[k].dir, "%s", key); dirs[k].n = 0; }
        if (k >= 0) dirs[k].n++;
    }
    str_addf(&s, "PROJETO: %zu arquivos indexados · %zu símbolos · %zu imports\n",
             g->n_files, g->n_syms, g->n_imps);
    str_adds(&s, "diretórios: ");
    for (int j = 0; j < nd && j < 12; j++)
        str_addf(&s, "%s%s (%d)", j ? ", " : "", dirs[j].dir, dirs[j].n);
    str_addc(&s, '\n');

    /* arquivos mais "conectados" (imports + quem importa) */
    str_adds(&s, "núcleo: ");
    int shown = 0;
    for (size_t i = 0; i < g->n_files && shown < 8; i++) {
        int score = 0;
        for (size_t k = 0; k < g->n_imps; k++) {
            if (g->imps[k].file == (int)i) score++;
            const char *base = path_basename(g->files[i].path);
            if (base && str_eq(g->imps[k].target, base)) score++;
            free((void *)base);
        }
        if (score >= 2) { str_addf(&s, "%s%s", shown ? ", " : "", g->files[i].path); shown++; }
    }
    if (!shown) {
        for (size_t i = 0; i < g->n_files && i < 5; i++)
            str_addf(&s, "%s%s", i ? ", " : "", g->files[i].path);
    }
    str_addc(&s, '\n');

    /* principais símbolos */
    str_adds(&s, "símbolos: ");
    int k2 = 0;
    for (size_t i = 0; i < g->n_syms && k2 < 24; i++) {
        str_addf(&s, "%s%s (%s:%d)", k2 ? ", " : "", g->syms[i].name,
                 g->files[g->syms[i].file].path, g->syms[i].line);
        k2++;
    }
    str_addc(&s, '\n');

    if (s.len > max_chars) { str_truncate(&s, max_chars - 20); str_adds(&s, "\n… (truncado)\n"); }
    return str_steal(&s);
}

char *graph_find(const CodeGraph *g, const char *name, int max)
{
    Str s; str_init(&s);
    int n = 0;
    for (size_t i = 0; i < g->n_syms && n < max; i++) {
        if (graph_icontains(g->syms[i].name, name)) {
            str_addf(&s, "%s  %s:%d  (%s)\n", g->syms[i].name,
                     g->files[g->syms[i].file].path, g->syms[i].line,
                     kind_name(g->syms[i].kind));
            n++;
        }
    }
    if (!n) str_addf(&s, "(nenhum símbolo com '%s')\n", name);
    return str_steal(&s);
}

char *graph_file_info(const CodeGraph *g, const char *path, int max)
{
    Str s; str_init(&s);
    int n = 0, fi = -1;
    for (size_t i = 0; i < g->n_files; i++)
        if (str_eq(g->files[i].path, path) || str_ends(g->files[i].path, path)) { fi = (int)i; break; }
    if (fi < 0) { str_addf(&s, "(arquivo '%s' não está no grafo)\n", path); return str_steal(&s); }
    str_addf(&s, "%s  (%ld bytes)\n", g->files[fi].path, g->files[fi].size);
    str_adds(&s, "importa:");
    int imp = 0;
    for (size_t i = 0; i < g->n_imps; i++)
        if (g->imps[i].file == fi) { str_addf(&s, "%s%s", imp ? "," : "", g->imps[i].target); if (++imp >= 20) break; }
    if (!imp) str_adds(&s, " (nada)");
    str_addc(&s, '\n');
    str_adds(&s, "símbolos:\n");
    for (size_t i = 0; i < g->n_syms && n < max; i++) {
        if (g->syms[i].file == fi) {
            str_addf(&s, "  %s:%d  %s (%s)\n", g->files[fi].path, g->syms[i].line,
                     g->syms[i].name, kind_name(g->syms[i].kind));
            n++;
        }
    }
    if (!n) str_adds(&s, "  (nenhum)\n");
    return str_steal(&s);
}

char *graph_refs(const CodeGraph *g, const char *name, int max)
{
    Str s; str_init(&s);
    int n = 0;
    for (size_t i = 0; i < g->n_files && n < max; i++) {
        char *full = path_join(g->root, g->files[i].path);
        size_t len = 0;
        char *txt = file_read(full, &len);
        free(full);
        if (!txt) continue;
        int hits = 0;
        for (const char *p = txt; (p = strstr(p, name)) != NULL; p++) {
            /* palavra inteira */
            int antes = (p == txt) ? 0 : is_ident((unsigned char)p[-1]);
            size_t l = strlen(name);
            int depois = is_ident((unsigned char)p[l]);
            if (!antes && !depois) hits++;
        }
        if (hits) { str_addf(&s, "%s  (%d menções)\n", g->files[i].path, hits); n++; }
        free(txt);
    }
    if (!n) str_addf(&s, "(nenhuma menção a '%s')\n", name);
    return str_steal(&s);
}

size_t graph_file_count(const CodeGraph *g) { return g->n_files; }
size_t graph_sym_count(const CodeGraph *g) { return g->n_syms; }
