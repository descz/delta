/* tools.c - implementacao das ferramentas */
#include "tools.h"
#include "codegraph.h"

#include <time.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#  include <sys/wait.h>
#  include <signal.h>
#  include <fcntl.h>
#  include <errno.h>
#endif

#define MAX_LINES 2000
#define MAX_BYTES (50 * 1024)

static void (*g_tick)(void *) = NULL;
static void *g_tick_ud = NULL;

void tools_set_tick(void (*fn)(void *), void *ud)
{
    g_tick = fn;
    g_tick_ud = ud;
}

static void tools_tick(void)
{
    if (g_tick) g_tick(g_tick_ud);
}

void tool_result_init(ToolResult *r)
{
    memset(r, 0, sizeof(*r));
    str_init(&r->output);
}

void tool_result_free(ToolResult *r)
{
    str_free(&r->output);
}

/* ------------------------------------------------------------------ */
/* Caminho absoluto a partir do cwd                                    */
/* ------------------------------------------------------------------ */
char *tools_resolve(const char *cwd, const char *path)
{
    if (!path) return NULL;
    if (path[0] == '~') return path_expand(path);
#ifdef _WIN32
    int abs = (isalpha((unsigned char)path[0]) && path[1] == ':') || path[0] == '\\' || path[0] == '/';
#else
    int abs = path[0] == '/';
#endif
    if (abs) return str_dup(path);
    return path_join(cwd, path);
}

/* ------------------------------------------------------------------ */
/* Diff simples (prefixo/sufixo comum)                                 */
/* ------------------------------------------------------------------ */
typedef struct { char *ptr; size_t len; } Slice;

static size_t split_lines(const char *s, Slice **out)
{
    size_t cap = 16, n = 0;
    Slice *v = (Slice *)malloc(cap * sizeof(Slice));
    size_t i = 0, start = 0;
    size_t len = s ? strlen(s) : 0;
    while (i <= len) {
        if (i == len || s[i] == '\n') {
            if (n + 1 >= cap) { cap *= 2; v = (Slice *)realloc(v, cap * sizeof(Slice)); }
            v[n].ptr = (char *)s + start;
            v[n].len = i - start;
            n++;
            start = i + 1;
        }
        i++;
    }
    if (n > 0 && v[n - 1].len == 0 && len > 0) n--;  /* nao conta a linha final vazia */
    *out = v;
    return n;
}

void diff_lines(const char *a, const char *b, Str *out, int context)
{
    Slice *la = NULL, *lb = NULL;
    size_t na = split_lines(a, &la);
    size_t nb = split_lines(b, &lb);

    size_t prefix = 0;
    while (prefix < na && prefix < nb &&
           la[prefix].len == lb[prefix].len &&
           memcmp(la[prefix].ptr, lb[prefix].ptr, la[prefix].len) == 0)
        prefix++;

    size_t suffix = 0;
    while (suffix < (na - prefix) && suffix < (nb - prefix) &&
           la[na - 1 - suffix].len == lb[nb - 1 - suffix].len &&
           memcmp(la[na - 1 - suffix].ptr, lb[nb - 1 - suffix].ptr, la[na - 1 - suffix].len) == 0)
        suffix++;

    size_t ctx_start = prefix > (size_t)context ? prefix - (size_t)context : 0;
    for (size_t i = ctx_start; i < prefix; i++) {
        str_adds(out, "  ");
        str_add(out, la[i].ptr, la[i].len);
        str_addc(out, '\n');
    }
    for (size_t i = prefix; i < na - suffix; i++) {
        str_adds(out, "- ");
        str_add(out, la[i].ptr, la[i].len);
        str_addc(out, '\n');
    }
    for (size_t i = prefix; i < nb - suffix; i++) {
        str_adds(out, "+ ");
        str_add(out, lb[i].ptr, lb[i].len);
        str_addc(out, '\n');
    }
    size_t tail_end = (na - suffix) + (size_t)context;
    if (tail_end > na) tail_end = na;
    for (size_t i = na - suffix; i < tail_end; i++) {
        str_adds(out, "  ");
        str_add(out, la[i].ptr, la[i].len);
        str_addc(out, '\n');
    }
    free(la);
    free(lb);
}

/* ------------------------------------------------------------------ */
/* Execucao de processo                                                */
/* ------------------------------------------------------------------ */
#ifdef _WIN32
/* A saida de programas do Windows (dir, etc.) vem na pagina OEM. Se o texto
   nao for UTF-8 valido, e porque esta nessa pagina: converte para UTF-8. */
static int utf8_valido(const char *s, size_t n)
{
    size_t i = 0;
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { i++; continue; }
        int tam = 0;
        if ((c & 0xE0) == 0xC0) tam = 2;
        else if ((c & 0xF0) == 0xE0) tam = 3;
        else if ((c & 0xF8) == 0xF0) tam = 4;
        else return 0;
        if (i + (size_t)tam > n) return 0;
        for (int k = 1; k < tam; k++)
            if (((unsigned char)s[i + (size_t)k] & 0xC0) != 0x80) return 0;
        i += (size_t)tam;
    }
    return 1;
}

void tools_oem_to_utf8(Str *out)
{
    if (out->len == 0 || utf8_valido(out->data, out->len)) return;

    UINT cp = GetConsoleOutputCP();
    if (cp == 0 || cp == CP_UTF8) cp = CP_OEMCP;

    int wlen = MultiByteToWideChar(cp, 0, out->data, (int)out->len, NULL, 0);
    if (wlen <= 0) return;
    WCHAR *w = (WCHAR *)malloc((size_t)wlen * sizeof(WCHAR));
    MultiByteToWideChar(cp, 0, out->data, (int)out->len, w, wlen);
    int ulen = WideCharToMultiByte(CP_UTF8, 0, w, wlen, NULL, 0, NULL, NULL);
    if (ulen > 0) {
        char *u = (char *)malloc((size_t)ulen + 1);
        WideCharToMultiByte(CP_UTF8, 0, w, wlen, u, ulen, NULL, NULL);
        str_clear(out);
        str_add(out, u, (size_t)ulen);
        free(u);
    }
    free(w);
}
#else
void tools_oem_to_utf8(Str *out) { (void)out; }
#endif
#ifdef _WIN32
int tools_exec(const char *cwd, const char *command, int timeout_sec, Str *out, int *exit_code)
{
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof sa;
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    HANDLE rd = NULL, wr = NULL;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return 0;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    memset(&pi, 0, sizeof pi);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = NULL;

    /* chcp 65001: sem isso o cmd escreve na pagina OEM e os acentos viram
       bytes invalidos em UTF-8 (quebrando o JSON enviado ao modelo) */
    char *cmdline = (char *)malloc(strlen(command) + 64);
    sprintf(cmdline, "cmd.exe /c chcp 65001 >nul & %s", command);

    Str full; str_init(&full);
    str_adds(&full, cmdline);
    /* CreateProcessA exige buffer modificavel */
    char *mutable_cmd = str_dup(full.data);

    int ok = CreateProcessA(NULL, mutable_cmd, NULL, NULL, TRUE,
                            CREATE_NO_WINDOW, NULL, cwd, &si, &pi);
    free(mutable_cmd);
    str_free(&full);
    free(cmdline);
    CloseHandle(wr);
    if (!ok) { CloseHandle(rd); return 0; }

    DWORD start = GetTickCount();
    uint64_t deadline = (uint64_t)(timeout_sec > 0 ? timeout_sec * 1000 : 0);
    int timed_out = 0;
    char buf[4096];
    for (;;) {
        tools_tick();
        DWORD avail = 0;
        if (!PeekNamedPipe(rd, NULL, 0, NULL, &avail, NULL)) break;
        if (avail > 0) {
            DWORD got = 0;
            DWORD want = avail > sizeof buf ? (DWORD)sizeof buf : avail;
            if (ReadFile(rd, buf, want, &got, NULL) && got > 0) {
                str_add(out, buf, got);
            } else break;
        } else {
            DWORD st = WaitForSingleObject(pi.hProcess, 30);
            if (st == WAIT_OBJECT_0) {
                /* drena o que sobrou */
                DWORD a2 = 0;
                while (PeekNamedPipe(rd, NULL, 0, NULL, &a2, NULL) && a2 > 0) {
                    DWORD got = 0;
                    DWORD want = a2 > sizeof buf ? (DWORD)sizeof buf : a2;
                    if (!ReadFile(rd, buf, want, &got, NULL) || got == 0) break;
                    str_add(out, buf, got);
                }
                break;
            }
            if (deadline > 0 && (GetTickCount() - start) > deadline) { timed_out = 1; break; }
        }
    }

    if (timed_out) {
        TerminateProcess(pi.hProcess, 1);
        str_addf(out, "\n[Delta] comando interrompido apos %d s\n", timeout_sec);
    }
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(rd);
    *exit_code = (int)code;
    if (timed_out) *exit_code = 124;
    tools_oem_to_utf8(out);   /* saida de programas do Windows vem na pagina OEM */
    return 1;
}
#else
int tools_exec(const char *cwd, const char *command, int timeout_sec, Str *out, int *exit_code)
{
    int fds[2];
    if (pipe(fds) != 0) return 0;
    pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return 0; }
    if (pid == 0) {
        close(fds[0]);
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        close(fds[1]);
        if (cwd) chdir(cwd);
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        _exit(127);
    }
    close(fds[1]);
    uint64_t start = now_ms();
    int status = 0;
    int timed_out = 0;
    char buf[4096];
    for (;;) {
        tools_tick();
        fd_set rf;
        FD_ZERO(&rf);
        FD_SET(fds[0], &rf);
        struct timeval tv = { 0, 100000 };
        int r = select(fds[0] + 1, &rf, NULL, NULL, &tv);
        if (r > 0) {
            ssize_t got = read(fds[0], buf, sizeof buf);
            if (got > 0) str_add(out, buf, (size_t)got);
            else if (got == 0) break;
        } else {
            pid_t w = waitpid(pid, &status, WNOHANG);
            if (w == pid) break;
            if (timeout_sec > 0 && (int)(now_ms() - start) > timeout_sec * 1000) { timed_out = 1; break; }
        }
    }
    if (timed_out) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        str_addf(out, "\n[Delta] comando interrompido apos %d s\n", timeout_sec);
    }
    close(fds[0]);
    if (timed_out) *exit_code = 124;
    else if (WIFEXITED(status)) *exit_code = WEXITSTATUS(status);
    else *exit_code = 1;
    return 1;
}
#endif

/* ------------------------------------------------------------------ */
/* read                                                                */
/* ------------------------------------------------------------------ */
static int tool_read(Tool *self, JValue *args, const char *cwd, ToolResult *res)
{
    (void)self;
    char *path = tools_resolve(cwd, json_str(json_get(args, "path")));
    if (!path || !*path) {
        str_adds(&res->output, "Erro: 'path' e obrigatorio.");
        res->is_error = 1;
        free(path);
        return 0;
    }
    if (path_is_dir(path)) {
        size_t n = 0;
        char **list = dir_list(path, &n);
        str_addf(&res->output, "Diretorio: %s\n", path);
        for (size_t i = 0; i < n; i++) str_addf(&res->output, "%s\n", list[i]);
        dir_list_free(list, n);
        free(path);
        return 1;
    }
    size_t len = 0;
    char *text = file_read(path, &len);
    if (!text) {
        str_addf(&res->output, "Erro: nao foi possivel ler '%s'", path);
        res->is_error = 1;
        free(path);
        return 0;
    }

    double offset = json_num(json_get(args, "offset"), 1);
    double limit = json_num(json_get(args, "limit"), MAX_LINES);
    int start_line = (int)offset;
    if (start_line < 1) start_line = 1;
    int max_lines = (int)limit;
    if (max_lines <= 0 || max_lines > MAX_LINES) max_lines = MAX_LINES;

    /* seleciona as linhas pedidas */
    int line = 1;
    size_t i = 0;
    size_t sel_start = 0;
    int sel_lines = 0;
    size_t sel_end = 0;
    while (i < len) {
        size_t eol = i;
        while (eol < len && text[eol] != '\n') eol++;
        if (line >= start_line) {
            if (sel_lines == 0) sel_start = i;
            sel_lines++;
            sel_end = eol < len ? eol + 1 : eol;
            if (sel_lines >= max_lines) break;
        }
        line++;
        i = eol + 1;
    }
    if (sel_lines == 0) { sel_start = len; sel_end = len; }

    size_t sel_len = sel_end - sel_start;
    int truncated_bytes = 0;
    if (sel_len > MAX_BYTES && start_line == 1 && max_lines >= MAX_LINES) {
        /* corte simples por bytes na primeira leitura */
        sel_len = MAX_BYTES;
        truncated_bytes = 1;
    }
    str_add(&res->output, text + sel_start, sel_len);
    if (truncated_bytes) {
        str_addf(&res->output, "\n[Delta] conteudo truncado em %d KB (use offset/limit para ler mais)\n", MAX_BYTES / 1024);
        res->truncated = 1;
    }
    free(text);
    free(path);
    return 1;
}

static const char READ_SCHEMA[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"path\":{\"type\":\"string\",\"description\":\"Path to the file to read (relative or absolute)\"},"
    "\"offset\":{\"type\":\"number\",\"description\":\"Line number to start reading from (1-indexed)\"},"
    "\"limit\":{\"type\":\"number\",\"description\":\"Maximum number of lines to read\"}},"
    "\"required\":[\"path\"]}";

/* ------------------------------------------------------------------ */
/* write                                                               */
/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* undo: cópia do arquivo antes de cada escrita                        */
/* ------------------------------------------------------------------ */
static char *undo_dir(void)
{
    char *base = delta_home();
    char *d = path_join(base, "undo");
    dir_make(d);
    free(base);
    return d;
}

/* Guarda o conteúdo atual de `path` (se existir) e registra no índice. */
void tools_undo_snapshot(const char *path)
{
    size_t len = 0;
    char *txt = file_read(path, &len);
    if (!txt) return;                       /* arquivo novo: nada a restaurar */
    char *dir = undo_dir();
    static unsigned long seq = 0;
    uint64_t t = (uint64_t)time(NULL);
    char nome[64];
    snprintf(nome, sizeof nome, "%llu-%lu.bak", (unsigned long long)t, ++seq);
    char *bak = path_join(dir, nome);
    if (!file_write(bak, txt, len)) { free(bak); free(dir); free(txt); return; }

    Str line; str_init(&line);
    str_adds(&line, "{\"backup\":");
    json_write_string(&line, bak, strlen(bak));
    str_adds(&line, ",\"path\":");
    json_write_string(&line, path, strlen(path));
    str_addf(&line, ",\"bytes\":%zu,\"ts\":%llu}\n", len, (unsigned long long)t);
    char *idx = path_join(dir, "index.jsonl");
    file_append(idx, line.data, line.len);
    str_free(&line);
    free(idx); free(bak); free(dir); free(txt);
}

/* Restaura o backup mais recente (do `path`, se informado). */
int tools_undo_restore(const char *path, char **out_path)
{
    char *dir = undo_dir();
    char *idx = path_join(dir, "index.jsonl");
    size_t len = 0;
    char *txt = file_read(idx, &len);
    free(idx);
    if (!txt) { free(dir); return 0; }

    /* varre de trás para frente procurando a última linha (com path, se dado) */
    char *last = NULL; size_t lastlen = 0;
    size_t pos = 0;
    while (pos < len) {
        size_t eol = pos;
        while (eol < len && txt[eol] != '\n') eol++;
        if (eol > pos) {
            char save = txt[eol]; txt[eol] = 0;
            JValue *v = json_parse_cstr(txt + pos);
            const char *p = v ? json_str(json_get(v, "path")) : "";
            int match = !path || !*path || str_eq(p, path) || str_ends(p, path);
            if (match && v) { last = txt + pos; lastlen = eol - pos; }
            txt[eol] = save;
            if (v) json_free(v);
        }
        pos = eol + 1;
    }
    if (!last) { free(txt); free(dir); return 0; }
    char keep = last[lastlen];
    last[lastlen] = 0;
    JValue *v = json_parse_cstr(last);
    const char *bak = json_str(json_get(v, "backup"));
    const char *orig = json_str(json_get(v, "path"));
    int ok = 0;
    if (*bak && *orig) {
        size_t blen = 0;
        char *data = file_read(bak, &blen);
        if (data) {
            ok = file_write(orig, data, blen);
            free(data);
        }
        if (ok && out_path) *out_path = str_dup(orig);
    }
    if (v) json_free(v);
    last[lastlen] = keep;
    free(txt);
    free(dir);
    return ok;
}

static const char UNDO_SCHEMA[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"op\":{\"type\":\"string\",\"enum\":[\"restore\",\"list\"]},"
    "\"path\":{\"type\":\"string\",\"description\":\"arquivo específico (opcional)\"}},"
    "\"required\":[\"op\"]}";

static int tool_undo(Tool *self, JValue *args, const char *cwd, ToolResult *res)
{
    (void)self; (void)cwd;
    const char *op = json_str(json_get(args, "op"));
    if (!*op) op = "restore";
    if (str_eq(op, "list")) {
        char *dir = undo_dir();
        char *idx = path_join(dir, "index.jsonl");
        size_t len = 0;
        char *txt = file_read(idx, &len);
        free(idx); free(dir);
        if (!txt) { str_adds(&res->output, "(sem histórico de edições)\n"); return 1; }
        size_t pos = 0; int n = 0;
        while (pos < len) {
            size_t eol = pos;
            while (eol < len && txt[eol] != '\n') eol++;
            if (eol > pos) {
                char save = txt[eol]; txt[eol] = 0;
                JValue *v = json_parse_cstr(txt + pos);
                if (v && n < 30) {
                    str_addf(&res->output, "%s  (%ld bytes)\n",
                             json_str(json_get(v, "path")),
                             (long)json_num(json_get(v, "bytes"), 0));
                    n++;
                }
                if (v) json_free(v);
                txt[eol] = save;
            }
            pos = eol + 1;
        }
        free(txt);
        if (!n) str_adds(&res->output, "(sem histórico de edições)\n");
        return 1;
    }
    char *restored = NULL;
    const char *want = json_str(json_get(args, "path"));
    char *abs = *want ? tools_resolve(cwd, want) : NULL;
    if (tools_undo_restore(abs ? abs : want, &restored)) {
        free(abs);
        str_addf(&res->output, "Restaurado: %s\n", restored ? restored : "?");
        free(restored);
        return 1;
    }
    free(abs);
    str_adds(&res->output, "Nada para desfazer.\n");
    res->is_error = 1;
    return 0;
}

static int tool_write(Tool *self, JValue *args, const char *cwd, ToolResult *res)
{
    (void)self;
    char *path = tools_resolve(cwd, json_str(json_get(args, "path")));
    const char *content = json_str(json_get(args, "content"));
    if (!path || !*path) {
        str_adds(&res->output, "Erro: 'path' e obrigatorio.");
        res->is_error = 1;
        free(path);
        return 0;
    }
    int existed = file_exists(path);
    tools_undo_snapshot(path);
    size_t old_len = 0;
    char *old = existed ? file_read(path, &old_len) : NULL;

    char *dir = path_dirname(path);
    dir_make(dir);
    free(dir);

    if (!file_write(path, content, strlen(content))) {
        str_addf(&res->output, "Erro: nao foi possivel escrever '%s'", path);
        res->is_error = 1;
        free(old);
        free(path);
        return 0;
    }

    str_addf(&res->output, "%s %s\n\n", existed ? "Arquivo atualizado:" : "Arquivo criado:", path);
    diff_lines(old ? old : "", content, &res->output, 0);
    free(old);
    free(path);
    return 1;
}

static const char WRITE_SCHEMA[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"path\":{\"type\":\"string\",\"description\":\"Path to the file to write (relative or absolute)\"},"
    "\"content\":{\"type\":\"string\",\"description\":\"Content to write to the file\"}},"
    "\"required\":[\"path\",\"content\"]}";

/* ------------------------------------------------------------------ */
/* edit                                                                */
/* ------------------------------------------------------------------ */
static long count_occurrences(const char *hay, const char *needle)
{
    if (!needle || !*needle) return 0;
    long n = 0;
    size_t nl = strlen(needle);
    const char *p = hay;
    while ((p = strstr(p, needle))) { n++; p += nl; }
    return n;
}

static char *apply_one_edit(const char *src, const char *oldText, const char *newText,
                            const char *path, Str *err, int *ok)
{
    long count = count_occurrences(src, oldText);
    if (count == 0) {
        str_addf(err, "Erro em %s: oldText nao encontrado no arquivo.", path);
        *ok = 0;
        return NULL;
    }
    if (count > 1) {
        str_addf(err, "Erro em %s: oldText aparece %ld vezes; ele precisa ser unico.", path, count);
        *ok = 0;
        return NULL;
    }
    const char *at = strstr(src, oldText);
    size_t prefix = (size_t)(at - src);
    size_t ol = strlen(oldText);
    size_t nl = strlen(newText);
    size_t total = strlen(src) - ol + nl;
    char *out = (char *)malloc(total + 1);
    memcpy(out, src, prefix);
    memcpy(out + prefix, newText, nl);
    strcpy(out + prefix + nl, src + prefix + ol);
    *ok = 1;
    return out;
}

static int tool_edit(Tool *self, JValue *args, const char *cwd, ToolResult *res)
{
    (void)self;
    char *path = tools_resolve(cwd, json_str(json_get(args, "path")));
    if (!path || !*path) {
        str_adds(&res->output, "Erro: 'path' e obrigatorio.");
        res->is_error = 1;
        free(path);
        return 0;
    }
    size_t len = 0;
    char *src = file_read(path, &len);
    if (!src) {
        str_addf(&res->output, "Erro: nao foi possivel ler '%s'", path);
        res->is_error = 1;
        free(path);
        return 0;
    }

    /* aceita {oldText,newText} e/ou {edits:[{oldText,newText}]} */
    JValue *edits = json_get(args, "edits");
    size_t applied = 0;

    if (edits && edits->kind == J_ARR && edits->count > 0) {
        for (size_t i = 0; i < edits->count; i++) {
            JValue *e = edits->items[i];
            const char *ot = json_str(json_get(e, "oldText"));
            const char *nt = json_str(json_get(e, "newText"));
            int ok = 0;
            char *next = apply_one_edit(src, ot, nt, path, &res->output, &ok);
            if (!ok) { free(next); free(src); free(path); res->is_error = 1; return 0; }
            free(src);
            src = next;
            applied++;
        }
    } else {
        const char *ot = json_str(json_get(args, "oldText"));
        const char *nt = json_str(json_get(args, "newText"));
        if (!*ot) {
            str_adds(&res->output, "Erro: informe 'edits' ou 'oldText'/'newText'.");
            res->is_error = 1;
            free(src); free(path);
            return 0;
        }
        int ok = 0;
        char *next = apply_one_edit(src, ot, nt, path, &res->output, &ok);
        if (!ok) { free(next); free(src); free(path); res->is_error = 1; return 0; }
        free(src);
        src = next;
        applied = 1;
    }

    if (!file_write(path, src, strlen(src))) {
        str_addf(&res->output, "Erro: nao foi possivel gravar '%s'", path);
        res->is_error = 1;
        free(src); free(path);
        return 0;
    }
    str_addf(&res->output, "Editado %s (%zu alteracao%s)\n", path, applied, applied == 1 ? "" : "es");
    free(src);
    free(path);
    return 1;
}

static const char EDIT_SCHEMA[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"path\":{\"type\":\"string\",\"description\":\"Path to the file to edit (relative or absolute)\"},"
    "\"edits\":{\"type\":\"array\",\"description\":\"One or more targeted replacements\",\"items\":{"
    "\"type\":\"object\",\"properties\":{"
    "\"oldText\":{\"type\":\"string\",\"description\":\"Exact text for one targeted replacement. It must be unique in the original file\"},"
    "\"newText\":{\"type\":\"string\",\"description\":\"Replacement text for this targeted edit.\"}},"
    "\"required\":[\"oldText\",\"newText\"]}},"
    "\"oldText\":{\"type\":\"string\",\"description\":\"Atalho para um unico edit.\"},"
    "\"newText\":{\"type\":\"string\",\"description\":\"Atalho para um unico edit.\"}},"
    "\"required\":[\"path\"]}";

/* ------------------------------------------------------------------ */
/* bash                                                                */
/* ------------------------------------------------------------------ */
static int tool_bash(Tool *self, JValue *args, const char *cwd, ToolResult *res)
{
    (void)self;
    const char *command = json_str(json_get(args, "command"));
    if (!*command) {
        str_adds(&res->output, "Erro: 'command' e obrigatorio.");
        res->is_error = 1;
        return 0;
    }
    int timeout = (int)json_num(json_get(args, "timeout"), 0);
    uint64_t t0 = now_ms();
    int code = 0;
    if (!tools_exec(cwd, command, timeout, &res->output, &code)) {
        str_adds(&res->output, "Erro: falha ao iniciar o processo.");
        res->is_error = 1;
        return 0;
    }
    res->wall_seconds = (double)(now_ms() - t0) / 1000.0;
    res->exit_code = code;
    if (code != 0) res->is_error = 1;
    return 1;
}

static const char BASH_SCHEMA[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"command\":{\"type\":\"string\",\"description\":\"Shell command to execute\"},"
    "\"timeout\":{\"type\":\"number\",\"description\":\"Timeout in seconds (optional, no default timeout)\"}},"
    "\"required\":[\"command\"]}";

/* ------------------------------------------------------------------ */
/* ls / find / grep                                                    */
/* ------------------------------------------------------------------ */
static int tool_ls(Tool *self, JValue *args, const char *cwd, ToolResult *res)
{
    (void)self;
    char *path = tools_resolve(cwd, json_str(json_get(args, "path")));
    if (!path || !*path) { free(path); path = str_dup(cwd); }
    size_t n = 0;
    char **list = dir_list(path, &n);
    if (!list) {
        str_addf(&res->output, "Erro: nao foi possivel listar '%s'", path);
        res->is_error = 1;
        free(path);
        return 0;
    }
    str_addf(&res->output, "%s:\n", path);
    for (size_t i = 0; i < n; i++) {
        char *full = path_join(path, list[i]);
        str_addf(&res->output, "%s%s\n", list[i], path_is_dir(full) ? "/" : "");
        free(full);
    }
    dir_list_free(list, n);
    free(path);
    return 1;
}

static const char LS_SCHEMA[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"path\":{\"type\":\"string\",\"description\":\"Directory to list (relative or absolute, defaults to cwd)\"}}}";

static void find_walk(const char *base, const char *pattern, int max_depth, int depth, Str *out, int *count, int max_results)
{
    if (*count >= max_results || depth > max_depth) return;
    size_t n = 0;
    char **list = dir_list(base, &n);
    if (!list) return;
    for (size_t i = 0; i < n && *count < max_results; i++) {
        char *full = path_join(base, list[i]);
        if (path_is_dir(full)) {
            if (!str_eq(list[i], ".git") && !str_eq(list[i], "node_modules") && !str_eq(list[i], "build"))
                find_walk(full, pattern, max_depth, depth + 1, out, count, max_results);
        } else {
            if (!pattern || !*pattern || str_find(list[i], pattern)) {
                str_addf(out, "%s\n", full);
                (*count)++;
            }
        }
        free(full);
    }
    dir_list_free(list, n);
}

static int tool_find_files(Tool *self, JValue *args, const char *cwd, ToolResult *res)
{
    (void)self;
    char *path = tools_resolve(cwd, json_str(json_get(args, "path")));
    if (!path || !*path) { free(path); path = str_dup(cwd); }
    const char *pattern = json_str(json_get(args, "pattern"));
    int count = 0;
    find_walk(path, pattern, 12, 0, &res->output, &count, 500);
    if (count == 0) str_adds(&res->output, "(nenhum arquivo encontrado)\n");
    free(path);
    return 1;
}

static const char FIND_SCHEMA[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"pattern\":{\"type\":\"string\",\"description\":\"Substring do nome do arquivo\"},"
    "\"path\":{\"type\":\"string\",\"description\":\"Diretorio raiz (padrao: cwd)\"}},"
    "\"required\":[\"pattern\"]}";

static void grep_file(const char *full, const char *needle, Str *out, int *count, int max_results)
{
    size_t len = 0;
    char *text = file_read(full, &len);
    if (!text) return;
    int line = 1;
    size_t i = 0;
    while (i < len && *count < max_results) {
        size_t eol = i;
        while (eol < len && text[eol] != '\n') eol++;
        size_t ll = eol - i;
        if (ll < 4000) {
            char save = text[i + ll];
            text[i + ll] = 0;
            if (str_find(text + i, needle)) {
                str_addf(out, "%s:%d: %.*s\n", full, line, (int)ll, text + i);
                (*count)++;
            }
            text[i + ll] = save;
        }
        line++;
        i = eol + 1;
    }
    free(text);
}

static void grep_walk(const char *base, const char *needle, Str *out, int *count, int max_results, int depth)
{
    if (*count >= max_results || depth > 12) return;
    size_t n = 0;
    char **list = dir_list(base, &n);
    if (!list) return;
    for (size_t i = 0; i < n && *count < max_results; i++) {
        if (str_eq(list[i], ".git") || str_eq(list[i], "node_modules") || str_eq(list[i], "build") ||
            str_eq(list[i], "dist") || str_eq(list[i], ".pi"))
            continue;
        char *full = path_join(base, list[i]);
        if (path_is_dir(full)) grep_walk(full, needle, out, count, max_results, depth + 1);
        else grep_file(full, needle, out, count, max_results);
        free(full);
    }
    dir_list_free(list, n);
}

static int tool_grep(Tool *self, JValue *args, const char *cwd, ToolResult *res)
{
    (void)self;
    const char *needle = json_str(json_get(args, "pattern"));
    if (!*needle) {
        str_adds(&res->output, "Erro: 'pattern' e obrigatorio.");
        res->is_error = 1;
        return 0;
    }
    char *path = tools_resolve(cwd, json_str(json_get(args, "path")));
    if (!path || !*path) { free(path); path = str_dup(cwd); }
    int count = 0;
    grep_walk(path, needle, &res->output, &count, 300, 0);
    if (count == 0) str_adds(&res->output, "(nenhuma ocorrencia)\n");
    free(path);
    return 1;
}

static const char GREP_SCHEMA[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"pattern\":{\"type\":\"string\",\"description\":\"Texto a procurar\"},"
    "\"path\":{\"type\":\"string\",\"description\":\"Diretorio raiz (padrao: cwd)\"}},"
    "\"required\":[\"pattern\"]}";

/* ------------------------------------------------------------------ */
/* Registro                                                            */
/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* graph: consulta o grafo do projeto (montado uma vez, sem reler tudo) */
/* ------------------------------------------------------------------ */
static CodeGraph *g_graph = NULL;
static char      *g_graph_root = NULL;

static CodeGraph *graph_cached(const char *cwd)
{
    if (g_graph_root && str_eq(g_graph_root, cwd)) return g_graph;
    graph_free(g_graph);
    free(g_graph_root);
    g_graph = graph_open(cwd, 0);
    g_graph_root = str_dup(cwd);
    return g_graph;
}

static const char GRAPH_SCHEMA[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"op\":{\"type\":\"string\",\"enum\":[\"summary\",\"find\",\"file\",\"refs\",\"build\",\"status\"],"
    "\"description\":\"summary = mapa do projeto; find = símbolos por nome; file = símbolos/imports de um arquivo; "
    "refs = onde o nome aparece; build = reindexa; status = contagem\"},"
    "\"name\":{\"type\":\"string\",\"description\":\"nome do símbolo (op=find/refs)\"},"
    "\"path\":{\"type\":\"string\",\"description\":\"caminho relativo do arquivo (op=file)\"}},"
    "\"required\":[\"op\"]}";

static int tool_graph(Tool *self, JValue *args, const char *cwd, ToolResult *res)
{
    (void)self;
    const char *op = json_str(json_get(args, "op"));
    if (!*op) op = "summary";
    if (str_eq(op, "build")) {
        graph_free(g_graph); free(g_graph_root); g_graph = NULL; g_graph_root = NULL;
    }
    CodeGraph *g = graph_cached(cwd);
    if (!g) { str_adds(&res->output, "Erro: não consegui montar o grafo."); res->is_error = 1; return 0; }

    if (str_eq(op, "build")) {
        int n = graph_refresh(g);
        char *sum = graph_summary(g, 1800);
        str_addf(&res->output, "Grafo reindexado (%d arquivo(s) atualizado(s)).\n\n%s", n, sum);
        free(sum);
    } else if (str_eq(op, "status")) {
        str_addf(&res->output, "arquivos: %zu\nsímbolos: %zu\n",
                 graph_file_count(g), graph_sym_count(g));
        char *p = graph_path_for(cwd);
        str_addf(&res->output, "cache: %s\n", p);
        free(p);
    } else if (str_eq(op, "find")) {
        char *r = graph_find(g, json_str(json_get(args, "name")), 40);
        str_adds(&res->output, r); free(r);
    } else if (str_eq(op, "file")) {
        char *r = graph_file_info(g, json_str(json_get(args, "path")), 60);
        str_adds(&res->output, r); free(r);
    } else if (str_eq(op, "refs")) {
        char *r = graph_refs(g, json_str(json_get(args, "name")), 30);
        str_adds(&res->output, r); free(r);
    } else {
        char *r = graph_summary(g, 4000);
        str_adds(&res->output, r); free(r);
    }
    return 1;
}

static const Tool TOOLS[] = {
    { "read",  "Read file contents", READ_SCHEMA,  tool_read },
    { "write", "Create or overwrite files", WRITE_SCHEMA, tool_write },
    { "edit",  "Make precise file edits with exact text replacement", EDIT_SCHEMA, tool_edit },
    { "graph", "Project graph: symbols and imports (summary|find|file|refs|build|status)", GRAPH_SCHEMA, tool_graph },
    { "undo",  "Restore the previous content of a file (list|restore)", UNDO_SCHEMA, tool_undo },
    { "bash",  "Execute shell commands (ls, grep, find, etc.)", BASH_SCHEMA, tool_bash },
    { "ls",    "List directory contents", LS_SCHEMA, tool_ls },
    { "find",  "Find files by name substring", FIND_SCHEMA, tool_find_files },
    { "grep",  "Search text inside files", GREP_SCHEMA, tool_grep },
    { NULL, NULL, NULL, NULL }
};

/* Tabela final = tools base + as familias unificadas de tools_ext.c */
static Tool g_todas[80];
static int  g_todas_prontas = 0;

const Tool *tools_all(void)
{
    if (!g_todas_prontas) {
        size_t n = 0;
        for (const Tool *t = TOOLS; t->name && n < 79; t++) g_todas[n++] = *t;
        const Tool *ext = tools_ext_all();
        for (const Tool *t = ext; t && t->name && n < 79; t++) g_todas[n++] = *t;
        const Tool *ex = extensions_all();
        for (const Tool *t = ex; t && t->name && n < 79; t++) g_todas[n++] = *t;
        const Tool *dy = ext_tools_all();
        for (const Tool *t = dy; t && t->name && n < 79; t++) g_todas[n++] = *t;
        memset(&g_todas[n], 0, sizeof g_todas[0]);
        g_todas_prontas = 1;
    }
    return g_todas;
}

const Tool *tool_find(const char *name)
{
    for (const Tool *t = tools_all(); t->name; t++)
        if (str_eq(t->name, name)) return t;
    return NULL;
}
