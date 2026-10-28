/* util.c */
#include "util.h"
#include "skills_seed.h"      /* texto das skills embutidas (tools/embed-skills.py) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#  include <windows.h>
#  include <direct.h>
#  include <io.h>
#else
#  include <unistd.h>
#  include <sys/stat.h>
#  include <dirent.h>
#endif

/* ================================================================== */
/* Str                                                                 */
/* ================================================================== */
void str_init(Str *s) { s->data = NULL; s->len = 0; s->cap = 0; }
void str_free(Str *s) { free(s->data); s->data = NULL; s->len = s->cap = 0; }
void str_clear(Str *s) { s->len = 0; if (s->data) s->data[0] = 0; }

void str_reserve(Str *s, size_t extra)
{
    size_t need = s->len + extra + 1;
    if (need <= s->cap) return;
    size_t cap = s->cap ? s->cap : 64;
    while (cap < need) cap *= 2;
    char *p = (char *)realloc(s->data, cap);
    if (!p) { fprintf(stderr, "pi: out of memory\n"); exit(1); }
    s->data = p;
    s->cap = cap;
}

void str_add(Str *s, const char *bytes, size_t n)
{
    if (!n) return;
    str_reserve(s, n);
    memcpy(s->data + s->len, bytes, n);
    s->len += n;
    s->data[s->len] = 0;
}

void str_adds(Str *s, const char *cstr)
{
    if (cstr) str_add(s, cstr, strlen(cstr));
}

void str_addc(Str *s, char c)
{
    str_reserve(s, 1);
    s->data[s->len++] = c;
    s->data[s->len] = 0;
}

void str_addn(Str *s, const char *cstr, size_t max)
{
    if (!cstr) return;
    size_t n = strlen(cstr);
    str_add(s, cstr, n < max ? n : max);
}

void str_addf(Str *s, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n <= 0) { va_end(ap2); return; }
    str_reserve(s, (size_t)n);
    vsnprintf(s->data + s->len, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    s->len += (size_t)n;
}

int utf8_encode(uint32_t cp, char *buf)
{
    if (cp < 0x80) { buf[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    buf[0] = (char)(0xF0 | (cp >> 18));
    buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    buf[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

void str_addcp(Str *s, uint32_t cp)
{
    char buf[4];
    int n = utf8_encode(cp, buf);
    str_add(s, buf, (size_t)n);
}

void str_truncate(Str *s, size_t len)
{
    if (len < s->len) { s->len = len; if (s->data) s->data[len] = 0; }
}

char *str_steal(Str *s)
{
    char *p = s->data;
    s->data = NULL; s->len = s->cap = 0;
    return p;
}

/* ================================================================== */
/* UTF-8                                                               */
/* ================================================================== */
int utf8_decode(const char *s, size_t len, uint32_t *out)
{
    if (len == 0) return 0;
    const unsigned char *p = (const unsigned char *)s;
    unsigned char c = p[0];
    if (c < 0x80) { *out = c; return 1; }
    if ((c & 0xE0) == 0xC0) {
        if (len < 2 || (p[1] & 0xC0) != 0x80) { *out = 0xFFFD; return 1; }
        *out = ((uint32_t)(c & 0x1F) << 6) | (p[1] & 0x3F);
        return 2;
    }
    if ((c & 0xF0) == 0xE0) {
        if (len < 3 || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) { *out = 0xFFFD; return 1; }
        *out = ((uint32_t)(c & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        return 3;
    }
    if ((c & 0xF8) == 0xF0) {
        if (len < 4 || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80) {
            *out = 0xFFFD; return 1;
        }
        *out = ((uint32_t)(c & 0x07) << 18) | ((uint32_t)(p[1] & 0x3F) << 12) |
               ((uint32_t)(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        return 4;
    }
    *out = 0xFFFD;
    return 1;
}

size_t utf8_len(const char *s)
{
    size_t n = 0;
    while (*s) { if ((*s & 0xC0) != 0x80) n++; s++; }
    return n;
}

/* Largura de coluna. Combina com o que o Node usa via get-east-asian-width. */
int cp_width(uint32_t cp)
{
    if (cp == 0) return 0;
    if (cp < 32 || (cp >= 0x7F && cp < 0xA0)) return 0;
    /* Combinantes / formatadores */
    if ((cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x1AB0 && cp <= 0x1AFF) ||
        (cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x20D0 && cp <= 0x20FF) ||
        (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0xFE20 && cp <= 0xFE2F) ||
        (cp >= 0x200B && cp <= 0x200F) || cp == 0x0FEFF || cp == 0x2060)
        return 0;
    /* Wide / fullwidth */
    if ((cp >= 0x1100 && cp <= 0x115F) || cp == 0x2329 || cp == 0x232A ||
        (cp >= 0x2E80 && cp <= 0x303E) || (cp >= 0x3041 && cp <= 0x33FF) ||
        (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF) ||
        (cp >= 0xA000 && cp <= 0xA4CF) || (cp >= 0xAC00 && cp <= 0xD7A3) ||
        (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFE10 && cp <= 0xFE19) ||
        (cp >= 0xFE30 && cp <= 0xFE6F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
        (cp >= 0xFFE0 && cp <= 0xFFE6) ||
        (cp >= 0x1F300 && cp <= 0x1F64F) || (cp >= 0x1F900 && cp <= 0x1F9FF) ||
        (cp >= 0x1F680 && cp <= 0x1F6FF) || (cp >= 0x20000 && cp <= 0x3FFFD))
        return 2;
    return 1;
}

int strn_width(const char *s, size_t n)
{
    int w = 0;
    size_t i = 0;
    while (i < n && s[i]) {
        /* pula sequencias ANSI */
        if (s[i] == 0x1B) {
            i++;
            if (i < n && s[i] == '[') {
                i++;
                while (i < n && !((s[i] >= '@' && s[i] <= '~'))) i++;
                if (i < n) i++;
            } else if (i < n && s[i] == ']') {
                i++;
                while (i < n && s[i] != 0x07) i++;
                if (i < n) i++;
            } else if (i < n) i++;
            continue;
        }
        uint32_t cp;
        int c = utf8_decode(s + i, n - i, &cp);
        if (c <= 0) break;
        w += cp_width(cp);
        i += (size_t)c;
    }
    return w;
}

int str_width(const char *s) { return strn_width(s, strlen(s)); }

size_t str_clip_cols(const char *s, size_t n, int cols)
{
    int w = 0;
    size_t i = 0;
    while (i < n && s[i]) {
        if (s[i] == 0x1B) {
            size_t start = i;
            i++;
            if (i < n && s[i] == '[') {
                i++;
                while (i < n && !((s[i] >= '@' && s[i] <= '~'))) i++;
                if (i < n) i++;
            } else if (i < n && s[i] == ']') {
                i++;
                while (i < n && s[i] != 0x07) i++;
                if (i < n) i++;
            } else if (i < n) i++;
            (void)start;
            continue;
        }
        uint32_t cp;
        int c = utf8_decode(s + i, n - i, &cp);
        if (c <= 0) break;
        int cw = cp_width(cp);
        if (w + cw > cols) break;
        w += cw;
        i += (size_t)c;
    }
    return i;
}

/* ================================================================== */
/* Strings                                                             */
/* ================================================================== */
int str_eq(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }

int str_ieq(const char *a, const char *b)
{
    if (!a || !b) return 0;
    while (*a && *b) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
        a++; b++;
    }
    return *a == *b;
}

int str_starts(const char *s, const char *prefix)
{
    if (!s || !prefix) return 0;
    size_t n = strlen(prefix);
    return strncmp(s, prefix, n) == 0;
}

int str_istarts(const char *s, const char *prefix)
{
    if (!s || !prefix) return 0;
    while (*prefix) {
        char x = *s, y = *prefix;
        if (!x) return 0;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
        s++; prefix++;
    }
    return 1;
}

int str_ends(const char *s, const char *suffix)
{
    if (!s || !suffix) return 0;
    size_t ls = strlen(s), lf = strlen(suffix);
    if (lf > ls) return 0;
    return strcmp(s + ls - lf, suffix) == 0;
}

const char *str_find(const char *hay, const char *needle)
{
    if (!hay || !needle) return NULL;
    return strstr(hay, needle);
}

char *str_dup(const char *s) { return s ? str_ndup(s, strlen(s)) : NULL; }

char *str_ndup(const char *s, size_t n)
{
    char *p = (char *)malloc(n + 1);
    if (!p) { fprintf(stderr, "pi: out of memory\n"); exit(1); }
    if (n) memcpy(p, s, n);
    p[n] = 0;
    return p;
}

/* ================================================================== */
/* Arquivos                                                            */
/* ================================================================== */
char *file_read(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = 0;
    if (out_len) *out_len = rd;
    return buf;
}

int file_write(const char *path, const char *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    size_t wr = len ? fwrite(data, 1, len, f) : 0;
    fclose(f);
    return wr == len;
}

int file_append(const char *path, const char *data, size_t len)
{
    FILE *f = fopen(path, "ab");
    if (!f) return 0;
    size_t wr = len ? fwrite(data, 1, len, f) : 0;
    fclose(f);
    return wr == len;
}

int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

int dir_exists(const char *path)
{
#ifdef _WIN32
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

int path_is_dir(const char *path)
{
    if (!path || !*path) return 0;
#ifdef _WIN32
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

#ifdef _WIN32
static void mkdir_one(const char *path) { _mkdir(path); }
#else
static void mkdir_one(const char *path) { mkdir(path, 0755); }
#endif

int dir_make(const char *path)
{
    if (!path || !*path) return 0;
    if (dir_exists(path)) return 1;
    char *tmp = str_dup(path);
    size_t n = strlen(tmp);
    for (size_t i = 1; i < n; i++) {
        if (tmp[i] == '/' || tmp[i] == '\\') {
            char save = tmp[i];
            tmp[i] = 0;
            if (*tmp) mkdir_one(tmp);
            tmp[i] = save;
        }
    }
    mkdir_one(tmp);
    int ok = dir_exists(tmp);
    free(tmp);
    return ok;
}

char *path_join(const char *a, const char *b)
{
    if (!a || !*a) return str_dup(b ? b : "");
    if (!b || !*b) return str_dup(a);
    size_t la = strlen(a);
    int need_sep = !(a[la - 1] == '/' || a[la - 1] == '\\');
    Str s; str_init(&s);
    str_adds(&s, a);
    if (need_sep) str_addc(&s, '/');
    str_adds(&s, b);
    return str_steal(&s);
}

char *path_dirname(const char *path)
{
    char *p = str_dup(path);
    size_t n = strlen(p);
    while (n > 1 && (p[n - 1] == '/' || p[n - 1] == '\\')) p[--n] = 0;
    while (n > 0 && !(p[n - 1] == '/' || p[n - 1] == '\\')) n--;
    if (n == 0) { strcpy(p, "."); return p; }
    p[n - 1] = 0;
    if (!*p) strcpy(p, "/");
    return p;
}

char *path_basename(const char *path)
{
    const char *p = path;
    const char *last = path;
    for (; *p; p++) if (*p == '/' || *p == '\\') last = p + 1;
    return str_dup(last);
}

char *env_get(const char *name)
{
    const char *v = getenv(name);
    return v ? str_dup(v) : NULL;
}

char *cwd_get(void)
{
#ifdef _WIN32
    char buf[4096];
    if (_getcwd(buf, sizeof buf)) return str_dup(buf);
    return str_dup(".");
#else
    char buf[4096];
    if (getcwd(buf, sizeof buf)) return str_dup(buf);
    return str_dup(".");
#endif
}

char *home_get(void)
{
#ifdef _WIN32
    /* no Windows a pasta do usuario e' C:\Users\<nome> (USERPROFILE). HOME de
       shells MSYS/Git Bash vem como /c/Users/... e nao serve para caminhos
       passados ao CreateProcess. */
    char *h = env_get("USERPROFILE");
    if (h && *h) return h;
    free(h);
    h = env_get("HOME");
    if (h && *h) return h;
    free(h);
    return str_dup(".");
#else
    char *h = env_get("HOME");
    if (h && *h) return h;
    free(h);
    return str_dup(".");
#endif
}

/* ================================================================== */
/* ~/.deltaCore                                                        */
/* ================================================================== */
static char *g_core_override = NULL;

void delta_set_core(const char *dir)
{
    free(g_core_override);
    g_core_override = (dir && *dir) ? str_dup(dir) : NULL;
}

/* Onde ficam os dados do Delta. Ordem:
   1. --core <pasta>                  (rodada atual)
   2. DELTA_CORE=<pasta>              (variavel de ambiente)
   3. <home>/.deltaCore/pointer.txt   (pasta escolhida no instalador)
   4. <home>/.deltaCore               (padrao: C:\Users\<nome>\.deltaCore) */
char *delta_home(void)
{
    char *chosen = NULL;

    if (g_core_override && *g_core_override) {
        chosen = str_dup(g_core_override);
        dir_make(chosen);
        return chosen;
    }
    {
        char *env = env_get("DELTA_CORE");
        if (env && *env) {
            dir_make(env);
            return env;
        }
        free(env);
    }

    char *home = home_get();
    char *anchor = path_join(home, ".deltaCore");
    dir_make(anchor);
    free(home);

    char *ptr = path_join(anchor, "pointer.txt");
    if (file_exists(ptr)) {
        size_t n = 0;
        char *txt = file_read(ptr, &n);
        if (txt) {
            size_t l = strlen(txt);
            while (l && (txt[l - 1] == '\n' || txt[l - 1] == '\r' || txt[l - 1] == ' ')) txt[--l] = 0;
            char *p = txt;
            if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB) p += 3;  /* BOM */
            if (p[0] == '"') { p++; if (l > 1) p[strlen(p) - 1] = 0; }
            if (*p) {
                dir_make(p);
                chosen = str_dup(p);
            }
            free(txt);
        }
    }
    free(ptr);

    if (chosen) { free(anchor); return chosen; }
    return anchor;
}

static const char SYSTEM_PROMPT_SEED[] =
    "<!-- Instrucoes extras do Delta. Tudo aqui e' anexado ao prompt de sistema\n"
    "     em toda conversa (e recarregado a cada turno). Edite a vontade. -->\n";

void delta_bootstrap(void)
{
    char *base = delta_home();
    static const char *subs[] = { "skills", "mcps", "extensions", "system",
                                  "sessions", "paste", "jobs", "themes" };
    for (size_t i = 0; i < sizeof subs / sizeof subs[0]; i++) {
        char *d = path_join(base, subs[i]);
        dir_make(d);
        free(d);
    }

    /* skills embutidas: entram no ambiente na primeira execucao e nunca
       sobrescrevem o que o usuario editou (cada SKILL.md e' dele depois) */
    {
        char *rootsk = path_join(base, "skills");
        for (int i = 0; SKILLS_SEED[i].name; i++) {
            char *d = path_join(rootsk, SKILLS_SEED[i].name);
            char *md = path_join(d, "SKILL.md");
            if (!file_exists(md)) {
                dir_make(d);
                file_write(md, SKILLS_SEED[i].body, strlen(SKILLS_SEED[i].body));
            }
            free(md);
            free(d);
        }
        free(rootsk);
    }

    char *sp = path_join(base, "system/prompt.md");
    if (!file_exists(sp)) file_write(sp, SYSTEM_PROMPT_SEED, sizeof SYSTEM_PROMPT_SEED - 1);
    free(sp);

    char *mem = path_join(base, "memory.json");
    if (!file_exists(mem)) file_write(mem, "[]\n", 3);
    free(mem);

    free(base);
}

char *path_expand(const char *path)
{
    if (path && path[0] == '~' && (path[1] == 0 || path[1] == '/' || path[1] == '\\')) {
        char *h = home_get();
        char *rest = path_join(h, path + 1);
        free(h);
        return rest;
    }
    return str_dup(path);
}

/* ================================================================== */
/* Diretorios                                                          */
/* ================================================================== */
#ifdef _WIN32
char **dir_list(const char *path, size_t *count)
{
    *count = 0;
    char *pattern = path_join(path, "*");
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    free(pattern);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    size_t cap = 16, n = 0;
    char **list = (char **)malloc(cap * sizeof(char *));
    do {
        if (str_eq(fd.cFileName, ".") || str_eq(fd.cFileName, "..")) continue;
        if (n + 2 > cap) { cap *= 2; list = (char **)realloc(list, cap * sizeof(char *)); }
        list[n++] = str_dup(fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    list[n] = NULL;
    *count = n;
    return list;
}
#else
char **dir_list(const char *path, size_t *count)
{
    *count = 0;
    DIR *d = opendir(path);
    if (!d) return NULL;
    size_t cap = 16, n = 0;
    char **list = (char **)malloc(cap * sizeof(char *));
    struct dirent *e;
    while ((e = readdir(d))) {
        if (str_eq(e->d_name, ".") || str_eq(e->d_name, "..")) continue;
        if (n + 2 > cap) { cap *= 2; list = (char **)realloc(list, cap * sizeof(char *)); }
        list[n++] = str_dup(e->d_name);
    }
    closedir(d);
    list[n] = NULL;
    *count = n;
    return list;
}
#endif

void dir_list_free(char **list, size_t count)
{
    if (!list) return;
    for (size_t i = 0; i < count; i++) free(list[i]);
    free(list);
}

/* ================================================================== */
/* Diversos                                                            */
/* ================================================================== */
uint64_t now_ms(void)
{
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
#endif
}

void msleep(int ms)
{
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

void short_id(char out[9])
{
    static uint64_t ctr = 0;
    uint64_t t = now_ms() ^ (++ctr * 0x9E3779B97F4A7C15ull);
    unsigned r = (unsigned)(t ^ (t >> 32));
    snprintf(out, 9, "%08x", r);
}

void uuid_v4(char out[37])
{
    static uint64_t ctr = 0;
    uint64_t a = now_ms() * 6364136223846793005ull + (++ctr) * 1442695040888963407ull;
    uint64_t b = a ^ 0x5DEECE66Dull;
    unsigned char bytes[16];
    for (int i = 0; i < 8; i++) { bytes[i] = (unsigned char)(a >> (i * 8)); bytes[i + 8] = (unsigned char)(b >> (i * 8)); }
    bytes[6] = (unsigned char)((bytes[6] & 0x0F) | 0x40);
    bytes[8] = (unsigned char)((bytes[8] & 0x3F) | 0x80);
    snprintf(out, 37,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
             bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
}

void iso_now(char out[32])
{
    time_t t = time(NULL);
    struct tm tmv;
#ifdef _WIN32
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    strftime(out, 32, "%Y-%m-%dT%H:%M:%SZ", &tmv);
}

void base64_encode(Str *out, const unsigned char *data, size_t n)
{
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i = 0;
    while (i + 3 <= n) {
        unsigned v = ((unsigned)data[i] << 16) | ((unsigned)data[i + 1] << 8) | data[i + 2];
        str_addc(out, T[(v >> 18) & 63]);
        str_addc(out, T[(v >> 12) & 63]);
        str_addc(out, T[(v >> 6) & 63]);
        str_addc(out, T[v & 63]);
        i += 3;
    }
    if (n - i == 1) {
        unsigned v = (unsigned)data[i] << 16;
        str_addc(out, T[(v >> 18) & 63]);
        str_addc(out, T[(v >> 12) & 63]);
        str_adds(out, "==");
    } else if (n - i == 2) {
        unsigned v = ((unsigned)data[i] << 16) | ((unsigned)data[i + 1] << 8);
        str_addc(out, T[(v >> 18) & 63]);
        str_addc(out, T[(v >> 12) & 63]);
        str_addc(out, T[(v >> 6) & 63]);
        str_addc(out, '=');
    }
}

void json_escape(Str *out, const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '"':  str_adds(out, "\\\""); break;
            case '\\': str_adds(out, "\\\\"); break;
            case '\n': str_adds(out, "\\n"); break;
            case '\r': str_adds(out, "\\r"); break;
            case '\t': str_adds(out, "\\t"); break;
            case '\b': str_adds(out, "\\b"); break;
            case '\f': str_adds(out, "\\f"); break;
            default:
                if (c < 0x20) {
                    char tmp[8];
                    snprintf(tmp, sizeof tmp, "\\u%04x", c);
                    str_adds(out, tmp);
                    break;
                }
                if (c < 0x80) { str_addc(out, (char)c); break; }

                /* Nao-ASCII: so passa se for UTF-8 valido. Saida de programas
                   do Windows (dir, etc.) vem na pagina OEM e quebraria o JSON. */
                {
                    int tam = 0;
                    uint32_t cp = 0;
                    if ((c & 0xE0) == 0xC0) { tam = 2; cp = c & 0x1F; }
                    else if ((c & 0xF0) == 0xE0) { tam = 3; cp = c & 0x0F; }
                    else if ((c & 0xF8) == 0xF0) { tam = 4; cp = c & 0x07; }

                    int ok = tam > 0 && i + (size_t)tam <= n;
                    if (ok) {
                        for (int k = 1; k < tam; k++) {
                            unsigned char cc = (unsigned char)s[i + (size_t)k];
                            if ((cc & 0xC0) != 0x80) { ok = 0; break; }
                            cp = (cp << 6) | (cc & 0x3F);
                        }
                    }
                    /* rejeita overlong, fora do range e surrogates */
                    if (ok) {
                        if ((tam == 2 && cp < 0x80) || (tam == 3 && cp < 0x800) ||
                            (tam == 4 && cp < 0x10000)) ok = 0;
                        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) ok = 0;
                    }
                    if (ok) {
                        str_add(out, s + i, (size_t)tam);
                        i += (size_t)tam - 1;
                    } else {
                        str_adds(out, "\ufffd");   /* U+FFFD */
                    }
                }
        }
    }
}
