/* tools_ext.c - as familias unificadas de ferramentas (arsenal do Delta Agent)
 *
 *   fs      list read write append replace mkdir delete search glob
 *   git     status diff log checkpoint
 *   shell   run start list tail cancel
 *   web     http scrape browser
 *   memory  remember update search list link unlink query insert
 *   code    structure symbol tests
 *   assert  equals contains
 *   agent   plan subagent skill
 *   extend  mcp plugin
 *
 * Cada familia e' uma tool unica com um campo "op" (mesma ideia do harness
 * ZenoTools/unified). As operacoes que ja existiam (read/write/ls/grep/bash)
 * sao as mesmas funcoes por baixo: nada foi duplicado.
 */
#include "tools.h"
#include "json.h"
#include "agent.h"
#include "modelsdev.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <winhttp.h>
#else
#  include <unistd.h>
#  include <signal.h>
#  include <sys/wait.h>
#  include <fcntl.h>
#  include <errno.h>
#endif

#define EXT_MAX_OUT (200 * 1024)

/* ------------------------------------------------------------------ */
/* argumentos                                                          */
/* ------------------------------------------------------------------ */
static const char *arg_s(JValue *a, const char *k)
{
    const char *v = json_str(json_get(a, k));
    return v ? v : "";
}

static int arg_i(JValue *a, const char *k, int def)
{
    JValue *v = json_get(a, k);
    if (!v) return def;
    return (int)json_num(v, def);
}

static int arg_b(JValue *a, const char *k, int def)
{
    JValue *v = json_get(a, k);
    if (!v) return def;
    return json_bool(v, def);
}

static void err_out(ToolResult *r, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[1024];
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    str_adds(&r->output, buf);
    r->is_error = 1;
}

/* ------------------------------------------------------------------ */
/* caminhos: resolve e mantem dentro do workspace (sandbox)            */
/* ------------------------------------------------------------------ */
static int ieq_prefix(const char *s, const char *p)
{
#ifdef _WIN32
    while (*p) {
        if (tolower((unsigned char)*s) != tolower((unsigned char)*p)) return 0;
        s++; p++;
    }
    return 1;
#else
    return strncmp(s, p, strlen(p)) == 0;
#endif
}

/* normaliza "a/../b", "a/./b" e separadores repetidos (Windows e POSIX) */
static char *path_normalize(const char *in)
{
    size_t n = strlen(in);
    char *tmp = str_dup(in);
    for (size_t i = 0; i < n; i++) if (tmp[i] == '\\') tmp[i] = '/';
    char *parts[128];
    int np = 0;
    char *save = NULL;
    char raiz[8] = { 0 };
    char *p = tmp;
    if (n >= 2 && isalpha((unsigned char)p[0]) && p[1] == ':') { raiz[0] = p[0]; raiz[1] = ':'; p += 2; }
    for (char *tok = strtok_r(p, "/", &save); tok && np < 128; tok = strtok_r(NULL, "/", &save)) {
        if (str_eq(tok, ".")) continue;
        if (str_eq(tok, "..")) { if (np > 0) np--; continue; }
        parts[np++] = tok;
    }
    Str out; str_init(&out);
    if (raiz[0]) { str_adds(&out, raiz); str_addc(&out, '/'); }
    else str_addc(&out, '/');
    for (int i = 0; i < np; i++) {
        if (i) str_addc(&out, '/');
        str_adds(&out, parts[i]);
    }
    free(tmp);
    return str_steal(&out);
}

static char *ws_path(const char *cwd, const char *rel, ToolResult *res, const char *what)
{
    char *bruto = tools_resolve(cwd, (rel && *rel) ? rel : ".");
    if (!bruto) { err_out(res, "Erro: caminho invalido para %s.", what); return NULL; }
    char *p = path_normalize(bruto);
    free(bruto);
    char *cwd_norm = path_normalize(cwd);
    size_t cl = strlen(cwd_norm);
    int dentro = ieq_prefix(p, cwd_norm) && (p[cl] == 0 || p[cl] == '/');
    if (!dentro) {
        err_out(res, "Erro: '%s' cai fora do workspace (%s).", rel ? rel : ".", cwd);
        free(p);
        free(cwd_norm);
        return NULL;
    }
    free(cwd_norm);
    return p;
}

static void ensure_parent(const char *path)
{
    char *dir = path_dirname(path);
    if (dir && *dir) dir_make(dir);
    free(dir);
}

/* ------------------------------------------------------------------ */
/* utilidades de texto                                                 */
/* ------------------------------------------------------------------ */
static char *read_all(const char *path, size_t *len)
{
    return file_read(path, len);
}

/* substitui todas (ou a primeira) ocorrencia de old por new */
static char *str_replace_all(const char *src, const char *old_s, const char *new_s, int all, int *count)
{
    size_t lo = strlen(old_s);
    if (lo == 0) return NULL;
    Str out; str_init(&out);
    const char *p = src;
    int n = 0;
    for (;;) {
        const char *hit = strstr(p, old_s);
        if (!hit) { str_adds(&out, p); break; }
        str_add(&out, p, (size_t)(hit - p));
        str_adds(&out, new_s);
        n++;
        p = hit + lo;
        if (!all) { str_adds(&out, p); break; }
    }
    if (count) *count = n;
    if (n == 0) { str_free(&out); return NULL; }
    return str_steal(&out);
}

/* glob simples: '*' e '?' (case-insensitive no Windows) */
static int glob_match(const char *pat, const char *s)
{
    while (*pat) {
        if (*pat == '*') {
            while (*pat == '*') pat++;
            if (!*pat) return 1;
            for (const char *t = s; ; t++) {
                if (glob_match(pat, t)) return 1;
                if (!*t) return 0;
            }
        }
        if (!*s) return 0;
        if (*pat != '?') {
#ifdef _WIN32
            if (tolower((unsigned char)*pat) != tolower((unsigned char)*s)) return 0;
#else
            if (*pat != *s) return 0;
#endif
        }
        pat++; s++;
    }
    return *s == 0;
}

/* ------------------------------------------------------------------ */
/* walk generico                                                       */
/* ------------------------------------------------------------------ */
typedef struct {
    Str  *out;
    const char *cwd;
    int   depth_max;
    int   count;
    int   max_results;
    const char *needle;   /* search */
    const char *glob;     /* glob */
    int   tree;           /* structure */
} Walk;

static int skip_dir(const char *name)
{
    return str_eq(name, ".git") || str_eq(name, "node_modules") || str_eq(name, "build") ||
           str_eq(name, "dist") || str_eq(name, ".deltaCore");
}

static void walk_dir(Walk *w, const char *dir, int depth)
{
    if (w->count >= w->max_results || depth > w->depth_max) return;
    size_t n = 0;
    char **list = dir_list(dir, &n);
    if (!list) return;
    for (size_t i = 0; i < n && w->count < w->max_results; i++) {
        char *full = path_join(dir, list[i]);
        int isdir = path_is_dir(full);
        if (isdir) {
            if (w->tree) {
                str_addf(w->out, "%*s%s/\n", depth * 2, "", list[i]);
                w->count++;
                if (!skip_dir(list[i])) walk_dir(w, full, depth + 1);
            } else if (!skip_dir(list[i])) {
                walk_dir(w, full, depth + 1);
            }
        } else if (w->tree) {
            size_t len = 0;
            char *tmp = file_read(full, &len);
            free(tmp);
            str_addf(w->out, "%*s%s  (%lu bytes)\n", depth * 2, "", list[i], (unsigned long)len);
            w->count++;
        } else if (w->glob) {
            char *rel = full;
            char *base = path_basename(full);
            if (glob_match(w->glob, base) || glob_match(w->glob, rel)) {
                str_addf(w->out, "%s\n", full);
                w->count++;
            }
            free(base);
        } else if (w->needle && *w->needle) {
            size_t len = 0;
            char *text = file_read(full, &len);
            if (text) {
                int line = 1;
                size_t k = 0;
                while (k < len && w->count < w->max_results) {
                    size_t eol = k;
                    while (eol < len && text[eol] != '\n') eol++;
                    if (eol - k < 2000) {
                        char save = text[eol];
                        text[eol] = 0;
                        if (strstr(text + k, w->needle)) {
                            str_addf(w->out, "%s:%d: %s\n", full, line, text + k);
                            w->count++;
                        }
                        text[eol] = save;
                    }
                    line++;
                    k = eol + 1;
                }
                free(text);
            }
        }
        free(full);
    }
    dir_list_free(list, n);
}

/* ------------------------------------------------------------------ */
/* fs                                                                  */
/* ------------------------------------------------------------------ */
static int fs_op(JValue *a, const char *cwd, ToolResult *res)
{
    const char *op = arg_s(a, "op");
    if (!*op) { err_out(res, "Erro: 'op' e obrigatorio (list|read|write|append|replace|mkdir|delete|search|glob)."); return 0; }

    if (str_eq(op, "glob") || str_eq(op, "search")) {
        Walk w;
        memset(&w, 0, sizeof w);
        w.out = &res->output;
        w.cwd = cwd;
        w.depth_max = arg_i(a, "max_depth", 12);
        w.max_results = arg_i(a, "max_results", 200);
        if (w.max_results <= 0) w.max_results = 200;
        w.glob = str_eq(op, "glob") ? arg_s(a, "pattern") : NULL;
        w.needle = str_eq(op, "search") ? arg_s(a, "query") : NULL;
        if (w.glob && !*w.glob) w.glob = "*";
        if (w.needle && !*w.needle) { err_out(res, "Erro: 'query' e obrigatorio para search."); return 0; }
        char *base = ws_path(cwd, arg_s(a, "relative_path"), res, "search");
        if (!base) return 0;
        walk_dir(&w, base, 0);
        if (w.count == 0) str_adds(&res->output, "(nada encontrado)\n");
        free(base);
        return 1;
    }

    char *path = ws_path(cwd, arg_s(a, "relative_path"), res, op);
    if (!path) return 0;
    int ok = 0;

    if (str_eq(op, "list")) {
        if (path_is_dir(path)) {
            size_t n = 0;
            char **list = dir_list(path, &n);
            if (!list) { err_out(res, "Erro: nao foi possivel listar %s.", path); free(path); return 0; }
            int max = arg_i(a, "max_entries", 200);
            str_addf(&res->output, "%s/\n", path);
            for (size_t i = 0; i < n && (int)i < max; i++) {
                char *full = path_join(path, list[i]);
                int isdir = path_is_dir(full);
                if (isdir) str_addf(&res->output, "  %s/\n", list[i]);
                else {
                    size_t len = 0;
                    char *tmp = file_read(full, &len);
                    free(tmp);
                    str_addf(&res->output, "  %s  (%lu bytes)\n", list[i], (unsigned long)len);
                }
                free(full);
            }
            dir_list_free(list, n);
            ok = 1;
        } else {
            str_addf(&res->output, "%s\n", path);
            ok = 1;
        }
    } else if (str_eq(op, "read")) {
        if (path_is_dir(path)) { err_out(res, "Erro: %s e' um diretorio (use op=list).", path); free(path); return 0; }
        size_t len = 0;
        char *text = read_all(path, &len);
        if (!text) { err_out(res, "Erro: nao foi possivel ler %s.", path); free(path); return 0; }
        int max = arg_i(a, "max_chars", 50000);
        if (max <= 0 || max > EXT_MAX_OUT) max = EXT_MAX_OUT;
        size_t take = len < (size_t)max ? len : (size_t)max;
        str_add(&res->output, text, take);
        if (take < len) str_addf(&res->output, "\n[truncado em %d caracteres]\n", max);
        free(text);
        ok = 1;
    } else if (str_eq(op, "write") || str_eq(op, "append")) {
        const char *content = arg_s(a, "content");
        ensure_parent(path);
        if (str_eq(op, "append") && file_exists(path)) {
            size_t len = 0;
            char *before = read_all(path, &len);
            Str all; str_init(&all);
            if (before) str_add(&all, before, len);
            str_adds(&all, content);
            ok = file_write(path, all.data ? all.data : "", all.len);
            str_free(&all);
            free(before);
        } else {
            ok = file_write(path, content, strlen(content));
        }
        if (!ok) { err_out(res, "Erro: nao foi possivel escrever %s.", path); }
        else str_addf(&res->output, "%s %s (%lu bytes)\n", str_eq(op, "append") ? "Appended:" : "Written:", path, (unsigned long)strlen(content));
    } else if (str_eq(op, "replace")) {
        const char *old_s = arg_s(a, "old_text");
        const char *new_s = arg_s(a, "new_text");
        if (!*old_s) { err_out(res, "Erro: 'old_text' e' obrigatorio."); free(path); return 0; }
        size_t len = 0;
        char *before = read_all(path, &len);
        if (!before) { err_out(res, "Erro: nao foi possivel ler %s.", path); free(path); return 0; }
        int n = 0;
        char *updated = str_replace_all(before, old_s, new_s, arg_b(a, "replace_all", 1), &n);
        if (!updated) {
            err_out(res, "Erro: old_text nao encontrado em %s.", path);
        } else if (!file_write(path, updated, strlen(updated))) {
            err_out(res, "Erro: nao foi possivel gravar %s.", path);
        } else {
            str_addf(&res->output, "Replaced %d ocorrencia(s) em %s.\n", n, path);
            diff_lines(before, updated, &res->output, 2);
            ok = 1;
        }
        free(updated); free(before);
    } else if (str_eq(op, "mkdir")) {
        ok = dir_make(path);
        if (ok) str_addf(&res->output, "Directory created: %s\n", path);
        else err_out(res, "Erro: nao foi possivel criar %s.", path);
    } else if (str_eq(op, "delete")) {
        if (!file_exists(path) && !dir_exists(path)) { err_out(res, "Erro: %s nao existe.", path); free(path); return 0; }
#ifdef _WIN32
        ok = path_is_dir(path) ? RemoveDirectoryA(path) : DeleteFileA(path);
#else
        ok = path_is_dir(path) ? (rmdir(path) == 0) : (remove(path) == 0);
#endif
        if (ok) str_addf(&res->output, "Deleted: %s\n", path);
        else err_out(res, "Erro: nao foi possivel remover %s (diretorio precisa estar vazio).", path);
    } else {
        err_out(res, "Erro: operacao desconhecida de fs: %s", op);
    }
    free(path);
    return ok;
}

static const char FS_SCHEMA[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"op\":{\"type\":\"string\",\"enum\":[\"list\",\"read\",\"write\",\"append\",\"replace\",\"mkdir\",\"delete\",\"search\",\"glob\"],\"description\":\"Operacao\"},"
    "\"relative_path\":{\"type\":\"string\",\"description\":\"Caminho dentro do workspace (padrao: .)\"},"
    "\"content\":{\"type\":\"string\",\"description\":\"Conteudo para write/append\"},"
    "\"old_text\":{\"type\":\"string\",\"description\":\"Texto a trocar (replace)\"},"
    "\"new_text\":{\"type\":\"string\",\"description\":\"Texto novo (replace)\"},"
    "\"replace_all\":{\"type\":\"boolean\",\"description\":\"Troca todas as ocorrencias (padrao: true)\"},"
    "\"pattern\":{\"type\":\"string\",\"description\":\"Glob (ex.: *.c) para op=glob\"},"
    "\"query\":{\"type\":\"string\",\"description\":\"Texto procurado para op=search\"},"
    "\"max_results\":{\"type\":\"number\"},\"max_entries\":{\"type\":\"number\"},\"max_chars\":{\"type\":\"number\"}},"
    "\"required\":[\"op\"]}";

/* ------------------------------------------------------------------ */
/* git                                                                 */
/* ------------------------------------------------------------------ */
static int git_run(const char *cwd, const char *args, int timeout, Str *out, int *code)
{
    Str cmd; str_init(&cmd);
    str_addf(&cmd, "git -C \"%s\" %s", cwd, args);
    int ok = tools_exec(cwd, cmd.data, timeout, out, code);
    str_free(&cmd);
    return ok;
}

static int git_op(JValue *a, const char *cwd, ToolResult *res)
{
    const char *op = arg_s(a, "op");
    if (!*op) { err_out(res, "Erro: 'op' e obrigatorio (status|diff|log|checkpoint)."); return 0; }

    if (str_eq(op, "status")) return git_run(cwd, "status --short --branch", 60, &res->output, &res->exit_code);
    if (str_eq(op, "diff")) return git_run(cwd, "diff --stat --patch", 120, &res->output, &res->exit_code);
    if (str_eq(op, "log")) {
        int n = arg_i(a, "n", 10);
        if (n <= 0) n = 10;
        if (n > 500) n = 500;
        char args[128];
        snprintf(args, sizeof args, "log -n %d --oneline --decorate", n);
        return git_run(cwd, args, 60, &res->output, &res->exit_code);
    }
    if (str_eq(op, "checkpoint")) {
        const char *msg = arg_s(a, "message");
        if (!*msg) msg = "Delta checkpoint";
        char args[2048];
        snprintf(args, sizeof args, "git -C \"%s\" add -A && git -C \"%s\" commit -m \"%s\"", cwd, cwd, msg);
        int code = 0;
        int ok = tools_exec(cwd, args, 120, &res->output, &code);
        res->exit_code = code;
        if (code != 0) res->is_error = 1;
        return ok;
    }
    err_out(res, "Erro: operacao desconhecida de git: %s", op);
    return 0;
}

static const char GIT_SCHEMA[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"op\":{\"type\":\"string\",\"enum\":[\"status\",\"diff\",\"log\",\"checkpoint\"]},"
    "\"n\":{\"type\":\"number\",\"description\":\"Quantidade de commits (log)\"},"
    "\"message\":{\"type\":\"string\",\"description\":\"Mensagem do commit (checkpoint)\"}},"
    "\"required\":[\"op\"]}";

/* ------------------------------------------------------------------ */
/* shell (com jobs em background)                                      */
/* ------------------------------------------------------------------ */
#define MAX_JOBS 16

typedef struct {
    int    used;
    int    id;
    char  *cmd;
    char  *outpath;
    int    done;
    int    cancelled;
    int    exit_code;
    uint64_t t0;
#ifdef _WIN32
    HANDLE proc;
#else
    pid_t  pid;
#endif
} Job;

static Job g_jobs[MAX_JOBS];
static int g_job_seq = 0;

static char *jobs_dir(void)
{
    char *home = home_get();
    char *dir = path_join(delta_home(), "jobs");
    dir_make(dir);
    free(home);
    return dir;
}

static const char *path_dup_cwd(const char *cwd)
{
    return cwd ? cwd : ".";
}

static Job *job_start(const char *cwd, const char *command, ToolResult *res)
{
    int slot = -1;
    for (int i = 0; i < MAX_JOBS; i++) if (!g_jobs[i].used) { slot = i; break; }
    if (slot < 0) { err_out(res, "Erro: limite de %d jobs simultaneos.", MAX_JOBS); return NULL; }

    Job *j = &g_jobs[slot];
    memset(j, 0, sizeof *j);
    j->used = 1;
    j->id = ++g_job_seq;
    j->cmd = str_dup(command);
    j->t0 = now_ms();

    char *dir = jobs_dir();
    char nome[64];
    snprintf(nome, sizeof nome, "job-%d.log", j->id);
    j->outpath = path_join(dir, nome);
    free(dir);

#ifdef _WIN32
    Str line; str_init(&line);
    str_addf(&line, "cmd /c %s > \"%s\" 2>&1", command, j->outpath);
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    memset(&pi, 0, sizeof pi);
    char *dirbuf = str_dup(path_dup_cwd(cwd));
    BOOL criado = CreateProcessA(NULL, line.data, NULL, NULL, FALSE,
                                 CREATE_NO_WINDOW, NULL, dirbuf, &si, &pi);
    free(dirbuf);
    str_free(&line);
    if (!criado) { err_out(res, "Erro: falha ao iniciar o job (codigo %lu).", (unsigned long)GetLastError()); j->used = 0; return NULL; }
    CloseHandle(pi.hThread);
    j->proc = pi.hProcess;
#else
    pid_t pid = fork();
    if (pid == 0) {
        FILE *f = freopen(j->outpath, "w", stdout);
        (void)f;
        freopen(j->outpath, "a", stderr);
        if (cwd) chdir(cwd);
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        _exit(127);
    }
    if (pid < 0) { err_out(res, "Erro: falha ao iniciar o job."); j->used = 0; return NULL; }
    j->pid = pid;
#endif
    str_addf(&res->output, "Job %d started: %s\nlog: %s\n", j->id, command, j->outpath);
    return j;
}

static int job_running(Job *j)
{
#ifdef _WIN32
    if (!j->proc) return 0;
    DWORD st = WaitForSingleObject(j->proc, 0);
    if (st == WAIT_OBJECT_0) {
        DWORD code = 0;
        GetExitCodeProcess(j->proc, &code);
        j->exit_code = (int)code;
        j->done = 1;
        CloseHandle(j->proc);
        j->proc = NULL;
        return 0;
    }
    return 1;
#else
    if (j->pid <= 0) return 0;
    int st = 0;
    pid_t w = waitpid(j->pid, &st, WNOHANG);
    if (w == j->pid) { j->done = 1; j->exit_code = WIFEXITED(st) ? WEXITSTATUS(st) : 1; j->pid = 0; return 0; }
    return 1;
#endif
}

static int read_tail(const char *path, int max_lines, Str *out)
{
    size_t len = 0;
    char *text = file_read(path, &len);
    if (!text) return 0;
    /* conta linhas */
    int total = 1;
    for (size_t i = 0; i < len; i++) if (text[i] == '\n') total++;
    int skip = total > max_lines ? total - max_lines : 0;
    int line = 0;
    size_t i = 0;
    while (i < len) {
        size_t eol = i;
        while (eol < len && text[eol] != '\n') eol++;
        if (line >= skip) { str_add(out, text + i, eol - i + (eol < len ? 1 : 0)); }
        line++;
        i = eol + 1;
    }
    free(text);
    return 1;
}

static int shell_op(JValue *a, const char *cwd, ToolResult *res)
{
    const char *op = arg_s(a, "op");
    if (!*op) { err_out(res, "Erro: 'op' e obrigatorio (run|start|list|tail|cancel)."); return 0; }

    if (str_eq(op, "run")) {
        const char *cmd = arg_s(a, "command");
        if (!*cmd) { err_out(res, "Erro: 'command' e' obrigatorio."); return 0; }
        int timeout = arg_i(a, "timeout_seconds", 120);
        uint64_t t0 = now_ms();
        int code = 0;
        if (!tools_exec(cwd, cmd, timeout, &res->output, &code)) {
            err_out(res, "Erro: falha ao executar o comando.");
            return 0;
        }
        res->exit_code = code;
        res->wall_seconds = (double)(now_ms() - t0) / 1000.0;
        if (code != 0) { res->is_error = 1; str_addf(&res->output, "\n[exit %d]", code); }
        return 1;
    }
    if (str_eq(op, "start")) {
        const char *cmd = arg_s(a, "command");
        if (!*cmd) { err_out(res, "Erro: 'command' e' obrigatorio."); return 0; }
        return job_start(cwd, cmd, res) != NULL;
    }
    if (str_eq(op, "list")) {
        int qualquer = 0;
        for (int i = 0; i < MAX_JOBS; i++) {
            Job *j = &g_jobs[i];
            if (!j->used) continue;
            int vivo = job_running(j);
            if (!vivo && !j->done) j->done = 1;
            const char *estado = vivo ? "running"
                                    : (j->cancelled ? "cancelled" : (j->exit_code == 0 ? "done" : "failed"));
            str_addf(&res->output, "%d  %-9s  %s\n", j->id, estado, j->cmd);
            qualquer = 1;
        }
        if (!qualquer) str_adds(&res->output, "(nenhum job)\n");
        return 1;
    }
    if (str_eq(op, "tail") || str_eq(op, "cancel")) {
        int id = arg_i(a, "job_id", 0);
        Job *j = NULL;
        for (int i = 0; i < MAX_JOBS; i++) if (g_jobs[i].used && g_jobs[i].id == id) { j = &g_jobs[i]; break; }
        if (!j) { err_out(res, "Erro: job %d nao existe (use op=list).", id); return 0; }
        if (str_eq(op, "tail")) {
            job_running(j);
            if (!read_tail(j->outpath, arg_i(a, "max_lines", 100), &res->output)) {
                err_out(res, "Erro: nao foi possivel ler o log do job %d.", id);
                return 0;
            }
            tools_oem_to_utf8(&res->output);
            return 1;
        }
        if (job_running(j)) {
#ifdef _WIN32
            TerminateProcess(j->proc, 1);
            CloseHandle(j->proc);
            j->proc = NULL;
#else
            kill(j->pid, SIGKILL);
            waitpid(j->pid, NULL, 0);
            j->pid = 0;
#endif
            j->done = 1;
            j->cancelled = 1;
            j->exit_code = -1;
            str_addf(&res->output, "Job %d cancelled.\n", id);
        } else {
            str_addf(&res->output, "Job %d already finished (exit %d).\n", id, j->exit_code);
        }
        return 1;
    }
    err_out(res, "Erro: operacao desconhecida de shell: %s", op);
    return 0;
}

static const char SHELL_SCHEMA[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"op\":{\"type\":\"string\",\"enum\":[\"run\",\"start\",\"list\",\"tail\",\"cancel\"]},"
    "\"command\":{\"type\":\"string\"},"
    "\"timeout_seconds\":{\"type\":\"number\"},"
    "\"job_id\":{\"type\":\"number\"},"
    "\"max_lines\":{\"type\":\"number\"}},"
    "\"required\":[\"op\"]}";

/* ------------------------------------------------------------------ */
/* web                                                                 */
/* ------------------------------------------------------------------ */
static void json_dump(Str *out, JValue *v)
{
    if (!v) { str_adds(out, "null"); return; }
    switch (v->kind) {
        case J_NULL:  str_adds(out, "null"); break;
        case J_BOOL:  str_adds(out, v->boolean ? "true" : "false"); break;
        case J_NUM:   str_addf(out, "%.17g", v->num); break;
        case J_STR:   json_write_string(out, v->str ? v->str : "", v->str ? strlen(v->str) : 0); break;
        case J_ARR:
            str_addc(out, '[');
            for (size_t i = 0; i < v->count; i++) { if (i) str_addc(out, ','); json_dump(out, v->items[i]); }
            str_addc(out, ']');
            break;
        case J_OBJ:
            str_addc(out, '{');
            for (size_t i = 0; i < v->count; i++) {
                if (i) str_addc(out, ',');
                json_write_string(out, v->pairs[i].key, strlen(v->pairs[i].key));
                str_addc(out, ':');
                json_dump(out, v->pairs[i].val);
            }
            str_addc(out, '}');
            break;
    }
}

static int ci_starts(const char *s, const char *p);
static void corpo_utf8(const char *ctype, const char *body, Str *out);
static void texto_readable(const char *txt, Str *out);

/* ------------------------------------------------------------------ */
/* sessao HTTP do micro navegador: cookies, URL final e cache           */
/* ------------------------------------------------------------------ */
#ifdef _WIN32
#  define ci_ncmp _strnicmp
#else
#  define ci_ncmp strncasecmp
#endif

static Str  g_cookie;                 /* "a=1; b=2" */
static char g_last_url[1024] = { 0 }; /* URL depois dos redirects */
static char g_last_ctype[256] = { 0 }; /* Content-Type da ultima resposta */
static char g_last_session[256] = { 0 }; /* Mcp-Session-Id da ultima resposta */
static int  g_cookie_pronto = 0;

static void cookie_init(void)
{
    if (!g_cookie_pronto) { str_init(&g_cookie); g_cookie_pronto = 1; }
}

/* guarda/atualiza "name=value" no jar (ignora atributos tipo Path, Expires) */
static void cookie_set(const char *host, const char *set_cookie)
{
    cookie_init();
    const char *fim = strchr(set_cookie, ';');
    size_t n = fim ? (size_t)(fim - set_cookie) : strlen(set_cookie);
    while (n && isspace((unsigned char)set_cookie[n - 1])) n--;
    const char *eq = memchr(set_cookie, '=', n);
    if (!eq || n > 900) return;
    size_t nn = (size_t)(eq - set_cookie);
    if (nn >= 256) return;

    /* entrada: "<host>|<name>=<value>" */
    char prefixo[320];
    snprintf(prefixo, sizeof prefixo, "%.*s|%.*s", (int)strlen(host), host, (int)nn, set_cookie);
    size_t pl = strlen(prefixo);

    Str novo; str_init(&novo);
    const char *p = g_cookie.data ? g_cookie.data : "";
    while (*p) {
        const char *virg = strstr(p, "; ");
        size_t ll = virg ? (size_t)(virg - p) : strlen(p);
        int mesma_chave = (ll > pl && memcmp(p, prefixo, pl) == 0 && p[pl] == '=');
        if (!mesma_chave) {
            if (novo.len) str_adds(&novo, "; ");
            str_add(&novo, p, ll);
        }
        if (!virg) break;
        p = virg + 2;
    }
    if (novo.len) str_adds(&novo, "; ");
    str_addf(&novo, "%s=%.*s", prefixo, (int)(n - nn - 1), set_cookie + nn + 1);
    str_free(&g_cookie);
    g_cookie = novo;
    if (getenv("DELTA_BROWSER_DEBUG")) fprintf(stderr, "[cookie] jar: %s\n", g_cookie.data);
}

/* manda so' os cookies do host atual (ou de um dominio pai dele) */
static void cookie_header(const char *host, Str *h)
{
    cookie_init();
    if (!g_cookie.len || !host) return;
    Str linha; str_init(&linha);
    const char *p = g_cookie.data;
    while (*p) {
        const char *virg = strstr(p, "; ");
        size_t ll = virg ? (size_t)(virg - p) : strlen(p);
        const char *barra = memchr(p, '|', ll);
        if (barra) {
            size_t hl = (size_t)(barra - p);
            int casa = (hl == strlen(host) && !ci_ncmp(p, host, hl));
            if (!casa && strlen(host) > hl && host[strlen(host) - hl - 1] == '.' &&
                !ci_ncmp(host + strlen(host) - hl, p, hl))
                casa = 1;
            if (casa) {
                if (linha.len) str_adds(&linha, "; ");
                str_add(&linha, barra + 1, ll - hl - 1);
            }
        }
        if (!virg) break;
        p = virg + 2;
    }
    if (linha.len) str_addf(h, "Cookie: %s\r\n", linha.data);
    str_free(&linha);
}

static void cookie_scan(const char *headers, const char *host)
{
    if (!headers) return;
    const char *p = headers;
    while ((p = strstr(p, "\r\n")) != NULL) {
        const char *linha = p + 2;
        if (ci_starts(linha, "content-type:")) {
            const char *v = linha + 13;
            while (*v == ' ') v++;
            size_t k = 0;
            while (*v && *v != '\r' && *v != '\n' && k + 1 < sizeof g_last_ctype) g_last_ctype[k++] = *v++;
            g_last_ctype[k] = 0;
        }
        if (ci_starts(linha, "mcp-session-id:")) {
            const char *v = linha + 15;
            while (*v == ' ') v++;
            size_t k = 0;
            while (*v && *v != '\r' && *v != '\n' && k + 1 < sizeof g_last_session) g_last_session[k++] = *v++;
            g_last_session[k] = 0;
        }
        if (!ci_starts(linha, "set-cookie:")) { p = linha; continue; }
        const char *val = linha + 11;
        while (*val == ' ') val++;
        cookie_set(host, val);
        p = val;
    }
}

static int http_do(const char *method, const char *url, const char *headers,
                   const char *body, int timeout_s, int max_chars, Str *out)
{
#ifdef _WIN32
    wchar_t wurl[2048], wmethod[16];
    if (!MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, 2048)) return 0;
    if (!MultiByteToWideChar(CP_UTF8, 0, method, -1, wmethod, 16)) return 0;

    URL_COMPONENTS uc;
    memset(&uc, 0, sizeof uc);
    uc.dwStructSize = sizeof uc;
    wchar_t host[512] = { 0 }, pathbuf[2048] = { 0 };
    uc.lpszHostName = host; uc.dwHostNameLength = 512;
    uc.lpszUrlPath = pathbuf; uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) return 0;

    HINTERNET session = WinHttpOpen(L"delta-agent/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return 0;
    int timeout = timeout_s > 0 ? timeout_s * 1000 : 30000;
    WinHttpSetTimeouts(session, timeout, timeout, timeout, timeout);
    char host_a[256] = { 0 };
    WideCharToMultiByte(CP_UTF8, 0, host, -1, host_a, sizeof host_a, NULL, NULL);
    HINTERNET conn = WinHttpConnect(session, host, uc.nPort, 0);
    if (!conn) { WinHttpCloseHandle(session); return 0; }
    DWORD flags = uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(conn, wmethod, pathbuf, NULL, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req) { WinHttpCloseHandle(conn); WinHttpCloseHandle(session); return 0; }

    /* cabecalhos do chamador + cookies da sessao */
    Str hdrs_tot; str_init(&hdrs_tot);
    if (headers && *headers) str_adds(&hdrs_tot, headers);
    cookie_header(host_a, &hdrs_tot);
    wchar_t wheaders[2048];
    if (hdrs_tot.len && MultiByteToWideChar(CP_UTF8, 0, hdrs_tot.data, -1, wheaders, 2048)) {
        WinHttpAddRequestHeaders(req, wheaders, (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }
    str_free(&hdrs_tot);
    BOOL enviado = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                      (LPVOID)(body && *body ? body : NULL),
                                      (DWORD)(body ? strlen(body) : 0),
                                      (DWORD)(body ? strlen(body) : 0), 0);
    int ok = 0;
    if (enviado && WinHttpReceiveResponse(req, NULL)) {
        DWORD status = 0, sz = sizeof status;
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
        str_addf(out, "HTTP %lu\n", (unsigned long)status);

        /* cabecalhos crus: pega Set-Cookie e a URL final (depois dos redirects).
           WINHTTP_QUERY_RAW_HEADERS_CRLF devolve WCHAR (tamanho em caracteres). */
        DWORD hlen = 0;
        WinHttpQueryHeaders(req, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                            NULL, &hlen, WINHTTP_NO_HEADER_INDEX);
        if (hlen) {
            wchar_t *wh = (wchar_t *)malloc(((size_t)hlen + 2) * sizeof(wchar_t));
            if (wh) {
                DWORD hsz = hlen;
                if (WinHttpQueryHeaders(req, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                                        wh, &hsz, WINHTTP_NO_HEADER_INDEX)) {
                    int precisa = WideCharToMultiByte(CP_UTF8, 0, wh, -1, NULL, 0, NULL, NULL);
                    if (precisa > 0) {
                        char *u = (char *)malloc((size_t)precisa + 1);
                        if (u) {
                            WideCharToMultiByte(CP_UTF8, 0, wh, -1, u, precisa, NULL, NULL);
                            u[precisa] = 0;
                            cookie_scan(u, host_a);
                            free(u);
                        }
                    }
                }
                free(wh);
            }
        }
        {
            wchar_t wfinal[2048];
            DWORD ulen = sizeof wfinal;
            if (WinHttpQueryOption(req, WINHTTP_OPTION_URL, wfinal, &ulen))
                WideCharToMultiByte(CP_UTF8, 0, wfinal, -1, g_last_url, sizeof g_last_url, NULL, NULL);
            else
                snprintf(g_last_url, sizeof g_last_url, "%s", url);
        }
        DWORD avail = 0;
        do {
            avail = 0;
            if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
            char *buf = (char *)malloc(avail + 1);
            if (!buf) break;
            DWORD got = 0;
            if (WinHttpReadData(req, buf, avail, &got) && got > 0) {
                buf[got] = 0;
                if (out->len < (size_t)max_chars) {
                    size_t sobra = (size_t)max_chars - out->len;
                    str_add(out, buf, got < sobra ? got : sobra);
                }
            }
            free(buf);
        } while (avail > 0);
        ok = 1;
    } else {
        str_addf(out, "Erro: falha na requisicao (codigo %lu).\n", (unsigned long)GetLastError());
    }
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return ok;
#else
    (void)method; (void)url; (void)headers; (void)body; (void)timeout_s; (void)max_chars; (void)out;
    return 0;
#endif
}

/* tira tags/entidades para leitura de pagina */
static void html_to_text(const char *html, Str *out)
{
    int in_tag = 0, in_skip = 0;
    size_t i = 0;
    while (html[i]) {
        char c = html[i];
        if (c == '<') {
            if (!strncmp(html + i, "<script", 7) || !strncmp(html + i, "<style", 6) ||
                !strncmp(html + i, "<title", 6)) in_skip = 1;
            if (!strncmp(html + i, "</script", 8) || !strncmp(html + i, "</style", 7) ||
                !strncmp(html + i, "</title", 7)) { in_skip = 0; }
            /* blocos viram linha nova; </a> vira espaco (links nao colam) */
            static const char *blocos[] = {
                "<br", "<p", "</p", "<div", "</div", "<li", "</li", "<ul", "</ul", "<ol", "</ol",
                "<nav", "</nav", "<header", "</header", "<footer", "</footer", "<section", "</section",
                "<article", "</article", "<h1", "<h2", "<h3", "<h4", "<h5", "<h6",
                "</h1", "</h2", "</h3", "</h4", "</h5", "</h6",
                "<tr", "</tr", "<table", "</table", "<form", "</form", NULL
            };
            for (int k = 0; blocos[k]; k++) {
                if (!ci_ncmp(html + i, blocos[k], strlen(blocos[k]))) { str_addc(out, '\n'); break; }
            }
            if (!ci_ncmp(html + i, "</a", 3)) str_addc(out, ' ');
            in_tag = 1;
            i++;
            continue;
        }
        if (c == '>') { in_tag = 0; i++; continue; }
        if (in_tag || in_skip) { i++; continue; }

        if (c == '&') {
            struct { const char *nome; const char *txt; } ents[] = {
                { "&amp;", "&" }, { "&lt;", "<" }, { "&gt;", ">" }, { "&quot;", "\"" },
                { "&apos;", "'" }, { "&nbsp;", " " }, { "&copy;", "\xC2\xA9" },
                { "&reg;", "\xC2\xAE" }, { "&trade;", "\xE2\x84\xA2" },
                { "&hellip;", "\xE2\x80\xA6" }, { "&mdash;", "\xE2\x80\x94" },
                { "&ndash;", "\xE2\x80\x93" }, { "&ldquo;", "\xE2\x80\x9C" },
                { "&rdquo;", "\xE2\x80\x9D" }, { "&lsquo;", "\xE2\x80\x98" },
                { "&rsquo;", "\xE2\x80\x99" }, { NULL, NULL }
            };
            int achou = 0;
            for (int k = 0; ents[k].nome; k++) {
                size_t l = strlen(ents[k].nome);
                if (!ci_ncmp(html + i, ents[k].nome, l)) { str_adds(out, ents[k].txt); i += l; achou = 1; break; }
            }
            if (achou) continue;
            if (html[i + 1] == '#') {
                const char *fim = strchr(html + i, ';');
                if (fim && (size_t)(fim - (html + i)) < 12) {
                    unsigned long v = 0;
                    if (html[i + 2] == 'x' || html[i + 2] == 'X') v = strtoul(html + i + 3, NULL, 16);
                    else v = strtoul(html + i + 2, NULL, 10);
                    if (v) {
                        char enc[4];
                        int n = utf8_encode((uint32_t)v, enc);
                        if (n > 0) str_add(out, enc, (size_t)n);
                    }
                    i = (size_t)(fim - html) + 1;
                    continue;
                }
            }
        }
        if (c == '\n' || c == '\r' || c == '\t') { str_addc(out, ' '); i++; continue; }
        str_addc(out, c);
        i++;
    }

    /* colapsa espacos, preserva quebras de linha */
    Str limpo; str_init(&limpo);
    int espaco = 0, linha = 0, quebras = 0;
    for (size_t k = 0; k < out->len; k++) {
        char ch = out->data[k];
        if (ch == '\n') {
            if (linha) { str_addc(&limpo, '\n'); linha = 0; quebras = 0; }
            else if (quebras < 1) { if (limpo.len) str_addc(&limpo, '\n'); quebras++; }
            espaco = 0;
        } else if (ch == ' ') {
            if (linha && !espaco) { str_addc(&limpo, ' '); espaco = 1; }
        } else {
            str_addc(&limpo, ch);
            linha = 1;
            espaco = 0;
            quebras = 0;
        }
    }
    str_free(out);
    *out = limpo;
}

/* ------------------------------------------------------------------ */
/* micro navegador: HTML -> texto, links numerados, historico          */
/* ------------------------------------------------------------------ */
#define NB_MAX_LINKS 80
#define NB_MAX_HIST  8

typedef struct { char href[512]; char texto[140]; } NbLink;

static NbLink g_nb_links[NB_MAX_LINKS];
static int    g_nb_nlinks = 0;
static char   g_nb_url[1024] = { 0 };
static char   g_nb_title[256] = { 0 };

static int ci_starts(const char *s, const char *p)
{
    while (*p) {
        if (!*s) return 0;
        if (tolower((unsigned char)*s) != tolower((unsigned char)*p)) return 0;
        s++; p++;
    }
    return 1;
}

static void trim_cpy(char *dst, size_t n, const char *src, size_t len)
{
    while (len && isspace((unsigned char)*src)) { src++; len--; }
    while (len && isspace((unsigned char)src[len - 1])) len--;
    if (len >= n) len = n - 1;
    memcpy(dst, src, len);
    dst[len] = 0;
}

/* href relativo -> absoluto (o basico de um navegador) */
static char *url_resolve(const char *base, const char *href)
{
    char buf[2048];
    if (ci_starts(href, "http://") || ci_starts(href, "https://")) {
        snprintf(buf, sizeof buf, "%s", href);
        return str_dup(buf);
    }
    if (href[0] == '/' && href[1] == '/') {
        const char *esq = strstr(base, "://");
        size_t n = esq ? (size_t)(esq - base) : 4;
        snprintf(buf, sizeof buf, "%.*s:%s", (int)n, base, href);
        return str_dup(buf);
    }
    if (href[0] == '/') {
        const char *esq = strstr(base, "://");
        const char *barra = esq ? strchr(esq + 3, '/') : NULL;
        size_t n = barra ? (size_t)(barra - base) : strlen(base);
        snprintf(buf, sizeof buf, "%.*s%s", (int)n, base, href);
        return str_dup(buf);
    }
    if (href[0] == '#') return str_dup(base);
    const char *ult = strrchr(base, '/');
    size_t n = ult ? (size_t)(ult - base + 1) : strlen(base);
    snprintf(buf, sizeof buf, "%.*s%s", (int)n, base, href);
    return str_dup(buf);
}

static void nb_titulo(const char *html)
{
    g_nb_title[0] = 0;
    for (const char *p = html; (p = strstr(p, "<")) != NULL; p++) {
        if (!ci_starts(p, "<title")) continue;
        const char *fim = strchr(p, '>');
        if (!fim) break;
        const char *fecha = strstr(fim, "</");
        if (!fecha) break;
        trim_cpy(g_nb_title, sizeof g_nb_title, fim + 1, (size_t)(fecha - fim - 1));
        return;
    }
}

static void nb_extrai_links(const char *html)
{
    g_nb_nlinks = 0;
    const char *p = html;
    while (*p && g_nb_nlinks < NB_MAX_LINKS) {
        const char *a = strstr(p, "<a");
        if (!a) break;
        if (!ci_starts(a, "<a ") && !ci_starts(a, "<a>") && !ci_starts(a, "<a\t")) { p = a + 2; continue; }
        const char *fim_tag = strchr(a, '>');
        if (!fim_tag) break;
        /* href="..." dentro da tag */
        char href[512] = { 0 };
        for (const char *q = a; q < fim_tag; q++) {
            if (ci_starts(q, "href")) {
                const char *eq = strchr(q, '=');
                if (!eq || eq > fim_tag) break;
                const char *v = eq + 1;
                while (*v == ' ' || *v == '\t') v++;
                char quote = (*v == '"' || *v == '\'') ? *v : 0;
                if (quote) v++;
                const char *f = v;
                while (*f && *f != quote && (quote || (!isspace((unsigned char)*f) && *f != '>'))) f++;
                trim_cpy(href, sizeof href, v, (size_t)(f - v));
                break;
            }
        }
        if (href[0] && !ci_starts(href, "mailto:") && !ci_starts(href, "javascript:") && href[0] != '#') {
            /* texto do link, sem tags */
            Str t; str_init(&t);
            int in_tag = 0;
            for (const char *q = fim_tag + 1; *q && !ci_starts(q, "</a"); q++) {
                if (*q == '<') { in_tag = 1; continue; }
                if (*q == '>') { in_tag = 0; continue; }
                if (!in_tag && t.len < 200) str_addc(&t, *q == '\n' ? ' ' : *q);
            }
            trim_cpy(g_nb_links[g_nb_nlinks].href, sizeof g_nb_links[0].href, href, strlen(href));
            trim_cpy(g_nb_links[g_nb_nlinks].texto, sizeof g_nb_links[0].texto,
                     t.data ? t.data : "", t.len);
            str_free(&t);
            g_nb_nlinks++;
        }
        p = fim_tag + 1;
    }
}

static char  g_nb_view[NB_MAX_LINKS][512];   /* links absolutos na ordem exibida */
static int   g_nb_nview = 0;
static Str   g_nb_text;                      /* texto da pagina atual */
static int   g_nb_text_pronto = 0;
static Str   g_nb_html;                      /* html da pagina atual (para o back) */
static int   g_nb_html_pronto = 0;
typedef struct { char url[1024]; char *html; } NbCache;
static NbCache g_nb_cache[NB_MAX_HIST];
static int   g_nb_ncache = 0;

static void nb_text_init(void)
{
    if (!g_nb_text_pronto) { str_init(&g_nb_text); g_nb_text_pronto = 1; }
    if (!g_nb_html_pronto) { str_init(&g_nb_html); g_nb_html_pronto = 1; }
}

static const char *nb_host(const char *url)
{
    const char *p = strstr(url, "://");
    return p ? p + 3 : url;
}

static int nb_mesmo_host(const char *a, const char *b)
{
    const char *ha = nb_host(a), *hb = nb_host(b);
    size_t la = strcspn(ha, "/:"), lb = strcspn(hb, "/:");
    if (la != lb) return 0;
#ifdef _WIN32
    return ci_ncmp(ha, hb, la) == 0;
#else
    return strncasecmp(ha, hb, la) == 0;
#endif
}

/* monta a lista de links (absolutos, sem repetir) e imprime */
static void nb_mostra_links(Str *out, int host_only)
{
    g_nb_nview = 0;
    for (int i = 0; i < g_nb_nlinks && g_nb_nview < NB_MAX_LINKS; i++) {
        char *abs = url_resolve(g_nb_url, g_nb_links[i].href);
        int repete = 0;
        for (int k = 0; k < g_nb_nview; k++)
            if (str_eq(g_nb_view[k], abs)) { repete = 1; break; }
        if (repete || (host_only && !nb_mesmo_host(abs, g_nb_url))) { free(abs); continue; }
        snprintf(g_nb_view[g_nb_nview], sizeof g_nb_view[0], "%s", abs);
        str_addf(out, "[%d] %s -> %s\n", ++g_nb_nview,
                 g_nb_links[i].texto[0] ? g_nb_links[i].texto : "(sem texto)", abs);
        free(abs);
    }
    if (g_nb_nview == 0) str_adds(out, "(nenhum link)\n");
}

static void nb_cache_push(const char *url, const char *html)
{
    if (g_nb_ncache == NB_MAX_HIST) {
        free(g_nb_cache[0].html);
        memmove(&g_nb_cache[0], &g_nb_cache[1], (NB_MAX_HIST - 1) * sizeof g_nb_cache[0]);
        g_nb_ncache--;
    }
    NbCache *c = &g_nb_cache[g_nb_ncache++];
    snprintf(c->url, sizeof c->url, "%s", url);
    size_t n = strlen(html);
    if (n > 200 * 1024) n = 200 * 1024;
    c->html = str_ndup(html, n);
}

static void nb_render(const char *html, const char *url, int max_chars, int readable, Str *out)
{
    snprintf(g_nb_url, sizeof g_nb_url, "%s", url ? url : "");
    nb_titulo(html);
    nb_extrai_links(html);

    nb_text_init();
    str_clear(&g_nb_text);
    html_to_text(html, &g_nb_text);

    Str visivel; str_init(&visivel);
    const char *mostrar = g_nb_text.data ? g_nb_text.data : "";
    if (readable) {
        texto_readable(mostrar, &visivel);
        mostrar = visivel.data ? visivel.data : "";
    }

    str_addf(out, "URL: %s\n", g_nb_url);
    str_addf(out, "TITLE: %s\n%s\n", g_nb_title[0] ? g_nb_title : "(sem titulo)",
             readable ? "MODE: leitura (menu/rodape fora)" : "");
    size_t lim = strlen(mostrar);
    if (max_chars > 0 && lim > (size_t)max_chars) lim = (size_t)max_chars;
    if (lim) str_add(out, mostrar, lim);
    if (lim < strlen(mostrar)) str_adds(out, "\n[texto truncado]\n");
    str_adds(out, "\nLinks:\n");
    nb_mostra_links(out, 0);
    str_adds(out, "\n[browser sem JavaScript: le HTML como texto]\n");
    str_free(&visivel);
}

static int nb_carrega(const char *method, const char *url, const char *body,
                      int timeout, int maxc, int max_chars, int readable, Str *out, ToolResult *res)
{
    if (g_nb_url[0] && g_nb_html.len) nb_cache_push(g_nb_url, g_nb_html.data);
    Str raw; str_init(&raw);
    if (!http_do(method, url, "User-Agent: delta-agent/1.0\r\nAccept: text/html,*/*\r\n",
                 body, timeout, maxc, &raw)) {
        str_free(&raw);
        err_out(res, "Erro: nao consegui abrir %s.", url);
        return 0;
    }
    char *corpo = strchr(raw.data, '\n');
    const char *bruto = corpo ? corpo + 1 : (raw.data ? raw.data : "");
    Str utf8; str_init(&utf8);
    corpo_utf8(g_last_ctype, bruto, &utf8);
    const char *final_url = g_last_url[0] ? g_last_url : url;
    nb_render(utf8.data ? utf8.data : "", final_url, max_chars, readable, out);
    nb_text_init();
    str_clear(&g_nb_html);
    str_addn(&g_nb_html, utf8.data ? utf8.data : "", 200 * 1024);
    str_free(&utf8);
    str_free(&raw);
    return 1;
}

/* query JSON -> "a=1&b=2" (URL-encode minimo) */
static void nb_query_str(JValue *q, Str *out)
{
    if (!q || q->kind != J_OBJ) return;
    for (size_t i = 0; i < q->count; i++) {
        if (out->len) str_addc(out, '&');
        for (const char *k = q->pairs[i].key; *k; k++) {
            unsigned char c = (unsigned char)*k;
            if (isalnum(c) || c == '_' || c == '-' || c == '.') str_addc(out, (char)c);
            else str_addf(out, "%%%02X", c);
        }
        str_addc(out, '=');
        const char *v = json_str(q->pairs[i].val);
        for (const char *p = v; *p; p++) {
            unsigned char c = (unsigned char)*p;
            if (isalnum(c) || c == '_' || c == '-' || c == '.' || c == '~') str_addc(out, (char)c);
            else if (c == ' ') str_addc(out, '+');
            else str_addf(out, "%%%02X", c);
        }
    }
}

static int browser_op(JValue *a, ToolResult *res)
{
    const char *action = arg_s(a, "action");
    if (!*action) action = "open";
    int timeout = arg_i(a, "timeout_seconds", 30);
    int maxc = arg_i(a, "max_response_chars", 400000);
    int max_chars = arg_i(a, "max_chars", 8000);

    if (str_eq(action, "links")) {
        if (!g_nb_url[0]) { err_out(res, "Erro: abra uma pagina antes (action=open)."); return 0; }
        str_addf(&res->output, "URL: %s\n", g_nb_url);
        nb_mostra_links(&res->output, arg_b(a, "host_only", 0));
        return 1;
    }
    if (str_eq(action, "follow")) {
        int idx = arg_i(a, "index", 0);
        if (!g_nb_url[0]) { err_out(res, "Erro: abra uma pagina antes (action=open)."); return 0; }
        if (g_nb_nview == 0) {
            Str tmp; str_init(&tmp);
            nb_mostra_links(&tmp, 0);      /* so' monta a view */
            str_free(&tmp);
        }
        if (idx < 1 || idx > g_nb_nview) {
            err_out(res, "Erro: indice fora da lista (a pagina tem %d links).", g_nb_nview);
            return 0;
        }
        return nb_carrega("GET", g_nb_view[idx - 1], NULL, timeout, maxc, max_chars, arg_b(a, "readable", 0), &res->output, res);
    }
    if (str_eq(action, "back")) {
        if (g_nb_ncache == 0) { err_out(res, "Erro: historico vazio."); return 0; }
        NbCache *c = &g_nb_cache[--g_nb_ncache];
        nb_render(c->html, c->url, max_chars, arg_b(a, "readable", 0), &res->output);
        nb_text_init();
        str_clear(&g_nb_html);
        str_addn(&g_nb_html, c->html, 200 * 1024);
        free(c->html);
        c->html = NULL;
        return 1;
    }
    if (str_eq(action, "text")) {
        if (!g_nb_url[0]) { err_out(res, "Erro: abra uma pagina antes (action=open)."); return 0; }
        size_t lim = g_nb_text.len;
        if (max_chars > 0 && lim > (size_t)max_chars) lim = (size_t)max_chars;
        if (lim) str_add(&res->output, g_nb_text.data, lim);
        return 1;
    }
    if (str_eq(action, "read")) {
        if (!g_nb_url[0]) { err_out(res, "Erro: abra uma pagina antes (action=open)."); return 0; }
        Str vis; str_init(&vis);
        texto_readable(g_nb_text.data ? g_nb_text.data : "", &vis);
        size_t lim = vis.len;
        if (max_chars > 0 && lim > (size_t)max_chars) lim = (size_t)max_chars;
        if (lim) str_add(&res->output, vis.data, lim);
        str_free(&vis);
        return 1;
    }
    if (str_eq(action, "find")) {
        const char *alvo = arg_s(a, "text");
        if (!*alvo) { err_out(res, "Erro: 'text' e' obrigatorio para action=find."); return 0; }
        if (!g_nb_url[0]) { err_out(res, "Erro: abra uma pagina antes (action=open)."); return 0; }
        int achou = 0, linha = 0;
        const char *p = g_nb_text.data ? g_nb_text.data : "";
        while (*p && achou < 20) {
            const char *eol = strchr(p, '\n');
            size_t ll = eol ? (size_t)(eol - p) : strlen(p);
            linha++;
            char *linha_txt = str_ndup(p, ll);
            char *low = str_dup(linha_txt), *alvo_low = str_dup(alvo);
            for (char *q = low; *q; q++) *q = (char)tolower((unsigned char)*q);
            for (char *q = alvo_low; *q; q++) *q = (char)tolower((unsigned char)*q);
            if (strstr(low, alvo_low)) {
                str_addf(&res->output, "%d: %s\n", linha, linha_txt);
                achou++;
            }
            free(low); free(alvo_low); free(linha_txt);
            if (!eol) break;
            p = eol + 1;
        }
        if (!achou) str_addf(&res->output, "(nenhuma ocorrencia de \"%s\")\n", alvo);
        return 1;
    }

    /* open (GET ou POST, com query opcional) */
    const char *url = arg_s(a, "url");
    if (!*url) { err_out(res, "Erro: 'url' e' obrigatorio para action=open."); return 0; }

    const char *method = arg_s(a, "method");
    if (!*method) method = "GET";
    Str alvo; str_init(&alvo);
    str_adds(&alvo, url);
    Str query; str_init(&query);
    nb_query_str(json_get(a, "query"), &query);
    if (query.len) {
        str_adds(&alvo, strchr(url, '?') ? "&" : "?");
        str_adds(&alvo, query.data);
    }
    const char *body = arg_s(a, "body");
    if (!*body) body = arg_s(a, "text_body");
    int ok = nb_carrega(method, alvo.data, body, timeout, maxc, max_chars, arg_b(a, "readable", 0), &res->output, res);
    str_free(&query);
    str_free(&alvo);
    return ok;
}

/* ------------------------------------------------------------------ */
/* charset: Content-Type / <meta charset>, converte para UTF-8          */
/* ------------------------------------------------------------------ */
static int utf8_valido_bytes(const unsigned char *s, size_t n)
{
    size_t i = 0;
    while (i < n) {
        unsigned char c = s[i];
        size_t extra = 0;
        if (c < 0x80) { i++; continue; }
        else if ((c & 0xE0) == 0xC0) extra = 1;
        else if ((c & 0xF0) == 0xE0) extra = 2;
        else if ((c & 0xF8) == 0xF0) extra = 3;
        else return 0;
        if (i + extra >= n) return 0;
        for (size_t k = 1; k <= extra; k++)
            if ((s[i + k] & 0xC0) != 0x80) return 0;
        i += extra + 1;
    }
    return 1;
}

/* charset do Content-Type ou do <meta charset="..."> (primeiros 4 KB) */
static void charset_descobrir(const char *ctype, const char *html, char *out, size_t n)
{
    out[0] = 0;
    const char *p = ctype ? strstr(ctype, "charset=") : NULL;
    if (p) {
        p += 8;
        while (*p == '"' || *p == ' ') p++;
        size_t k = 0;
        while (*p && *p != '"' && *p != ';' && *p != ' ' && k + 1 < n) out[k++] = (char)tolower((unsigned char)*p++);
        out[k] = 0;
        if (k) return;
    }
    const char *meta = html ? html : "";
    for (int i = 0; i < 4096 && meta[i]; i++) {
        if ((meta[i] == 'c' || meta[i] == 'C') && !ci_ncmp(meta + i, "charset=", 8)) {
            const char *q = meta + i + 8;
            while (*q == '"' || *q == '\'') q++;
            size_t k = 0;
            while (*q && *q != '"' && *q != '\'' && *q != ';' && *q != '>' && *q != ' ' && k + 1 < n)
                out[k++] = (char)tolower((unsigned char)*q++);
            out[k] = 0;
            return;
        }
    }
}

/* converte o corpo da resposta para UTF-8 quando o charset nao e' utf-8 */
static void corpo_utf8(const char *ctype, const char *body, Str *out)
{
    size_t n = strlen(body);
    char cs[64];
    charset_descobrir(ctype, body, cs, sizeof cs);

    int eh_utf8 = !cs[0] || !strcmp(cs, "utf-8") || !strcmp(cs, "utf8");
    if (eh_utf8 && utf8_valido_bytes((const unsigned char *)body, n)) {
        str_add(out, body, n);
        return;
    }
    if (utf8_valido_bytes((const unsigned char *)body, n) && (!cs[0] || strstr(cs, "1252") || strstr(cs, "8859"))) {
        /* ou ja' e' utf-8 de verdade, ou veio rotulado como latin1 sem precisar converter */
        str_add(out, body, n);
        return;
    }

#ifdef _WIN32
    UINT cp = CP_UTF8;
    if (!cs[0] || strstr(cs, "1252") || strstr(cs, "8859-1") || strstr(cs, "latin")) cp = 1252;
    else if (strstr(cs, "1251")) cp = 1251;
    else if (strstr(cs, "1250")) cp = 1250;
    else if (strstr(cs, "1253")) cp = 1253;
    else if (strstr(cs, "1254")) cp = 1254;
    else if (strstr(cs, "1256")) cp = 1256;
    else if (strstr(cs, "1257")) cp = 1257;
    else if (strstr(cs, "932") || strstr(cs, "shift_jis") || strstr(cs, "sjis")) cp = 932;
    else if (strstr(cs, "936") || strstr(cs, "gbk")) cp = 936;
    else if (strstr(cs, "949")) cp = 949;
    else if (strstr(cs, "950") || strstr(cs, "big5")) cp = 950;
    else cp = 1252;

    if (cp == CP_UTF8) { str_add(out, body, n); return; }
    int wlen = MultiByteToWideChar(cp, 0, body, (int)n, NULL, 0);
    if (wlen <= 0) { str_add(out, body, n); return; }
    wchar_t *w = (wchar_t *)malloc(((size_t)wlen + 1) * sizeof(wchar_t));
    if (!w) { str_add(out, body, n); return; }
    MultiByteToWideChar(cp, 0, body, (int)n, w, wlen);
    int ulen = WideCharToMultiByte(CP_UTF8, 0, w, wlen, NULL, 0, NULL, NULL);
    if (ulen > 0) {
        char *u = (char *)malloc((size_t)ulen + 1);
        if (u) {
            WideCharToMultiByte(CP_UTF8, 0, w, wlen, u, ulen, NULL, NULL);
            str_add(out, u, (size_t)ulen);
            free(u);
        } else str_add(out, body, n);
    } else str_add(out, body, n);
    free(w);
#else
    str_add(out, body, n);
#endif
}

/* ------------------------------------------------------------------ */
/* modo leitura: derruba linhas de menu/rodape (corta tokens)           */
/* ------------------------------------------------------------------ */
static int linha_de_ruido(const char *s, size_t n)
{
    while (n && isspace((unsigned char)*s)) { s++; n--; }
    while (n && isspace((unsigned char)s[n - 1])) n--;
    if (n == 0) return 1;
    if (n > 200) return 0;

    static const char *ruido[] = {
        "home", "inicio", "menu", "login", "entrar", "sair", "logout", "search", "buscar",
        "skip", "pular", "next", "prev", "anterior", "proximo", "contato", "contact",
        "sobre", "about", "blog", "carrinho", "cart", "voltar", "back", "topo", "top",
        "termos", "privacidade", "privacy", "cookies", "newsletter", "assinar", "compartilhar",
        "en", "pt", "br", "english", "portugues", "compartilhe", "siga", "menu principal",
        NULL
    };
    for (int i = 0; ruido[i]; i++)
        if (n == strlen(ruido[i]) && !ci_ncmp(s, ruido[i], n)) return 1;

    if ((n >= 2 && (unsigned char)s[0] == 0xC2 && (unsigned char)s[1] == 0xA9) ||
        (n > 9 && !ci_ncmp(s, "copyright", 9)) ||
        (n >= 8 && !ci_ncmp(s, "todos os", 8)) ||
        (n >= 10 && !ci_ncmp(s, "all rights", 10)))
        return 1;

    /* linha feita so' de itens de menu: "Home | Menu | Contato" */
    int tokens = 0, todos = 1;
    size_t k = 0;
    while (k < n) {
        while (k < n && (isspace((unsigned char)s[k]) || s[k] == '|' || s[k] == '/' ||
                         s[k] == '-' || s[k] == '>' || s[k] == ':')) k++;
        size_t ini = k;
        while (k < n && !(isspace((unsigned char)s[k]) || s[k] == '|' || s[k] == '/' ||
                          s[k] == '-' || s[k] == '>' || s[k] == ':')) k++;
        size_t ll = k - ini;
        if (!ll) continue;
        tokens++;
        int conhecido = 0;
        for (int i = 0; ruido[i]; i++) {
            size_t lr = strlen(ruido[i]);
            if (ll == lr && !ci_ncmp(s + ini, ruido[i], lr)) { conhecido = 1; break; }
        }
        if (!conhecido) { todos = 0; break; }
    }
    return (tokens > 0 && todos) ? 1 : 0;
}

static void texto_readable(const char *txt, Str *out)
{
    const char *p = txt ? txt : "";
    int vazias = 0;
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t ll = eol ? (size_t)(eol - p) : strlen(p);
        if (!linha_de_ruido(p, ll)) {
            if (out->len) str_addc(out, '\n');
            str_add(out, p, ll);
            vazias = 0;
        } else if (ll == 0 && !vazias) {
            if (out->len) str_addc(out, '\n');
            vazias = 1;
        }
        if (!eol) break;
        p = eol + 1;
    }
}

/* ------------------------------------------------------------------ */
/* extrator de dados: JSON embutido e endpoints de API                  */
/* ------------------------------------------------------------------ */

/* chaves que nao interessam ao agente (ruido/ids/rastreio) */
static int json_chave_ruido(const char *k)
{
    static const char *ruim[] = {
        "id", "uuid", "guid", "hash", "token", "csrf", "nonce", "key", "secret",
        "src", "href", "url", "uri", "image", "images", "icon", "thumbnail", "avatar",
        "class", "classname", "style", "css", "font", "color", "width", "height",
        "created", "updated", "timestamp", "ts", "date", "time", "buildid",
        "imagem", "foto", "photo", "thumb", "logo", "banner", "background", "bg",
        "tracking", "analytics", "pixel", "gtm", "version", "schema", "type",
        NULL
    };
    for (int i = 0; ruim[i]; i++)
        if (ci_ncmp(k, ruim[i], strlen(ruim[i])) == 0 && (k[strlen(ruim[i])] == 0 || k[strlen(ruim[i])] == '_'))
            return 1;
    return 0;
}

/* vale a pena mostrar esse valor? */
static int json_valor_util(const char *v)
{
    size_t n = strlen(v);
    if (n < 2 || n > 400) return 0;
    if (strstr(v, "http://") == v || strstr(v, "https://") == v) return 0;
    if (v[0] == '#' || v[0] == '@') return 0;
    if (v[0] == '/') {
        if (v[1] == '/') return 0;                       /* //cdn... */
        if (strchr(v + 1, '/')) return 0;                /* /img/x.png, /api/... */
        const char *ext = strrchr(v, '.');
        if (ext && (ci_ncmp(ext, ".png", 4) == 0 || ci_ncmp(ext, ".jpg", 4) == 0 ||
                    ci_ncmp(ext, ".svg", 4) == 0 || ci_ncmp(ext, ".webp", 5) == 0 ||
                    ci_ncmp(ext, ".gif", 4) == 0)) return 0;
    }
    int digitos = 0, outros = 0;
    for (size_t i = 0; i < n; i++) {
        if (isdigit((unsigned char)v[i])) digitos++;
        else if (isalpha((unsigned char)v[i])) outros++;
    }
    if (digitos && outros == 0) return n <= 8;          /* numero puro curto */
    if ((size_t)digitos > n * 3 / 4) return 0;          /* hash/base64-ish */
    if (n > 60 && outros == 0) return 0;
    return 1;
}

static void json_mover(Str *out, JValue *v, Str *caminho, const char *filt, int *n, int max)
{
    if (!v || *n >= max) return;
    if (v->kind == J_OBJ) {
        for (size_t i = 0; i < v->count && *n < max; i++) {
            if (json_chave_ruido(v->pairs[i].key)) continue;
            size_t antes = caminho->len;
            if (caminho->len) str_addc(caminho, '.');
            str_adds(caminho, v->pairs[i].key);
            json_mover(out, v->pairs[i].val, caminho, filt, n, max);
            str_truncate(caminho, antes);
        }
    } else if (v->kind == J_ARR) {
        for (size_t i = 0; i < v->count && *n < max; i++) {
            size_t antes = caminho->len;
            str_addf(caminho, "[%lu]", (unsigned long)i);
            json_mover(out, v->items[i], caminho, filt, n, max);
            str_truncate(caminho, antes);
        }
    } else if (v->kind == J_STR || v->kind == J_NUM) {
        char valor[512];
        if (v->kind == J_STR) snprintf(valor, sizeof valor, "%s", v->str ? v->str : "");
        else snprintf(valor, sizeof valor, "%.10g", v->num);
        if (!json_valor_util(valor)) return;
        if (filt && *filt) {
            char low_v[512], low_f[128];
            snprintf(low_v, sizeof low_v, "%s", valor);
            snprintf(low_f, sizeof low_f, "%s", filt);
            for (char *p = low_v; *p; p++) *p = (char)tolower((unsigned char)*p);
            for (char *p = low_f; *p; p++) *p = (char)tolower((unsigned char)*p);
            char low_c[512];
            snprintf(low_c, sizeof low_c, "%s", caminho->data ? caminho->data : "");
            for (char *p = low_c; *p; p++) *p = (char)tolower((unsigned char)*p);
            if (!strstr(low_v, low_f) && !strstr(low_c, low_f)) return;
        }
        str_addf(out, "%s: %.400s\n", caminho->data ? caminho->data : "?", valor);
        (*n)++;
    }
}

/* fim do bloco JSON que comeca em p (conta chaves, respeita strings) */
static const char *json_fim(const char *p)
{
    int prof = 0, dentro = 0, esc = 0;
    for (; *p; p++) {
        char c = *p;
        if (dentro) {
            if (esc) esc = 0;
            else if (c == '\\') esc = 1;
            else if (c == '"') dentro = 0;
            continue;
        }
        if (c == '"') { dentro = 1; continue; }
        if (c == '{' || c == '[') prof++;
        else if (c == '}' || c == ']') { prof--; if (prof == 0) return p + 1; }
    }
    return NULL;
}

/* junta os <script> que parecem carregar dados: application/json, ld+json, __NEXT_DATA__... */
static void extrair_blocos_json(const char *html, Str *out, const char *filt, int max, int *nblocos)
{
    const char *p = html;
    int total = 0;
    while ((p = strstr(p, "<script")) != NULL && total < max) {
        const char *fim_tag = strchr(p, '>');
        if (!fim_tag) break;
        size_t attrs_len = (size_t)(fim_tag - p);
        char attrs[1024];
        size_t al = attrs_len < sizeof attrs - 1 ? attrs_len : sizeof attrs - 1;
        memcpy(attrs, p, al);
        attrs[al] = 0;

        const char *corpo = fim_tag + 1;
        const char *fecha = strstr(corpo, "</script");
        if (!fecha) break;

        int eh_json = 0;
        if (strstr(attrs, "application/json") || strstr(attrs, "application/ld+json")) eh_json = 1;
        if (strstr(attrs, "__NEXT_DATA__") || strstr(attrs, "__NUXT") || strstr(attrs, "__APOLLO_STATE__") ||
            strstr(attrs, "__INITIAL_STATE__") || strstr(attrs, "__DATA__") || strstr(attrs, "data-json"))
            eh_json = 1;

        const char *inicio = corpo;
        while (*inicio && isspace((unsigned char)*inicio)) inicio++;
        if (!eh_json && (*inicio == '{' || *inicio == '[')) {
            /* script solto que e' so' JSON (ex.: window.__X = {...} cai no caso abaixo) */
            const char *f = json_fim(inicio);
            if (f && (size_t)(f - inicio) > 20) eh_json = 1;
        }
        if (!eh_json) {
            /* window.__X = { ... } ou JSON.parse('{...}') */
            const char *atrib = strstr(inicio, "=");
            if (atrib && atrib < fecha) {
                const char *b = atrib;
                while (b < fecha && *b != '{' && *b != '[') b++;
                if (b < fecha && (size_t)(b - inicio) < 400) {
                    const char *f = json_fim(b);
                    if (f && f <= fecha + 2 && (size_t)(f - b) > 10) { inicio = b; eh_json = 1; }
                }
            }
        }

        if (eh_json) {
            const char *fimjson = json_fim(inicio);
            size_t len = fimjson && fimjson <= fecha ? (size_t)(fimjson - inicio) : (size_t)(fecha - inicio);
            char *json = str_ndup(inicio, len);
            JValue *v = json_parse_cstr(json);
            if (v) {
                (*nblocos)++;
                Str caminho; str_init(&caminho);
                json_mover(out, v, &caminho, filt, &total, max);
                str_free(&caminho);
                json_free(v);
            }
            free(json);
        }
        p = fecha + 1;
    }
}

static void extrair_apis(const char *html, Str *out, int max)
{
    static const char *padroes[] = { "fetch(", "axios.get(", "axios.post(", "axios(", "\\$http.get(", "url:" };
    char vistos[64][512];
    int nv = 0;
    const char *p = html;
    while (*p && nv < max) {
        int achou = 0;
        for (int k = 0; k < (int)(sizeof padroes / sizeof padroes[0]) && !achou; k++) {
            const char *hit = strstr(p, padroes[k]);
            if (!hit) continue;
            const char *q = strchr(hit, '\'');
            if (!q) q = strchr(hit, '"');
            if (!q) q = strchr(hit, '`');
            if (!q) continue;
            char quote = *q;
            const char *f = q + 1;
            while (*f && *f != quote && *f != '\n' && (size_t)(f - q) < 300) f++;
            if (*f != quote) continue;
            char url[512];
            size_t ll = (size_t)(f - q - 1);
            if (ll < 3 || ll >= sizeof url) continue;
            memcpy(url, q + 1, ll);
            url[ll] = 0;
            if (!(url[0] == '/' || !strncmp(url, "http", 4))) continue;
            if (strstr(url, ".js") || strstr(url, ".css") || strstr(url, ".png") || strstr(url, ".svg")) continue;
            int rep = 0;
            for (int i = 0; i < nv; i++) if (str_eq(vistos[i], url)) { rep = 1; break; }
            if (!rep && nv < 64) {
                snprintf(vistos[nv++], sizeof vistos[0], "%s", url);
                str_addf(out, "%s\n", url);
            }
            achou = 1;
            p = f + 1;
        }
        if (!achou) p++;
    }
    if (nv == 0) str_adds(out, "(nenhum endpoint encontrado)\n");
}

/* baixa a pagina ja' em UTF-8 (usa cookies/redirects da sessao) */
static int baixar_html(const char *url, int timeout, int maxc, Str *out, ToolResult *res)
{
    Str raw; str_init(&raw);
    if (!http_do("GET", url, "User-Agent: delta-agent/1.0\r\nAccept: text/html,*/*\r\n", NULL,
                 timeout, maxc, &raw)) {
        str_free(&raw);
        err_out(res, "Erro: nao consegui abrir %s.", url);
        return 0;
    }
    char *corpo = strchr(raw.data, '\n');
    corpo_utf8(g_last_ctype, corpo ? corpo + 1 : "", out);
    str_free(&raw);
    return 1;
}

static int data_op(JValue *a, ToolResult *res)
{
    const char *action = arg_s(a, "action");
    if (!*action) action = "json";
    const char *url = arg_s(a, "url");
    int max = arg_i(a, "max_items", 120);
    int maxc = arg_i(a, "max_response_chars", 600000);
    int timeout = arg_i(a, "timeout_seconds", 30);

    Str html; str_init(&html);
    if (*url) {
        if (!baixar_html(url, timeout, maxc, &html, res)) { str_free(&html); return 0; }
    } else {
        /* usa a pagina atual do micro navegador, se houver */
        nb_text_init();
        if (!g_nb_html.len) {
            err_out(res, "Erro: informe 'url' (ou abra uma pagina com op=browser antes).");
            str_free(&html);
            return 0;
        }
        str_add(&html, g_nb_html.data, g_nb_html.len);
    }

    const char *filt = arg_s(a, "filter");
    if (!*filt) filt = arg_s(a, "query");

    if (str_eq(action, "api")) {
        str_adds(&res->output, "ENDPOINTS:\n");
        extrair_apis(html.data ? html.data : "", &res->output, max);
    } else if (str_eq(action, "find")) {
        int blocos = 0;
        Str j; str_init(&j);
        extrair_blocos_json(html.data ? html.data : "", &j, filt, max, &blocos);
        str_addf(&res->output, "JSON: %d bloco(s), %d campo(s)\n", blocos,
                 j.data ? (int)(j.len ? 1 : 0) : 0);
        if (j.len) str_add(&res->output, j.data, j.len);
        str_adds(&res->output, "\nENDPOINTS:\n");
        extrair_apis(html.data ? html.data : "", &res->output, max);
        str_free(&j);
    } else {
        int blocos = 0;
        Str j; str_init(&j);
        extrair_blocos_json(html.data ? html.data : "", &j, filt, max, &blocos);
        if (!blocos) {
            str_adds(&res->output, "(nenhum JSON embutido encontrado — tente action=api)\n");
        } else {
            str_addf(&res->output, "%d bloco(s) de JSON, campos uteis:\n", blocos);
            str_add(&res->output, j.data ? j.data : "", j.len);
        }
        str_free(&j);
    }
    str_free(&html);
    return 1;
}

static int web_op(JValue *a, const char *cwd, ToolResult *res)
{
    (void)cwd;
    const char *op = arg_s(a, "op");
    const char *url = arg_s(a, "url");
    if (!*op) { err_out(res, "Erro: 'op' e obrigatorio (http|scrape|browser)."); return 0; }
    if (str_eq(op, "data")) return data_op(a, res);
    if (str_eq(op, "browser")) return browser_op(a, res);
    if (!*url) { err_out(res, "Erro: 'url' e' obrigatorio."); return 0; }

    int maxc = arg_i(a, "max_response_chars", 200000);
    if (maxc <= 0 || maxc > EXT_MAX_OUT) maxc = EXT_MAX_OUT;

    if (str_eq(op, "http")) {
        const char *method = arg_s(a, "method");
        if (!*method) method = "GET";
        Str hdrs; str_init(&hdrs);
        JValue *h = json_get(a, "headers");
        if (h && h->kind == J_OBJ) {
            for (size_t i = 0; i < h->count; i++)
                str_addf(&hdrs, "%s: %s\r\n", h->pairs[i].key, json_str(h->pairs[i].val));
        }
        JValue *jb = json_get(a, "json_body");
        if (jb) {
            Str body; str_init(&body);
            json_dump(&body, jb);
            int ok = http_do(method, url, hdrs.data, body.data, arg_i(a, "timeout_seconds", 30), maxc, &res->output);
            str_free(&body);
            str_free(&hdrs);
            return ok;
        }
        const char *body = arg_s(a, "text_body");
        int ok = http_do(method, url, hdrs.data, body, arg_i(a, "timeout_seconds", 30), maxc, &res->output);
        str_free(&hdrs);
        return ok;
    }
    if (str_eq(op, "scrape")) {
        Str raw; str_init(&raw);
        int ok = http_do("GET", url, "User-Agent: delta-agent/1.0\r\n", NULL, arg_i(a, "timeout_seconds", 30), maxc, &raw);
        if (!ok) { str_free(&raw); err_out(res, "Erro: falha ao ler a pagina."); return 0; }
        /* remove a linha de status do cabecalho */
        char *corpo = strchr(raw.data, '\n');
        Str htmlu; str_init(&htmlu);
        corpo_utf8(g_last_ctype, corpo ? corpo + 1 : (raw.data ? raw.data : ""), &htmlu);
        Str texto; str_init(&texto);
        html_to_text(htmlu.data ? htmlu.data : "", &texto);
        if (arg_b(a, "readable", 0)) {
            Str vis; str_init(&vis);
            texto_readable(texto.data ? texto.data : "", &vis);
            str_clear(&texto);
            str_add(&texto, vis.data ? vis.data : "", vis.len);
            str_free(&vis);
        }
        str_free(&htmlu);
        int lim = arg_i(a, "max_chars", 20000);
        if (lim <= 0 || (size_t)lim > texto.len) lim = (int)(texto.len ? texto.len : 0);
        if (texto.len) str_add(&res->output, texto.data, (size_t)lim);
        if ((size_t)lim < texto.len) str_adds(&res->output, "\n[truncado]\n");
        if (str_eq(op, "browser"))
            str_adds(&res->output, "\n[nota: browser le o HTML como texto; nao executa JavaScript]\n");
        str_free(&texto);
        str_free(&raw);
        return 1;
    }
    err_out(res, "Erro: operacao desconhecida de web: %s", op);
    return 0;
}

static const char WEB_SCHEMA[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"op\":{\"type\":\"string\",\"enum\":[\"http\",\"scrape\",\"browser\",\"data\"]},"
    "\"filter\":{\"type\":\"string\",\"description\":\"filtro de campos para op=data\"},"
    "\"max_items\":{\"type\":\"number\"},"
    "\"action\":{\"type\":\"string\",\"enum\":[\"open\",\"links\",\"follow\",\"back\",\"text\",\"find\"],\"description\":\"browser: open (padrao), links, follow, back, text, find\"},"
    "\"index\":{\"type\":\"number\",\"description\":\"numero do link (1..N) para action=follow\"},"
    "\"text\":{\"type\":\"string\",\"description\":\"texto procurado na pagina (action=find)\"},"
    "\"body\":{\"type\":\"string\",\"description\":\"corpo do POST (action=open + method=POST)\"},"
    "\"query\":{\"type\":\"object\",\"description\":\"parametros de query: vira ?a=1&b=2\"},"
    "\"host_only\":{\"type\":\"boolean\",\"description\":\"links apenas do mesmo host (action=links)\"},"
    "\"url\":{\"type\":\"string\"},"
    "\"method\":{\"type\":\"string\"},"
    "\"headers\":{\"type\":\"object\",\"description\":\"Cabecalhos HTTP\"},"
    "\"text_body\":{\"type\":\"string\"},"
    "\"timeout_seconds\":{\"type\":\"number\"},"
    "\"max_chars\":{\"type\":\"number\"},\"max_response_chars\":{\"type\":\"number\"}},"
    "\"required\":[\"op\",\"url\"]}";

/* ------------------------------------------------------------------ */
/* memory                                                              */
/* ------------------------------------------------------------------ */
static char *memory_path(void)
{
    char *dir = delta_home();
    dir_make(dir);
    char *p = path_join(dir, "memory.json");
    free(dir);
    return p;
}

static JValue *memory_load(void)
{
    char *p = memory_path();
    size_t len = 0;
    char *txt = file_read(p, &len);
    free(p);
    JValue *arr = NULL;
    if (txt) { arr = json_parse(txt, len); free(txt); }
    if (!arr || arr->kind != J_ARR) { if (arr) json_free(arr); arr = json_parse_cstr("[]"); }
    return arr;
}

static int memory_save(JValue *arr)
{
    Str out; str_init(&out);
    str_addc(&out, '[');
    for (size_t i = 0; i < arr->count; i++) {
        JValue *n = arr->items[i];
        if (i) str_addc(&out, ',');
        str_addc(&out, '{');
        const char *keys[] = { "id", "title", "content", "kind", "scope", "tags", "created" };
        int first = 1;
        for (int k = 0; k < 7; k++) {
            JValue *v = json_get(n, keys[k]);
            if (!v) continue;
            if (!first) str_addc(&out, ',');
            first = 0;
            json_write_string(&out, keys[k], strlen(keys[k]));
            str_addc(&out, ':');
            if (v->kind == J_STR) json_write_string(&out, v->str, strlen(v->str));
            else if (v->kind == J_ARR) json_write_string(&out, "[]", 2);
            else json_write_string(&out, "", 0);
        }
        str_addc(&out, '}');
    }
    str_addc(&out, ']');
    char *p = memory_path();
    int ok = file_write(p, out.data, out.len);
    free(p);
    str_free(&out);
    return ok;
}

static JValue *memory_new_note(const char *title, const char *content, const char *kind,
                               const char *scope, const char *tags_json)
{
    (void)tags_json;
    char id[40];
    uuid_v4(id);
    char shortid[9];
    memcpy(shortid, id, 8);
    shortid[8] = 0;
    char ts[32];
    iso_now(ts);

    JValue *v;
    v = json_parse_cstr("{\"id\":\"\",\"title\":\"\",\"content\":\"\",\"kind\":\"\",\"scope\":\"\",\"tags\":[],\"created\":\"\"}");
    /* preenche por ponteiro (o parser ja criou as chaves) */
    JValue *pair;
    if ((pair = json_get(v, "id"))) { free(pair->str); pair->str = str_dup(shortid); }
    if ((pair = json_get(v, "title"))) { free(pair->str); pair->str = str_dup(title); }
    if ((pair = json_get(v, "content"))) { free(pair->str); pair->str = str_dup(content); }
    if ((pair = json_get(v, "kind"))) { free(pair->str); pair->str = str_dup(*kind ? kind : "note"); }
    if ((pair = json_get(v, "scope"))) { free(pair->str); pair->str = str_dup(*scope ? scope : "global"); }
    if ((pair = json_get(v, "created"))) { free(pair->str); pair->str = str_dup(ts); }
    return v;
}

static void note_print(Str *out, JValue *n)
{
    const char *kind = json_str(json_get(n, "kind"));
    if (str_eq(kind, "link")) {
        str_addf(out, "- [%s] link: %s -> %s\n",
                 json_str(json_get(n, "id")), json_str(json_get(n, "title")),
                 json_str(json_get(n, "content")));
        return;
    }
    str_addf(out, "- [%s] %s (%s/%s)\n  %s\n",
             json_str(json_get(n, "id")), json_str(json_get(n, "title")),
             kind, json_str(json_get(n, "scope")),
             json_str(json_get(n, "content")));
}

static int memory_op(JValue *a, const char *cwd, ToolResult *res)
{
    (void)cwd;
    const char *op = arg_s(a, "op");
    if (!*op) { err_out(res, "Erro: 'op' e' obrigatorio (remember|update|search|list|link|unlink|query|insert)."); return 0; }

    JValue *arr = memory_load();
    int ok = 0;

    if (str_eq(op, "remember")) {
        const char *title = arg_s(a, "title");
        const char *content = arg_s(a, "content");
        if (!*title && !*content) { err_out(res, "Erro: informe 'title' ou 'content'."); json_free(arr); return 0; }
        JValue *n = memory_new_note(title, content, arg_s(a, "kind"), arg_s(a, "scope"), arg_s(a, "tags_json"));
        /* empilha no array */
        arr->items = (JValue **)realloc(arr->items, (arr->count + 1) * sizeof(JValue *));
        arr->items[arr->count++] = n;
        ok = memory_save(arr);
        if (ok) str_addf(&res->output, "Memory stored: %s\n", json_str(json_get(n, "id")));
        else err_out(res, "Erro: falha ao gravar a memoria.");
    } else if (str_eq(op, "update")) {
        const char *id = arg_s(a, "id");
        JValue *n = NULL;
        for (size_t i = 0; i < arr->count; i++)
            if (str_eq(json_str(json_get(arr->items[i], "id")), id)) { n = arr->items[i]; break; }
        if (!n) { err_out(res, "Erro: id de memoria nao encontrado (use search/list)."); json_free(arr); return 0; }
        const char *fields[] = { "title", "content", "kind", "scope" };
        for (int k = 0; k < 4; k++) {
            JValue *v = json_get(a, fields[k]);
            if (v && v->kind == J_STR) {
                JValue *dst = json_get(n, fields[k]);
                if (dst) { free(dst->str); dst->str = str_dup(v->str); }
            }
        }
        ok = memory_save(arr);
        if (ok) str_addf(&res->output, "Memory updated: %s\n", id);
        else err_out(res, "Erro: falha ao gravar a memoria.");
    } else if (str_eq(op, "search") || str_eq(op, "query")) {
        const char *q = str_eq(op, "search") ? arg_s(a, "query") : "";
        int limit = arg_i(a, "limit", 20);
        int n = 0;
        for (size_t i = 0; i < arr->count && n < limit; i++) {
            JValue *note = arr->items[i];
            const char *title = json_str(json_get(note, "title"));
            const char *content = json_str(json_get(note, "content"));
            if (!*q || strstr(title, q) || strstr(content, q)) { note_print(&res->output, note); n++; }
        }
        if (n == 0) str_adds(&res->output, "(nenhuma memoria encontrada)\n");
        ok = 1;
    } else if (str_eq(op, "list")) {
        const char *scope = arg_s(a, "scope");
        int limit = arg_i(a, "limit", 50);
        int n = 0;
        for (size_t i = 0; i < arr->count && n < limit; i++) {
            JValue *note = arr->items[i];
            if (*scope && !str_eq(json_str(json_get(note, "scope")), scope)) continue;
            note_print(&res->output, note);
            n++;
        }
        if (n == 0) str_adds(&res->output, "(memoria vazia)\n");
        ok = 1;
    } else if (str_eq(op, "insert")) {
        const char *table = arg_s(a, "table");
        if (!str_eq(table, "memory_records") && !str_eq(table, "facts")) {
            err_out(res, "Erro: tabela nao permitida (memory_records|facts).");
        } else {
            JValue *data = json_get(a, "data_json");
            (void)data;
            JValue *n = memory_new_note(table, arg_s(a, "data_json"), "record", "database", "[]");
            arr->items = (JValue **)realloc(arr->items, (arr->count + 1) * sizeof(JValue *));
            arr->items[arr->count++] = n;
            ok = memory_save(arr);
            if (ok) str_addf(&res->output, "Inserted into %s (note %s).\n", table, json_str(json_get(n, "id")));
            else err_out(res, "Erro: falha ao inserir.");
        }
    } else if (str_eq(op, "link") || str_eq(op, "unlink")) {
        const char *from = arg_s(a, "from");
        const char *to = arg_s(a, "to");
        const char *label = arg_s(a, "label");
        if (!*from || !*to) {
            err_out(res, "Erro: 'from' e 'to' sao obrigatorios.");
        } else if (str_eq(op, "link")) {
            int existe = 0;
            for (size_t i = 0; i < arr->count; i++)
                if (str_eq(json_str(json_get(arr->items[i], "id")), from)) { existe = 1; break; }
            if (!existe) {
                err_out(res, "Erro: nota '%s' nao existe (use memory_search).", from);
            } else {
                int dup = 0;
                for (size_t i = 0; i < arr->count; i++) {
                    JValue *l = arr->items[i];
                    if (str_eq(json_str(json_get(l, "kind")), "link") &&
                        str_eq(json_str(json_get(l, "title")), from) &&
                        str_eq(json_str(json_get(l, "content")), to)) { dup = 1; break; }
                }
                if (dup) {
                    str_addf(&res->output, "Already linked %s -> %s\n", from, to);
                    ok = 1;
                } else {
                    JValue *n = memory_new_note(from, to, "link", "link", "[]");
                    (void)label;
                    arr->items = (JValue **)realloc(arr->items, (arr->count + 1) * sizeof(JValue *));
                    arr->items[arr->count++] = n;
                    ok = memory_save(arr);
                    if (ok) str_addf(&res->output, "Linked %s -> %s\n", from, to);
                    else err_out(res, "Erro: falha ao gravar a memoria.");
                }
            }
        } else {
            int achou = 0;
            for (size_t i = 0; i < arr->count; ) {
                JValue *l = arr->items[i];
                if (str_eq(json_str(json_get(l, "kind")), "link") &&
                    str_eq(json_str(json_get(l, "title")), from) &&
                    str_eq(json_str(json_get(l, "content")), to)) {
                    json_free(l);
                    memmove(&arr->items[i], &arr->items[i + 1], (arr->count - i - 1) * sizeof(JValue *));
                    arr->count--;
                    achou = 1;
                    continue;
                }
                i++;
            }
            if (!achou) err_out(res, "Erro: link %s -> %s nao existe.", from, to);
            else if (!memory_save(arr)) err_out(res, "Erro: falha ao gravar a memoria.");
            else { str_addf(&res->output, "Unlinked %s -> %s\n", from, to); ok = 1; }
        }
    } else {
        err_out(res, "Erro: operacao desconhecida de memory: %s", op);
    }
    json_free(arr);
    return ok;
}

static const char MEMORY_SCHEMA[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"op\":{\"type\":\"string\",\"enum\":[\"remember\",\"update\",\"search\",\"list\",\"link\",\"unlink\",\"query\",\"insert\"]},"
    "\"title\":{\"type\":\"string\"},\"content\":{\"type\":\"string\"},"
    "\"kind\":{\"type\":\"string\"},\"scope\":{\"type\":\"string\"},\"tags_json\":{\"type\":\"string\"},"
    "\"id\":{\"type\":\"string\"},\"query\":{\"type\":\"string\"},\"limit\":{\"type\":\"number\"},"
    "\"from\":{\"type\":\"string\"},\"to\":{\"type\":\"string\"},\"label\":{\"type\":\"string\"},"
    "\"table\":{\"type\":\"string\"},\"data_json\":{\"type\":\"string\"}},"
    "\"required\":[\"op\"]}";

/* ------------------------------------------------------------------ */
/* code                                                                */
/* ------------------------------------------------------------------ */
static int code_op(JValue *a, const char *cwd, ToolResult *res)
{
    const char *op = arg_s(a, "op");
    if (!*op) { err_out(res, "Erro: 'op' e' obrigatorio (structure|symbol|tests)."); return 0; }

    if (str_eq(op, "structure")) {
        Walk w;
        memset(&w, 0, sizeof w);
        w.out = &res->output;
        w.cwd = cwd;
        w.depth_max = arg_i(a, "max_depth", 3);
        w.max_results = arg_i(a, "max_entries", 400);
        w.tree = 1;
        str_addf(&res->output, "%s/\n", path_basename(cwd));
        walk_dir(&w, cwd, 1);
        return 1;
    }
    if (str_eq(op, "symbol")) {
        const char *name = arg_s(a, "name");
        if (!*name) { err_out(res, "Erro: 'name' e' obrigatorio."); return 0; }
        Walk w;
        memset(&w, 0, sizeof w);
        w.out = &res->output;
        w.cwd = cwd;
        w.depth_max = 12;
        w.max_results = arg_i(a, "max_results", 60);
        w.needle = name;
        str_addf(&res->output, "SYMBOL '%s'\n", name);
        walk_dir(&w, cwd, 0);
        if (w.count == 0) str_adds(&res->output, "(nenhuma ocorrencia)\n");
        return 1;
    }
    if (str_eq(op, "tests")) {
        const char *cmd = NULL;
        char buf[512];
        if (file_exists(path_join(cwd, "Makefile")) ) { snprintf(buf, sizeof buf, "make test 2>&1 || mingw32-make test 2>&1"); cmd = buf; }
        else if (file_exists(path_join(cwd, "build.bat"))) { snprintf(buf, sizeof buf, "cmd /c build.bat test"); cmd = buf; }
        else if (file_exists(path_join(cwd, "package.json"))) { snprintf(buf, sizeof buf, "npm test"); cmd = buf; }
        else if (file_exists(path_join(cwd, "tests/run_all.sh"))) { snprintf(buf, sizeof buf, "bash tests/run_all.sh"); cmd = buf; }
        if (!cmd) { err_out(res, "Erro: nenhum framework de teste detectado (Makefile/build.bat/package.json/tests/run_all.sh)."); return 0; }
        int code = 0;
        int timeout = arg_i(a, "timeout_seconds", 300);
        int ok = tools_exec(cwd, cmd, timeout, &res->output, &code);
        res->exit_code = code;
        str_addf(&res->output, "\n[TESTS: %s -> exit %d]", cmd, code);
        if (code != 0) res->is_error = 1;
        return ok;
    }
    err_out(res, "Erro: operacao desconhecida de code: %s", op);
    return 0;
}

static const char CODE_SCHEMA[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"op\":{\"type\":\"string\",\"enum\":[\"structure\",\"symbol\",\"tests\"]},"
    "\"name\":{\"type\":\"string\",\"description\":\"Simbolo procurado (op=symbol)\"},"
    "\"max_depth\":{\"type\":\"number\"},\"max_entries\":{\"type\":\"number\"},"
    "\"max_results\":{\"type\":\"number\"},\"timeout_seconds\":{\"type\":\"number\"}},"
    "\"required\":[\"op\"]}";

/* ------------------------------------------------------------------ */
/* assert                                                              */
/* ------------------------------------------------------------------ */
static int assert_op(JValue *a, const char *cwd, ToolResult *res)
{
    (void)cwd;
    const char *op = arg_s(a, "op");
    if (!*op) { err_out(res, "Erro: 'op' e' obrigatorio (equals|contains)."); return 0; }
    if (str_eq(op, "assert_true")) op = "equals";
    if (str_eq(op, "assert_contains")) op = "contains";

    const char *desc = arg_s(a, "condition_description");
    if (!*desc) desc = "assertion";
    const char *value = arg_s(a, "value");

    if (str_eq(op, "equals")) {
        const char *expected = arg_s(a, "expected");
        if (str_eq(value, expected)) {
            str_addf(&res->output, "PASS: %s (valor identico ao esperado)\n", desc);
            return 1;
        }
        err_out(res, "FAIL: %s\n  esperado: %s\n  obtido  : %s\n", desc, expected, value);
        return 0;
    }
    if (str_eq(op, "contains")) {
        const char *sub = arg_s(a, "substring");
        if (strstr(value, sub)) {
            str_addf(&res->output, "PASS: %s (contem \"%s\")\n", desc, sub);
            return 1;
        }
        err_out(res, "FAIL: %s\n  nao contem: %s\n  valor: %.500s\n", desc, sub, value);
        return 0;
    }
    err_out(res, "Erro: operacao desconhecida de assert: %s", op);
    return 0;
}

static const char ASSERT_SCHEMA[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"op\":{\"type\":\"string\",\"enum\":[\"equals\",\"contains\",\"assert_true\",\"assert_contains\"]},"
    "\"condition_description\":{\"type\":\"string\"},"
    "\"value\":{\"type\":\"string\"},\"expected\":{\"type\":\"string\"},\"substring\":{\"type\":\"string\"}},"
    "\"required\":[\"op\"]}";

/* ------------------------------------------------------------------ */
/* agent: plan, skill, subagent                                        */
/* ------------------------------------------------------------------ */
static char *plan_path(void)
{
    char *dir = delta_home();
    dir_make(dir);
    char *p = path_join(dir, "plan.md");
    free(dir);
    return p;
}

/* Executavel do proprio app (para rodar um subagente com todas as tools). */
static const char *self_exe(void)
{
#ifdef _WIN32
    static char buf[1024];
    DWORD n = GetModuleFileNameA(NULL, buf, sizeof buf);
    if (n == 0 || n >= sizeof buf) return NULL;
    return buf;
#else
    static char buf[1024];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return NULL;
    buf[n] = 0;
    return buf;
#endif
}

/* Subagente = o proprio binario em modo -p (um agente completo, com tools),
 * num processo separado: nao mexe no estado da TUI nem na sessao do pai. */
/* ------------------------------------------------------------------ */
/* Subagentes                                                          */
/* ------------------------------------------------------------------ */
/* Um subagente e' outro processo do proprio delta com -p: contexto limpo,
   ferramentas proprias, sem sessao na pasta do pai (--no-session) e, se
   quiser, com outro modelo (barato para varrer, forte para revisar).
   op=subagent roda um; op=subagents roda ate' 4 em paralelo, cada um com a
   saida num arquivo (nada de pipe: o filho nunca escreve na TUI do pai). */

typedef struct {
    char    *task_file;
    char    *out_file;
    char    *cmd;
#ifdef _WIN32
    HANDLE   proc;
#else
    int      pid;
#endif
    uint64_t t0;
    int      exit_code;
    int      rodou;
} Child;

/* Grava a tarefa num arquivo: evita brigar com as aspas do cmd.exe. */
static char *child_task_file(const char *task, size_t idx)
{
    char *dir = delta_home();
    dir_make(dir);
    Str nome; str_init(&nome);
    str_addf(&nome, "subagent-%llu-%lu.md", (unsigned long long)now_ms(), (unsigned long)idx);
    char *tmp = path_join(dir, nome.data);
    str_free(&nome);
    free(dir);
    if (!file_write(tmp, task, strlen(task))) { free(tmp); return NULL; }
    return tmp;
}

static char *child_cmd(const char *exe, const char *task_file, const char *model,
                       const char *out_file, const char *child_cwd)
{
    Str c; str_init(&c);
#ifdef _WIN32
    str_adds(&c, "set DELTA_SUBAGENT=1&& ");
    if (child_cwd && *child_cwd) str_addf(&c, "cd /d \"%s\" && ", child_cwd);
#else
    str_adds(&c, "DELTA_SUBAGENT=1 ");
    if (child_cwd && *child_cwd) str_addf(&c, "cd \"%s\" && ", child_cwd);
#endif
    str_addf(&c, "\"%s\" -p \"@%s\" --no-session", exe, task_file);
    if (model && *model) str_addf(&c, " --model \"%s\"", model);
    if (out_file) str_addf(&c, " > \"%s\" 2>&1", out_file);
    return str_steal(&c);
}

static int child_start(Child *c, const char *exe, const char *task, size_t idx,
                       const char *model, const char *child_cwd, const char *out_dir)
{
    memset(c, 0, sizeof *c);
    c->task_file = child_task_file(task, idx);
    if (!c->task_file) return 0;
    {
        Str o; str_init(&o);
#ifdef _WIN32
        str_addf(&o, "%s\\subagent-%lu.out", out_dir, (unsigned long)idx);
#else
        str_addf(&o, "%s/subagent-%lu.out", out_dir, (unsigned long)idx);
#endif
        c->out_file = str_steal(&o);
    }
    c->cmd = child_cmd(exe, c->task_file, model, c->out_file, child_cwd);
    c->t0 = now_ms();

#ifdef _WIN32
    {
        STARTUPINFOA si;
        memset(&si, 0, sizeof si);
        si.cb = sizeof si;
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi;
        memset(&pi, 0, sizeof pi);
        Str full; str_init(&full);
        str_adds(&full, "cmd.exe /c chcp 65001 >nul & ");
        str_adds(&full, c->cmd);
        char *mutavel = str_dup(full.data);
        str_free(&full);
        int ok = CreateProcessA(NULL, mutavel, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL,
                                (child_cwd && *child_cwd) ? child_cwd : NULL, &si, &pi);
        free(mutavel);
        if (!ok) return 0;
        CloseHandle(pi.hThread);
        c->proc = pi.hProcess;
        c->rodou = 1;
        return 1;
    }
#else
    {
        pid_t pid = fork();
        if (pid < 0) return 0;
        if (pid == 0) {
            if (child_cwd && *child_cwd && chdir(child_cwd) != 0) _exit(126);
            execl("/bin/sh", "sh", "-c", c->cmd, (char *)NULL);
            _exit(127);
        }
        c->pid = (int)pid;
        c->rodou = 1;
        return 1;
    }
#endif
}

/* Espera ate' `timeout_sec` (0 = sem limite). Preenche exit_code. */
static void child_wait(Child *c, int timeout_sec)
{
    if (!c->rodou) return;
#ifdef _WIN32
    DWORD ms = timeout_sec > 0 ? (DWORD)timeout_sec * 1000 : INFINITE;
    if (WaitForSingleObject(c->proc, ms) == WAIT_TIMEOUT) {
        TerminateProcess(c->proc, 124);
        WaitForSingleObject(c->proc, 5000);
        c->exit_code = 124;
    } else {
        DWORD code = 1;
        GetExitCodeProcess(c->proc, &code);
        c->exit_code = (int)code;
    }
    CloseHandle(c->proc);
    c->proc = NULL;
#else
    uint64_t inicio = now_ms();
    for (;;) {
        int status = 0;
        pid_t w = waitpid((pid_t)c->pid, &status, WNOHANG);
        if (w == (pid_t)c->pid) {
            c->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
            break;
        }
        if (w < 0) { c->exit_code = 1; break; }
        if (timeout_sec > 0 && (now_ms() - inicio) > (uint64_t)timeout_sec * 1000) {
            kill((pid_t)c->pid, SIGKILL);
            waitpid((pid_t)c->pid, &status, 0);
            c->exit_code = 124;
            break;
        }
        msleep(30);
    }
#endif
}

static void child_free(Child *c)
{
    if (c->task_file) { remove(c->task_file); free(c->task_file); c->task_file = NULL; }
    if (c->out_file) { free(c->out_file); c->out_file = NULL; }
    if (c->cmd) { free(c->cmd); c->cmd = NULL; }
    c->rodou = 0;
}

/* Copia a saida do filho (arquivo) para `out`, com teto de caracteres. */
static void child_output(Child *c, size_t max_chars, Str *out)
{
    size_t n = 0;
    char *txt = c->out_file ? file_read(c->out_file, &n) : NULL;
    if (txt) {
        str_add(out, txt, n < max_chars ? n : max_chars);
        if (n > max_chars) str_adds(out, "\n[... saida cortada]\n");
        free(txt);
    }
    if (c->out_file) { remove(c->out_file); free(c->out_file); c->out_file = NULL; }
    if (out->len == 0) str_adds(out, "(subagente sem resposta)\n");
}

static char *job_dir(void)
{
    char *dir = delta_home();
    char *jobs = path_join(dir, "jobs");
    dir_make(jobs);
    free(dir);
    return jobs;
}

static int subagent_run(const char *cwd, const char *task, const char *model,
                        int timeout_sec, size_t max_chars, const char *child_cwd,
                        Str *out, ToolResult *res)
{
    (void)cwd;
    if (getenv("DELTA_SUBAGENT")) {
        err_out(res, "Erro: um subagente nao pode abrir outro subagente.");
        return 0;
    }
    const char *exe = self_exe();
    if (!exe) { err_out(res, "Erro: nao consegui localizar o proprio executavel."); return 0; }
    if (timeout_sec <= 0) timeout_sec = 900;
    if (max_chars == 0) max_chars = 20000;

    char *jobs = job_dir();
    Child c;
    if (!child_start(&c, exe, task, 0, model, child_cwd ? child_cwd : cwd, jobs)) {
        child_free(&c);
        free(jobs);
        err_out(res, "Erro: falha ao iniciar o subagente.");
        return 0;
    }
    free(jobs);
    child_wait(&c, timeout_sec);
    child_output(&c, max_chars, out);
    if (c.exit_code != 0) {
        res->is_error = 1;
        str_addf(&res->output, "\n[subagente saiu com codigo %d em %.1f s]\n",
                 c.exit_code, (double)(now_ms() - c.t0) / 1000.0);
    } else {
        str_addf(&res->output, "\n[subagente ok em %.1f s]\n",
                 (double)(now_ms() - c.t0) / 1000.0);
    }
    child_free(&c);
    return 1;
}

#define SUBAGENT_MAX 4

/* Ate' 4 tarefas em paralelo; cada uma com contexto e ferramentas proprios. */
static int subagent_many(const char *cwd, JValue *tasks, const char *model, int timeout_sec,
                         size_t max_chars, const char *child_cwd, Str *out, ToolResult *res)
{
    if (getenv("DELTA_SUBAGENT")) {
        err_out(res, "Erro: um subagente nao pode abrir outro subagente.");
        return 0;
    }
    const char *exe = self_exe();
    if (!exe) { err_out(res, "Erro: nao consegui localizar o proprio executavel."); return 0; }
    if (tasks->kind != J_ARR || tasks->count == 0) {
        err_out(res, "Erro: 'tasks' tem que ser uma lista de tarefas.");
        return 0;
    }
    if (timeout_sec <= 0) timeout_sec = 900;
    if (max_chars == 0) max_chars = 20000;
    size_t n = tasks->count > SUBAGENT_MAX ? SUBAGENT_MAX : tasks->count;
    if (tasks->count > SUBAGENT_MAX)
        str_addf(&res->output, "(%lu tarefas pedidas; rodando as %d primeiras)\n",
                 (unsigned long)tasks->count, SUBAGENT_MAX);

    char *jobs = job_dir();
    Child cs[SUBAGENT_MAX];
    memset(cs, 0, sizeof cs);
    size_t iniciados = 0;
    for (size_t i = 0; i < n; i++) {
        const char *task = json_str(json_at(tasks, i));
        if (!*task) continue;
        if (!child_start(&cs[i], exe, task, i + 1, model,
                         child_cwd ? child_cwd : cwd, jobs))
            continue;
        iniciados++;
    }
    for (size_t i = 0; i < n; i++) child_wait(&cs[i], timeout_sec);

    int falhou = 0;
    for (size_t i = 0; i < n; i++) {
        if (!cs[i].rodou) continue;
        str_addf(&res->output, "\n=== tarefa %lu (%.1f s, codigo %d) ===\n",
                 (unsigned long)(i + 1), (double)(now_ms() - cs[i].t0) / 1000.0, cs[i].exit_code);
        Str saida; str_init(&saida);
        child_output(&cs[i], max_chars, &saida);
        str_add(&res->output, saida.data, saida.len);
        str_free(&saida);
        if (cs[i].exit_code != 0) falhou = 1;
        child_free(&cs[i]);
    }
    free(jobs);
    if (falhou) res->is_error = 1;
    if (iniciados == 0) {
        err_out(res, "Erro: nenhuma tarefa pode ser iniciada.");
        return 0;
    }
    return 1;
}

static int agent_op(JValue *a, const char *cwd, ToolResult *res)
{
    const char *op = arg_s(a, "op");
    if (!*op) { err_out(res, "Erro: 'op' e' obrigatorio (plan|subagent|skill)."); return 0; }

    if (str_eq(op, "plan")) {
        const char *plan = arg_s(a, "plan_markdown");
        char *p = plan_path();
        int ok = file_write(p, plan, strlen(plan));
        free(p);
        if (!ok) { err_out(res, "Erro: nao foi possivel gravar o plano."); return 0; }
        int pend = 0, done = 0;
        for (const char *c = plan; (c = strstr(c, "- [")) != NULL; c += 3) {
            if (c[4] == ']') {
                if (c[3] == 'x' || c[3] == 'X') done++;
                else pend++;
            }
        }
        str_addf(&res->output, "Plan updated: %d pendentes, %d concluidos.\n", pend, done);
        return 1;
    }
    if (str_eq(op, "skill")) {
        const char *name = arg_s(a, "name");
        /* skills ficam em <cwd>/skills/<nome>/SKILL.md ou ~/.deltaCore/skills/<nome>/SKILL.md */
        char *home = home_get();
        char *dirs[2];
        dirs[0] = path_join(cwd, "skills");
        char *globdir = path_join(home, ".deltaCore/skills");
        dirs[1] = globdir;
        free(home);
        if (!*name) {
            int achou = 0;
            for (int d = 0; d < 2; d++) {
                size_t n = 0;
                char **list = dir_list(dirs[d], &n);
                for (size_t i = 0; i < n; i++) {
                    char *f = path_join(path_join(dirs[d], list[i]), "SKILL.md");
                    if (file_exists(f)) { str_addf(&res->output, "%s\n", list[i]); achou = 1; }
                    free(f);
                }
                dir_list_free(list, n);
            }
            if (!achou) str_adds(&res->output, "(nenhuma skill em skills/ ou ~/.deltaCore/skills)\n");
            free(dirs[0]); free(dirs[1]);
            return 1;
        }
        for (int d = 0; d < 2; d++) {
            char *f = path_join(path_join(dirs[d], name), "SKILL.md");
            size_t len = 0;
            char *txt = file_read(f, &len);
            free(f);
            if (txt) {
                str_add(&res->output, txt, len);
                free(txt);
                free(dirs[0]); free(dirs[1]);
                return 1;
            }
        }
        err_out(res, "Erro: skill '%s' nao encontrada em skills/ nem em ~/.deltaCore/skills.", name);
        free(dirs[0]); free(dirs[1]);
        return 0;
    }
    if (str_eq(op, "subagent")) {
        const char *task = arg_s(a, "task");
        if (!*task) { err_out(res, "Erro: 'task' e' obrigatorio."); return 0; }
        return subagent_run(cwd, task, arg_s(a, "model"),
                            arg_i(a, "timeout", 900), (size_t)arg_i(a, "max_output", 20000),
                            arg_s(a, "cwd"), &res->output, res);
    }
    if (str_eq(op, "subagents")) {
        JValue *tasks = json_get(a, "tasks");
        if (!tasks) { err_out(res, "Erro: 'tasks' (lista) e' obrigatorio em op=subagents."); return 0; }
        return subagent_many(cwd, tasks, arg_s(a, "model"),
                             arg_i(a, "timeout", 900), (size_t)arg_i(a, "max_output", 20000),
                             arg_s(a, "cwd"), &res->output, res);
    }
    err_out(res, "Erro: operacao desconhecida de agent: %s", op);
    return 0;
}

static const char AGENT_SCHEMA[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"op\":{\"type\":\"string\",\"enum\":[\"plan\",\"subagent\",\"subagents\",\"skill\"]},"
    "\"plan_markdown\":{\"type\":\"string\",\"description\":\"Plano em markdown com caixas - [ ] / - [x]\"},"
    "\"task\":{\"type\":\"string\",\"description\":\"Tarefa do subagente (op=subagent)\"},"
    "\"tasks\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},"
    "\"description\":\"Ate' 4 tarefas independentes para rodar em paralelo (op=subagents)\"},"
    "\"model\":{\"type\":\"string\",\"description\":\"Modelo do subagente (ex.: um barato para varrer, o forte para revisar)\"},"
    "\"timeout\":{\"type\":\"number\",\"description\":\"Segundos antes de matar o subagente (padrao 900)\"},"
    "\"max_output\":{\"type\":\"number\",\"description\":\"Teto de caracteres da saida (padrao 20000)\"},"
    "\"cwd\":{\"type\":\"string\",\"description\":\"Pasta onde o subagente trabalha (padrao: a atual)\"},"
    "\"name\":{\"type\":\"string\",\"description\":\"Nome da skill (vazio lista todas)\"}},"
    "\"required\":[\"op\"]}";

/* ------------------------------------------------------------------ */
/* extend: mcp, plugin                                                 */
/* ------------------------------------------------------------------ */
static int extend_op(JValue *a, const char *cwd, ToolResult *res)
{
    const char *op = arg_s(a, "op");
    if (!*op) { err_out(res, "Erro: 'op' e' obrigatorio (mcp|plugin)."); return 0; }

    if (str_eq(op, "mcp")) {
        const char *url = arg_s(a, "server_url");
        const char *tool = arg_s(a, "tool_name");
        if (!*url) { err_out(res, "Erro: 'server_url' e' obrigatorio."); return 0; }
        if (!*tool) { err_out(res, "Erro: 'tool_name' e' obrigatorio."); return 0; }
        const char *args_json = arg_s(a, "args_json");
        if (!*args_json) args_json = "{}";

        Str body; str_init(&body);
        str_adds(&body, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":");
        json_write_string(&body, tool, strlen(tool));
        str_adds(&body, ",\"arguments\":");
        str_adds(&body, args_json);
        str_adds(&body, "}}");

        Str hdrs; str_init(&hdrs);
        str_adds(&hdrs, "Content-Type: application/json\r\nAccept: application/json, text/event-stream\r\n");
        const char *extra = arg_s(a, "headers");
        if (*extra) str_addf(&hdrs, "%s\r\n", extra);

        Str resp; str_init(&resp);
        int ok = http_do("POST", url, hdrs.data, body.data, 60, 200000, &resp);
        if (!ok) err_out(res, "Erro: chamada MCP falhou.");
        else {
            const char *json = strstr(resp.data, "{");
            if (!json) { str_adds(&res->output, resp.data); ok = 1; }
            else {
                JValue *r = json_parse_cstr(json);
                JValue *error = json_get(r, "error");
                if (error) { err_out(res, "Erro do MCP: %s", json_str(json_get(error, "message"))); ok = 0; }
                else {
                    JValue *result = json_get(r, "result");
                    if (result) {
                        JValue *content = json_get(result, "content");
                        if (content && content->kind == J_ARR) {
                            for (size_t i = 0; i < content->count; i++) {
                                const char *txt = json_str(json_get(content->items[i], "text"));
                                if (*txt) str_addf(&res->output, "%s\n", txt);
                            }
                        } else {
                            /* devolve o result cru */
                            str_adds(&res->output, json + 0);
                        }
                    } else {
                        str_adds(&res->output, resp.data);
                    }
                    ok = 1;
                }
                json_free(r);
            }
        }
        str_free(&resp);
        str_free(&hdrs);
        str_free(&body);
        return ok;
    }
    if (str_eq(op, "plugin")) {
        const char *request = arg_s(a, "request");
        const char *hint = arg_s(a, "name_hint");
        if (!*request) { err_out(res, "Erro: 'request' e' obrigatorio."); return 0; }
        char nome[128];
        if (*hint) {
            snprintf(nome, sizeof nome, "%s", hint);
        } else {
            /* deriva um nome do pedido */
            size_t k = 0;
            for (const char *c = request; *c && k < sizeof nome - 1; c++) {
                if (isalnum((unsigned char)*c)) nome[k++] = (char)tolower((unsigned char)*c);
                else if (k && nome[k - 1] != '-') nome[k++] = '-';
            }
            while (k && nome[k - 1] == '-') k--;
            nome[k] = 0;
            if (!k) snprintf(nome, sizeof nome, "plugin");
        }
        char *dir = path_join(cwd, "plugins");
        dir_make(dir);
        char nomearq[160];
        snprintf(nomearq, sizeof nomearq, "%s.json", nome);
        char *path = path_join(dir, nomearq);
        Str out; str_init(&out);
        str_adds(&out, "{\n  \"name\": ");
        json_write_string(&out, nome, strlen(nome));
        str_adds(&out, ",\n  \"request\": ");
        json_write_string(&out, request, strlen(request));
        str_adds(&out, ",\n  \"created\": ");
        char ts[32];
        iso_now(ts);
        json_write_string(&out, ts, strlen(ts));
        str_adds(&out, ",\n  \"kind\": \"delta-plugin-scaffold\"\n}\n");
        int ok = file_write(path, out.data, out.len);
        if (ok) str_addf(&res->output, "Plugin scaffold criado: %s\n", path);
        else err_out(res, "Erro: nao foi possivel gravar %s.", path);
        str_free(&out);
        free(path);
        free(dir);
        return ok;
    }
    err_out(res, "Erro: operacao desconhecida de extend: %s", op);
    return 0;
}

static const char EXTEND_SCHEMA[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"op\":{\"type\":\"string\",\"enum\":[\"mcp\",\"plugin\"]},"
    "\"server_url\":{\"type\":\"string\",\"description\":\"Endpoint HTTP do servidor MCP\"},"
    "\"tool_name\":{\"type\":\"string\"},\"args_json\":{\"type\":\"string\"},"
    "\"headers\":{\"type\":\"string\"},\"request\":{\"type\":\"string\"},"
    "\"name_hint\":{\"type\":\"string\"}},"
    "\"required\":[\"op\"]}";

/* ------------------------------------------------------------------ */
/* handlers                                                            */
/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* skills: list / read / create / delete / search                      */
/* ------------------------------------------------------------------ */
static char *skills_root(const char *cwd, int escopo)
{
    if (escopo == 0) return path_join(cwd, "skills");        /* projeto */
    char *home = home_get();
    char *p;
    if (escopo == 2) p = path_join(home, ".agents/skills");  /* pi/agents (compartilhado) */
    else p = path_join(home, ".deltaCore/skills");             /* do Delta */
    free(home);
    return p;
}

static const char *skills_escopo_nome(int escopo)
{
    return escopo == 0 ? "" : (escopo == 2 ? " [agents]" : " [user]");
}


static int skill_nome_ok(const char *nome)
{
    if (!nome || !*nome) return 0;
    for (const char *p = nome; *p; p++)
        if (!(isalnum((unsigned char)*p) || *p == '-' || *p == '_')) return 0;
    return 1;
}

/* primeira linha util (description: ou texto) para o indice */
static void skill_desc(const char *conteudo, Str *out)
{
    const char *p = conteudo;
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t ll = eol ? (size_t)(eol - p) : strlen(p);
        while (ll && (p[ll - 1] == '\r' || isspace((unsigned char)p[ll - 1]))) ll--;
        if (ll && !(p[0] == '-' && p[1] == '-') && !(ll > 5 && ci_ncmp(p, "name:", 5) == 0)) {
            const char *v = p;
            size_t n = ll;
            if (n > 9 && ci_ncmp(v, "description", 11) == 0) {
                v = v + 11;
                while (*v == ':' || *v == ' ') v++;
                n = ll - (size_t)(v - p);
            }
            str_add(out, v, n);
            return;
        }
        if (!eol) break;
        p = eol + 1;
    }
}

/* indice para o prompt de sistema: "- nome: descricao" (limitado) */
char *tools_skills_index(const char *cwd, size_t max_chars)
{
    Str out; str_init(&out);
    for (int escopo = 0; escopo < 3 && out.len < max_chars; escopo++) {
        char *raiz = skills_root(cwd, escopo);
        size_t n = 0;
        char **lista = dir_list(raiz, &n);
        for (size_t i = 0; i < n && out.len < max_chars; i++) {
            char *dir = path_join(raiz, lista[i]);
            char *md = path_join(dir, "SKILL.md");
            size_t len = 0;
            char *txt = file_read(md, &len);
            if (txt) {
                Str d; str_init(&d);
                skill_desc(txt, &d);
                str_addf(&out, "- %s: %s\n", lista[i], (d.data && d.len) ? d.data : "(sem descricao)");
                str_free(&d);
                free(txt);
            }
            free(md);
            free(dir);
        }
        dir_list_free(lista, n);
        free(raiz);
    }
    return str_steal(&out);
}

static int skills_list(const char *cwd, int detalhe, ToolResult *res)
{
    int achou = 0;
    for (int escopo = 0; escopo < 3; escopo++) {
        char *raiz = skills_root(cwd, escopo);
        size_t n = 0;
        char **lista = dir_list(raiz, &n);
        for (size_t i = 0; i < n; i++) {
            char *dir = path_join(raiz, lista[i]);
            if (!path_is_dir(dir)) { free(dir); continue; }
            char *md = path_join(dir, "SKILL.md");
            size_t len = 0;
            char *txt = file_read(md, &len);
            if (txt) {
                achou = 1;
                Str d; str_init(&d);
                skill_desc(txt, &d);
                str_addf(&res->output, "%s%s%s%s\n", lista[i], skills_escopo_nome(escopo),
                         d.len ? " — " : "", d.data ? d.data : "");
                if (detalhe) str_addf(&res->output, "%s\n", md);
                str_free(&d);
                free(txt);
            }
            free(md);
            free(dir);
        }
        dir_list_free(lista, n);
        free(raiz);
    }
    if (!achou) str_adds(&res->output, "(nenhuma skill em skills/ ou ~/.deltaCore/skills)\n");
    return 1;
}

static int skills_op(JValue *a, const char *cwd, ToolResult *res)
{
    const char *op = arg_s(a, "op");
    const char *nome = arg_s(a, "name");
    if (!*op) op = "list";

    if (str_eq(op, "list")) return skills_list(cwd, arg_b(a, "detail", 0), res);

    if (str_eq(op, "read")) {
        if (!*nome) { err_out(res, "Erro: 'name' e' obrigatorio."); return 0; }
        for (int escopo = 0; escopo < 3; escopo++) {
            char *raiz = skills_root(cwd, escopo);
            char *dir = path_join(raiz, nome);
            char *md = path_join(dir, "SKILL.md");
            size_t len = 0;
            char *txt = file_read(md, &len);
            free(md); free(dir); free(raiz);
            if (txt) { str_add(&res->output, txt, len); free(txt); return 1; }
        }
        err_out(res, "Erro: skill '%s' nao encontrada.", nome);
        return 0;
    }
    if (str_eq(op, "create") || str_eq(op, "update")) {
        if (!skill_nome_ok(nome)) { err_out(res, "Erro: nome invalido (use letras, numeros, '-' e '_')."); return 0; }
        const char *conteudo = arg_s(a, "content");
        const char *desc = arg_s(a, "description");
        if (!*conteudo) { err_out(res, "Erro: 'content' e' obrigatorio."); return 0; }
        const char *esc = arg_s(a, "scope");
        int escopo = str_eq(esc, "agents") ? 2 : (str_eq(esc, "user") ? 1 : 0);
        char *raiz = skills_root(cwd, escopo);
        char *dir = path_join(raiz, nome);
        dir_make(dir);
        char *md = path_join(dir, "SKILL.md");
        Str out; str_init(&out);
        if (!(conteudo[0] == '-' && conteudo[1] == '-')) {
            str_adds(&out, "---\nname: ");
            str_adds(&out, nome);
            str_adds(&out, "\ndescription: ");
            str_adds(&out, *desc ? desc : nome);
            str_adds(&out, "\n---\n\n");
        }
        str_adds(&out, conteudo);
        int ok = file_write(md, out.data, out.len);
        if (ok) str_addf(&res->output, "Skill %s: %s\n", str_eq(op, "create") ? "criada" : "atualizada", md);
        else err_out(res, "Erro: nao consegui gravar %s.", md);
        str_free(&out);
        free(md); free(dir); free(raiz);
        return ok;
    }
    if (str_eq(op, "delete")) {
        if (!skill_nome_ok(nome)) { err_out(res, "Erro: nome invalido."); return 0; }
        int ok = 0;
        for (int escopo = 0; escopo < 3; escopo++) {
            char *raiz = skills_root(cwd, escopo);
            char *dir = path_join(raiz, nome);
            char *md = path_join(dir, "SKILL.md");
            if (file_exists(md)) { remove(md); ok = 1; }
#ifdef _WIN32
            if (ok) RemoveDirectoryA(dir);
#else
            if (ok) rmdir(dir);
#endif
            free(md); free(dir); free(raiz);
            if (ok) break;
        }
        if (ok) str_addf(&res->output, "Skill removida: %s\n", nome);
        else err_out(res, "Erro: skill '%s' nao encontrada.", nome);
        return ok;
    }
    if (str_eq(op, "search")) {
        const char *q = arg_s(a, "query");
        if (!*q) { err_out(res, "Erro: 'query' e' obrigatorio."); return 0; }
        int achou = 0;
        for (int escopo = 0; escopo < 3; escopo++) {
            char *raiz = skills_root(cwd, escopo);
            size_t n = 0;
            char **lista = dir_list(raiz, &n);
            for (size_t i = 0; i < n; i++) {
                char *dir = path_join(raiz, lista[i]);
                char *md = path_join(dir, "SKILL.md");
                size_t len = 0;
                char *txt = file_read(md, &len);
                if (txt) {
                    char *hit = strstr(txt, q);
                    if (hit) {
                        achou = 1;
                        const char *ini = hit > txt + 60 ? hit - 60 : txt;
                        size_t ctx = (size_t)(hit - ini) + strlen(q) + 60;
                        if (ctx > len - (size_t)(ini - txt)) ctx = len - (size_t)(ini - txt);
                        str_addf(&res->output, "%s/%s: ...%.*s...\n", lista[i],
                                 "SKILL.md", (int)ctx, ini);
                    }
                    free(txt);
                }
                free(md); free(dir);
            }
            dir_list_free(lista, n);
            free(raiz);
        }
        if (!achou) str_addf(&res->output, "(nenhuma skill contem \"%s\")\n", q);
        return 1;
    }
    err_out(res, "Erro: op desconhecida de skills: %s", op);
    return 0;
}

/* ------------------------------------------------------------------ */
/* mcp: registro de servidores + tools/list + tools/call                */
/* ------------------------------------------------------------------ */
#define MAX_MCP 32

typedef struct {
    char name[96];
    char url[1024];      /* HTTP */
    char headers[512];
    char command[512];   /* stdio: executável */
    char args[1024];     /* stdio: argumentos */
} McpConfig;

static char *mcp_path(void)
{
    char *dir = delta_home();
    dir_make(dir);
    char *p = path_join(dir, "mcp.json");
    free(dir);
    return p;
}

/* Le um arquivo de MCP: um objeto {"name","url","headers"} ou um array deles. */
static int mcp_load_file(const char *path, McpConfig *arr, int n, int max)
{
    size_t len = 0;
    char *txt = file_read(path, &len);
    if (!txt) return n;
    JValue *v = json_parse(txt, len);
    free(txt);
    if (!v) return n;

    size_t total = (v->kind == J_ARR) ? v->count : 1;
    for (size_t i = 0; i < total && n < max; i++) {
        JValue *it = (v->kind == J_ARR) ? v->items[i] : v;
        snprintf(arr[n].name, sizeof arr[n].name, "%s", json_str(json_get(it, "name")));
        snprintf(arr[n].url, sizeof arr[n].url, "%s", json_str(json_get(it, "url")));
        snprintf(arr[n].headers, sizeof arr[n].headers, "%s", json_str(json_get(it, "headers")));
        snprintf(arr[n].command, sizeof arr[n].command, "%s", json_str(json_get(it, "command")));
        {
            JValue *aargs = json_get(it, "args");
            if (aargs && aargs->kind == J_ARR) {
                size_t k = 0;
                arr[n].args[0] = 0;
                for (size_t j = 0; j < aargs->count; j++) {
                    const char *v = json_str(aargs->items[j]);
                    if (!*v) continue;
                    k += (size_t)snprintf(arr[n].args + k, sizeof arr[n].args - k, "%s%s",
                                          k ? " " : "", v);
                    if (k >= sizeof arr[n].args) break;
                }
            } else {
                snprintf(arr[n].args, sizeof arr[n].args, "%s", json_str(aargs));
            }
        }
        if (arr[n].url[0] || arr[n].command[0]) n++;
    }
    json_free(v);
    return n;
}

/* Servidores MCP: ~/.deltaCore/mcp.json + cada arquivo em ~/.deltaCore/mcps/. */
static int mcp_load(McpConfig *arr, int max)
{
    int n = 0;
    char *p = mcp_path();
    n = mcp_load_file(p, arr, n, max);
    free(p);

    char *home = home_get();
    char *dir = path_join(home, ".deltaCore/mcps");
    free(home);
    dir_make(dir);
    size_t cnt = 0;
    char **files = dir_list(dir, &cnt);
    for (size_t i = 0; files && i < cnt && n < max; i++) {
        const char *nm = files[i];
        size_t nl = strlen(nm);
        if (nl < 6 || !str_ieq(nm + nl - 5, ".json")) continue;
        char *fp = path_join(dir, nm);
        n = mcp_load_file(fp, arr, n, max);
        free(fp);
    }
    if (files) dir_list_free(files, cnt);
    free(dir);
    return n;
}

static int mcp_save(McpConfig *arr, int n)
{
    Str out; str_init(&out);
    str_addc(&out, '[');
    for (int i = 0; i < n; i++) {
        if (i) str_addc(&out, ',');
        str_adds(&out, "{\"name\":");
        json_write_string(&out, arr[i].name, strlen(arr[i].name));
        str_adds(&out, ",\"url\":");
        json_write_string(&out, arr[i].url, strlen(arr[i].url));
        str_adds(&out, ",\"headers\":");
        json_write_string(&out, arr[i].headers, strlen(arr[i].headers));
        if (arr[i].command[0]) {
            str_adds(&out, ",\"command\":");
            json_write_string(&out, arr[i].command, strlen(arr[i].command));
            str_adds(&out, ",\"args\":");
            json_write_string(&out, arr[i].args, strlen(arr[i].args));
        }
        str_addc(&out, '}');
    }
    str_addc(&out, ']');
    char *p = mcp_path();
    int ok = file_write(p, out.data, out.len);
    free(p);
    str_free(&out);
    return ok;
}

static void jsonrpc(Str *out, int id, const char *method, const char *params_json)
{
    str_addf(out, "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":", id);
    json_write_string(out, method, strlen(method));
    if (params_json && *params_json) {
        str_adds(out, ",\"params\":");
        str_adds(out, params_json);
    }
    str_addc(out, '}');
}

/* devolve o objeto JSON da resposta (trata SSE e roda o 1o objeto) */
static JValue *mcp_resposta(const char *resp)
{
    const char *p = resp;
    while (p && *p) {
        const char *b = strchr(p, '{');
        if (!b) break;
        const char *f = json_fim(b);
        if (!f) break;
        JValue *v = json_parse(b, (size_t)(f - b));
        if (v) return v;
        p = b + 1;
    }
    return NULL;
}

static int mcp_chamar(const char *url, const char *headers, const char *corpo, Str *resp, ToolResult *res)
{
    Str hdrs; str_init(&hdrs);
    str_adds(&hdrs, "Content-Type: application/json\r\nAccept: application/json, text/event-stream\r\n");
    if (headers && *headers) str_addf(&hdrs, "%s\r\n", headers);
    int ok = http_do("POST", url, hdrs.data, corpo, 60, 400000, resp);
    str_free(&hdrs);
    if (!ok) { err_out(res, "Erro: falha ao falar com o servidor MCP (%s).", url); return 0; }
    return 1;
}

/* transporte stdio (definido mais abaixo) */
static int mcp_stdio_op(const McpConfig *c, const char *op, const char *tool,
                        const char *args_json, ToolResult *res);

static int mcp_op(JValue *a, const char *cwd, ToolResult *res)
{
    (void)cwd;
    const char *op = arg_s(a, "op");
    if (!*op) op = "list";
    McpConfig cfg[MAX_MCP];
    int n = mcp_load(cfg, MAX_MCP);

    if (str_eq(op, "list")) {
        if (!n) { str_adds(&res->output, "(nenhum servidor MCP registrado — use op=add)\n"); return 1; }
        for (int i = 0; i < n; i++) str_addf(&res->output, "%s  %s\n", cfg[i].name, cfg[i].url);
        return 1;
    }
    if (str_eq(op, "add")) {
        const char *url = arg_s(a, "url");
        const char *nome = arg_s(a, "name");
        const char *cmdv = arg_s(a, "command");
        const char *argsv = arg_s(a, "args");
        if (!*url && !*cmdv) { err_out(res, "Erro: informe 'url' (HTTP) ou 'command' (stdio)."); return 0; }
        if (!*nome && *cmdv) nome = "stdio";
        char derivado[96] = { 0 };
        if (!*nome) {
            const char *h = strstr(url, "://");
            h = h ? h + 3 : url;
            size_t k = 0;
            while (*h && *h != '/' && *h != ':' && k + 1 < sizeof derivado) derivado[k++] = *h++;
            nome = derivado;
        }
        if (n >= MAX_MCP) { err_out(res, "Erro: limite de %d servidores.", MAX_MCP); return 0; }
        for (int i = 0; i < n; i++) if (str_eq(cfg[i].name, nome)) { n = i; break; }
        snprintf(cfg[n].name, sizeof cfg[n].name, "%s", nome);
        snprintf(cfg[n].url, sizeof cfg[n].url, "%s", url);
        snprintf(cfg[n].headers, sizeof cfg[n].headers, "%s", arg_s(a, "headers"));
        snprintf(cfg[n].command, sizeof cfg[n].command, "%s", cmdv);
        snprintf(cfg[n].args, sizeof cfg[n].args, "%s", argsv);
        n++;
        if (!mcp_save(cfg, n)) { err_out(res, "Erro: nao consegui gravar a configuracao."); return 0; }
        str_addf(&res->output, "MCP registrado: %s -> %s\n", cfg[n - 1].name,
                 *url ? url : cfg[n - 1].command);
    }
    if (str_eq(op, "remove")) {
        const char *nome = arg_s(a, "name");
        int achou = 0, k = 0;
        for (int i = 0; i < n; i++) {
            if (str_eq(cfg[i].name, nome)) { achou = 1; continue; }
            cfg[k++] = cfg[i];
        }
        if (!achou) { err_out(res, "Erro: servidor '%s' nao registrado.", nome); return 0; }
        if (!mcp_save(cfg, k)) { err_out(res, "Erro: nao consegui gravar a configuracao."); return 0; }
        str_addf(&res->output, "MCP removido: %s\n", nome);
        return 1;
    }

    /* tools / call: precisa de um servidor (por nome ou url inline) */
    const char *nome = arg_s(a, "name");
    const char *url = arg_s(a, "url");
    const char *headers = arg_s(a, "headers");
    McpConfig alvo;
    memset(&alvo, 0, sizeof alvo);
    if (*url) { snprintf(alvo.url, sizeof alvo.url, "%s", url); snprintf(alvo.headers, sizeof alvo.headers, "%s", headers); }
    else {
        for (int i = 0; i < n; i++) if (str_eq(cfg[i].name, nome)) { alvo = cfg[i]; break; }
    }
    if (!alvo.url[0] && !alvo.command[0]) {
        err_out(res, "Erro: informe 'name' (registrado com op=add), 'url' ou 'command'.");
        return 0;
    }
    /* servidor por stdio (command/args): sobe o processo e fala JSON-RPC */
    if (!alvo.url[0] && alvo.command[0]) {
        if (str_eq(op, "tools"))
            return mcp_stdio_op(&alvo, "tools", "", "{}", res);
        if (str_eq(op, "call"))
            return mcp_stdio_op(&alvo, "call", arg_s(a, "tool"), arg_s(a, "args_json"), res);
    }

    /* initialize (pega Mcp-Session-Id, se o servidor exigir) */
    Str corpo; str_init(&corpo);
    Str resp; str_init(&resp);
    jsonrpc(&corpo, 1, "initialize",
            "{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{},\"clientInfo\":{\"name\":\"delta-agent\",\"version\":\"1.0\"}}");
    if (!mcp_chamar(alvo.url, alvo.headers, corpo.data, &resp, res)) { str_free(&corpo); str_free(&resp); return 0; }
    char sessao[320] = { 0 };
    if (g_last_session[0]) snprintf(sessao, sizeof sessao, "Mcp-Session-Id: %s\r\n", g_last_session);
    Str h2; str_init(&h2);
    if (alvo.headers[0]) str_addf(&h2, "%s\r\n", alvo.headers);
    if (sessao[0]) str_adds(&h2, sessao);

    /* notifications/initialized (sem id) */
    str_clear(&corpo);
    str_clear(&resp);
    jsonrpc(&corpo, 0, "notifications/initialized", NULL);
    {
        Str tmp; str_init(&tmp);
        (void)mcp_chamar(alvo.url, h2.data, corpo.data, &tmp, res);
        str_free(&tmp);
    }

    if (str_eq(op, "tools")) {
        str_clear(&corpo); str_clear(&resp);
        jsonrpc(&corpo, 2, "tools/list", "{}");
        if (!mcp_chamar(alvo.url, h2.data, corpo.data, &resp, res)) { str_free(&h2); str_free(&corpo); str_free(&resp); return 0; }
        JValue *v = mcp_resposta(resp.data ? resp.data : "");
        JValue *result = v ? json_get(v, "result") : NULL;
        JValue *tools = result ? json_get(result, "tools") : NULL;
        if (tools && tools->kind == J_ARR) {
            str_addf(&res->output, "%lu tool(s):\n", (unsigned long)tools->count);
            for (size_t i = 0; i < tools->count; i++) {
                JValue *t = tools->items[i];
                str_addf(&res->output, "- %s: %.200s\n", json_str(json_get(t, "name")),
                         json_str(json_get(t, "description")));
            }
        } else {
            JValue *erro = v ? json_get(v, "error") : NULL;
            err_out(res, "Erro do MCP: %s", erro ? json_str(json_get(erro, "message")) : "resposta inesperada");
            if (v) json_free(v);
            str_free(&h2); str_free(&corpo); str_free(&resp);
            return 0;
        }
        json_free(v);
        str_free(&h2); str_free(&corpo); str_free(&resp);
        return 1;
    }
    if (str_eq(op, "call")) {
        const char *tool = arg_s(a, "tool");
        const char *args_json = arg_s(a, "args_json");
        if (!*tool) { err_out(res, "Erro: 'tool' e' obrigatorio."); return 0; }
        if (!*args_json) args_json = "{}";
        Str params; str_init(&params);
        str_adds(&params, "{\"name\":");
        json_write_string(&params, tool, strlen(tool));
        str_adds(&params, ",\"arguments\":");
        str_adds(&params, args_json);
        str_addc(&params, '}');
        str_clear(&corpo); str_clear(&resp);
        jsonrpc(&corpo, 3, "tools/call", params.data);
        int ok = mcp_chamar(alvo.url, h2.data, corpo.data, &resp, res);
        str_free(&params);
        if (!ok) { str_free(&h2); str_free(&corpo); str_free(&resp); return 0; }
        JValue *v = mcp_resposta(resp.data ? resp.data : "");
        JValue *erro = v ? json_get(v, "error") : NULL;
        if (erro) {
            err_out(res, "Erro do MCP: %s", json_str(json_get(erro, "message")));
            json_free(v); str_free(&h2); str_free(&corpo); str_free(&resp);
            return 0;
        }
        JValue *result = v ? json_get(v, "result") : NULL;
        JValue *content = result ? json_get(result, "content") : NULL;
        if (content && content->kind == J_ARR) {
            for (size_t i = 0; i < content->count; i++) {
                const char *txt = json_str(json_get(content->items[i], "text"));
                if (*txt) str_addf(&res->output, "%s\n", txt);
            }
        } else if (resp.len) {
            str_add(&res->output, resp.data, resp.len);
        }
        if (v) json_free(v);
        str_free(&h2); str_free(&corpo); str_free(&resp);
        return 1;
    }
    err_out(res, "Erro: op desconhecida de mcp: %s", op);
    str_free(&h2); str_free(&corpo); str_free(&resp);
    return 0;
}

/* ------------------------------------------------------------------ */
/* memoria automatica: roda quando o usuario manda a mensagem          */
/* ------------------------------------------------------------------ */
static int contem_ci(const char *hay, const char *needle)
{
    if (!hay || !needle || !*needle) return 0;
    size_t nl = strlen(needle);
    for (const char *p = hay; *p; p++)
        if (!ci_ncmp(p, needle, nl)) return 1;
    return 0;
}

static int memoria_tem(const JValue *arr, const char *texto, const char *kind)
{
    for (size_t i = 0; i < arr->count; i++) {
        JValue *n = arr->items[i];
        if (str_eq(json_str(json_get(n, "content")), texto) &&
            str_eq(json_str(json_get(n, "kind")), kind))
            return 1;
    }
    return 0;
}

static void memoria_add(JValue *arr, const char *title, const char *content, const char *kind, const char *scope)
{
    if (memoria_tem(arr, content, kind)) return;
    JValue *n = memory_new_note(title, content, kind, scope, "[]");
    arr->items = (JValue **)realloc(arr->items, (arr->count + 1) * sizeof(JValue *));
    arr->items[arr->count++] = n;
}

/* guarda a mensagem do usuario e extrai o que parece duradouro */
void tools_auto_memorize(const char *texto)
{
    if (!texto || !*texto) return;
    if (str_starts(texto, "/")) return;          /* comando nao vira memoria */

    JValue *arr = memory_load();

    /* 1) diario: a mensagem crua (limitada) */
    char resumo[120];
    size_t k = 0;
    for (const char *p = texto; *p && k + 1 < sizeof resumo; p++) {
        if (*p == '\n' || *p == '\r') break;
        resumo[k++] = *p;
    }
    resumo[k] = 0;
    char corpo[2048];
    snprintf(corpo, sizeof corpo, "%.2000s", texto);
    memoria_add(arr, resumo[0] ? resumo : "(mensagem)", corpo, "user", "session");

    /* 2) heuristicas de fato duravel */
    static const char *marcas[] = {
        "sempre", "nunca", "prefiro", "preferimos", "prefere", "nao use", "não use",
        "use sempre", "lembre", "memorize", "anote", "guarde", "salve", "regra",
        "a partir de agora", "de agora em diante", "nosso padrao", "nosso padrão",
        "convencao", "convenção", "o projeto usa", "trabalhamos com", NULL
    };
    const char *p = texto;
    while (*p) {
        const char *fim = p;
        while (*fim && *fim != '.' && *fim != '!' && *fim != '?' && *fim != '\n') fim++;
        size_t ll = (size_t)(fim - p);
        if (ll >= 12 && ll < 400) {
            for (int i = 0; marcas[i]; i++) {
                if (contem_ci(p, marcas[i])) {
                    char frase[420];
                    snprintf(frase, sizeof frase, "%.400s", p);
                    memoria_add(arr, "preferencia", frase, "preference", "global");
                    break;
                }
            }
        }
        p = *fim ? fim + 1 : fim;
    }

    /* 3) poda o diario: mantem as 300 mensagens mais recentes */
    int users = 0;
    for (size_t i = 0; i < arr->count; i++)
        if (str_eq(json_str(json_get(arr->items[i], "kind")), "user")) users++;
    if (users > 300) {
        int remover = users - 300;
        size_t k2 = 0;
        for (size_t i = 0; i < arr->count; i++) {
            if (remover > 0 && str_eq(json_str(json_get(arr->items[i], "kind")), "user")) {
                json_free(arr->items[i]);
                remover--;
                continue;
            }
            arr->items[k2++] = arr->items[i];
        }
        arr->count = k2;
    }

    memory_save(arr);
    json_free(arr);
}


static int tool_fs(Tool *self, JValue *a, const char *cwd, ToolResult *res)      { (void)self; return fs_op(a, cwd, res); }
static int tool_git(Tool *self, JValue *a, const char *cwd, ToolResult *res)     { (void)self; return git_op(a, cwd, res); }
static int tool_shell(Tool *self, JValue *a, const char *cwd, ToolResult *res)   { (void)self; return shell_op(a, cwd, res); }
static int tool_web(Tool *self, JValue *a, const char *cwd, ToolResult *res)     { (void)self; return web_op(a, cwd, res); }
static int tool_memory(Tool *self, JValue *a, const char *cwd, ToolResult *res)  { (void)self; return memory_op(a, cwd, res); }
static int tool_code(Tool *self, JValue *a, const char *cwd, ToolResult *res)    { (void)self; return code_op(a, cwd, res); }
static int tool_assert(Tool *self, JValue *a, const char *cwd, ToolResult *res)  { (void)self; return assert_op(a, cwd, res); }
static int tool_agent(Tool *self, JValue *a, const char *cwd, ToolResult *res)   { (void)self; return agent_op(a, cwd, res); }
static int tool_extend(Tool *self, JValue *a, const char *cwd, ToolResult *res)  { (void)self; return extend_op(a, cwd, res); }
static int tool_skills(Tool *self, JValue *a, const char *cwd, ToolResult *res)  { (void)self; return skills_op(a, cwd, res); }
/* ------------------------------------------------------------------ */
/* MCP por stdio: sobe o servidor, manda initialize + o pedido, lê a   */
/* resposta com o id pedido e encerra o processo.                      */
/* ------------------------------------------------------------------ */
static void mcp_line_for(Str *out, int id, const char *method, const char *params)
{
    str_addf(out, "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"", id);
    str_adds(out, method);
    str_adds(out, "\",\"params\":");
    str_adds(out, params ? params : "{}");
    str_adds(out, "}\n");
}

/* procura "id":N aceitando espaços ("id": 2, "id" : 2) */
static int mcp_has_id(const char *buf, int id)
{
    char num[24];
    snprintf(num, sizeof num, "%d", id);
    const char *p = buf;
    while ((p = strstr(p, "\"id\"")) != NULL) {
        const char *q = p + 4;
        while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') q++;
        if (*q != ':') { p += 4; continue; }
        q++;
        while (*q == ' ' || *q == '\t') q++;
        size_t l = strlen(num);
        if (!strncmp(q, num, l)) {
            char depois = q[l];
            if (depois == ',' || depois == '}' || depois == ' ' || depois == '\r' || depois == '\n')
                return 1;
        }
        p += 4;
    }
    return 0;
}

#ifdef _WIN32
static int mcp_stdio_run(const McpConfig *c, int want_id, const char *params,
                         Str *resp, Str *erro)
{
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof sa;
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    HANDLE inR = NULL, inW = NULL, outR = NULL, outW = NULL;
    if (!CreatePipe(&inR, &inW, &sa, 0)) return 0;
    if (!CreatePipe(&outR, &outW, &sa, 0)) { CloseHandle(inR); CloseHandle(inW); return 0; }
    SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    memset(&pi, 0, sizeof pi);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = inR;
    si.hStdOutput = outW;
    si.hStdError = outW;

    Str cmd; str_init(&cmd);
    str_adds(&cmd, "\"");
    str_adds(&cmd, c->command);
    str_adds(&cmd, "\"");
    if (c->args[0]) { str_adds(&cmd, " "); str_adds(&cmd, c->args); }
    char *mut = str_dup(cmd.data);
    str_free(&cmd);

    int ok = CreateProcessA(NULL, mut, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    free(mut);
    CloseHandle(inR);
    CloseHandle(outW);
    if (!ok) {
        CloseHandle(inW); CloseHandle(outR);
        str_adds(erro, "nao consegui iniciar o servidor MCP (command/args)");
        return 0;
    }

    Str req; str_init(&req);
    mcp_line_for(&req, 1, "initialize",
        "{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{},\"clientInfo\":{\"name\":\"delta-agent\",\"version\":\"1.0\"}}");
    mcp_line_for(&req, 0, "notifications/initialized", "{}");
    {
        char metodo[64] = "";
        const char *p = params;
        size_t i = 0;
        while (*p && *p != '\n' && i < sizeof metodo - 1) metodo[i++] = *p++;
        metodo[i] = 0;
        if (*p == '\n') p++;
        if (metodo[0]) mcp_line_for(&req, want_id, metodo, p);
    }
    DWORD wr = 0;
    WriteFile(inW, req.data, (DWORD)req.len, &wr, NULL);
    str_free(&req);
    CloseHandle(inW);

    uint64_t t0 = now_ms();
    char buf[4096];
    Str acc; str_init(&acc);
    for (;;) {
        if (now_ms() - t0 > 20000) break;
        DWORD avail = 0;
        if (!PeekNamedPipe(outR, NULL, 0, NULL, &avail, NULL)) break;
        if (avail > 0) {
            DWORD got = 0;
            DWORD want = avail > sizeof buf ? (DWORD)sizeof buf : avail;
            if (!ReadFile(outR, buf, want, &got, NULL) || got == 0) break;
            str_add(&acc, buf, got);
            if (mcp_has_id(acc.data, want_id)) break;
        } else {
            if (WaitForSingleObject(pi.hProcess, 30) == WAIT_OBJECT_0) {
                DWORD a2 = 0;
                while (PeekNamedPipe(outR, NULL, 0, NULL, &a2, NULL) && a2 > 0) {
                    DWORD got = 0;
                    DWORD want = a2 > sizeof buf ? (DWORD)sizeof buf : a2;
                    if (!ReadFile(outR, buf, want, &got, NULL) || got == 0) break;
                    str_add(&acc, buf, got);
                }
                break;
            }
        }
    }
    TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(outR);

    int achou = 0;
    size_t pos = 0;
    while (pos < acc.len) {
        size_t eol = pos;
        while (eol < acc.len && acc.data[eol] != '\n') eol++;
        char save = acc.data[eol];
        acc.data[eol] = 0;
        if (mcp_has_id(acc.data + pos, want_id)) {
            str_add(resp, acc.data + pos, eol - pos);
            achou = 1;
        }
        acc.data[eol] = save;
        pos = eol + 1;
        if (achou) break;
    }
    if (!achou) {
        str_adds(erro, "servidor MCP nao respondeu (stdio)");
        if (acc.len) { str_adds(erro, ": "); str_add(erro, acc.data, acc.len > 300 ? 300 : acc.len); }
    }
    str_free(&acc);
    return achou;
}
#else
static int mcp_stdio_run(const McpConfig *c, int want_id, const char *params,
                         Str *resp, Str *erro)
{
    /* POSIX: uma execução por pedido, stdin por arquivo (servidores one-shot) */
    char tpl[128], treq[160], tresp[160];
    snprintf(tpl, sizeof tpl, "/tmp/delta-mcp-%lu", (unsigned long)getpid());
    snprintf(treq, sizeof treq, "%s.in", tpl);
    snprintf(tresp, sizeof tresp, "%s.out", tpl);
    Str req; str_init(&req);
    mcp_line_for(&req, 1, "initialize",
        "{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{},\"clientInfo\":{\"name\":\"delta-agent\",\"version\":\"1.0\"}}");
    mcp_line_for(&req, 0, "notifications/initialized", "{}");
    {
        char metodo[64] = "";
        const char *p = params;
        size_t i = 0;
        while (*p && *p != '\n' && i < sizeof metodo - 1) metodo[i++] = *p++;
        metodo[i] = 0;
        if (*p == '\n') p++;
        if (metodo[0]) mcp_line_for(&req, want_id, metodo, p);
    }
    file_write(treq, req.data, req.len);
    str_free(&req);
    Str cmd; str_init(&cmd);
    str_addf(&cmd, "%s %s < '%s' > '%s' 2>/dev/null", c->command, c->args, treq, tresp);
    int rc = system(cmd.data);
    str_free(&cmd);
    size_t len = 0;
    char *txt = file_read(tresp, &len);
    remove(treq);
    remove(tresp);
    if (!txt) { str_adds(erro, "servidor MCP nao respondeu (stdio)"); return 0; }
    char *hit = NULL;
    for (char *p = txt; *p; p++) if (mcp_has_id(p, want_id)) { hit = p; break; }
    int ok = 0;
    if (hit) {
        char *start = hit;
        while (start > txt && start[-1] != '\n') start--;
        char *end = strchr(start, '\n');
        str_add(resp, start, end ? (size_t)(end - start) : strlen(start));
        ok = 1;
    }
    if (!ok) str_adds(erro, "servidor MCP nao respondeu (stdio)");
    free(txt);
    (void)rc;
    return ok;
}
#endif

/* Executa o pedido via stdio e imprime tools/list ou tools/call. */
static int mcp_stdio_op(const McpConfig *c, const char *op, const char *tool,
                        const char *args_json, ToolResult *res)
{
    Str params; str_init(&params);
    int id = 2;
    if (str_eq(op, "tools")) {
        str_adds(&params, "tools/list\n{}");
    } else {
        id = 3;
        str_adds(&params, "tools/call\n{\"name\":");
        json_write_string(&params, tool, strlen(tool));
        str_adds(&params, ",\"arguments\":");
        str_adds(&params, args_json && *args_json ? args_json : "{}");
        str_addc(&params, '}');
    }
    Str resp; str_init(&resp);
    Str err; str_init(&err);
    int ok = mcp_stdio_run(c, id, params.data, &resp, &err);
    str_free(&params);
    if (!ok) {
        err_out(res, "%s", err.data ? err.data : "falha no MCP stdio");
        str_free(&resp);
        str_free(&err);
        return 0;
    }
    str_free(&err);
    JValue *v = mcp_resposta(resp.data ? resp.data : "");
    JValue *erro = v ? json_get(v, "error") : NULL;
    if (erro) {
        err_out(res, "Erro do MCP: %s", json_str(json_get(erro, "message")));
        json_free(v);
        str_free(&resp);
        return 0;
    }
    JValue *result = v ? json_get(v, "result") : NULL;
    if (str_eq(op, "tools")) {
        JValue *tools = result ? json_get(result, "tools") : NULL;
        if (tools && tools->kind == J_ARR) {
            str_addf(&res->output, "%lu tool(s):\n", (unsigned long)tools->count);
            for (size_t i = 0; i < tools->count; i++) {
                JValue *t = tools->items[i];
                str_addf(&res->output, "- %s: %.200s\n", json_str(json_get(t, "name")),
                         json_str(json_get(t, "description")));
            }
            json_free(v);
            str_free(&resp);
            return 1;
        }
        err_out(res, "resposta inesperada do MCP stdio");
        json_free(v);
        str_free(&resp);
        return 0;
    }
    JValue *content = result ? json_get(result, "content") : NULL;
    if (content && content->kind == J_ARR) {
        for (size_t i = 0; i < content->count; i++) {
            const char *txt = json_str(json_get(content->items[i], "text"));
            if (*txt) str_adds(&res->output, txt);
        }
    } else if (result) {
        str_adds(&res->output, "(resultado sem content textual)\n");
    }
    json_free(v);
    str_free(&resp);
    return 1;
}

static int tool_mcp(Tool *self, JValue *a, const char *cwd, ToolResult *res)     { (void)self; return mcp_op(a, cwd, res); }

/* ------------------------------------------------------------------ */
/* registro                                                            */
/* ------------------------------------------------------------------ */
static const char SKILLS_SCHEMA[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"op\":{\"type\":\"string\",\"enum\":[\"list\",\"read\",\"create\",\"update\",\"delete\",\"search\"]},"
    "\"name\":{\"type\":\"string\",\"description\":\"nome da skill (letras, numeros, - e _)\"},"
    "\"content\":{\"type\":\"string\",\"description\":\"markdown da skill (create/update)\"},"
    "\"description\":{\"type\":\"string\"},"
    "\"scope\":{\"type\":\"string\",\"enum\":[\"project\",\"user\",\"agents\"]},"
    "\"query\":{\"type\":\"string\"},"
    "\"detail\":{\"type\":\"boolean\"}},"
    "\"required\":[\"op\"]}";

static const char MCP_SCHEMA[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"op\":{\"type\":\"string\",\"enum\":[\"list\",\"add\",\"remove\",\"tools\",\"call\"]},"
    "\"name\":{\"type\":\"string\",\"description\":\"nome do servidor registrado\"},"
    "\"url\":{\"type\":\"string\",\"description\":\"endpoint HTTP do MCP (add ou avulso)\"},"
    "\"command\":{\"type\":\"string\",\"description\":\"executavel do servidor MCP via stdio\"},"
    "\"args\":{\"type\":\"string\",\"description\":\"argumentos do comando stdio\"},"
    "\"headers\":{\"type\":\"string\",\"description\":\"cabecalhos extras, um por linha\"},"
    "\"tool\":{\"type\":\"string\",\"description\":\"ferramenta do MCP para op=call\"},"
    "\"args_json\":{\"type\":\"string\",\"description\":\"argumentos JSON de op=call\"}},"
    "\"required\":[\"op\"]}";

static const Tool EXT_TOOLS[] = {
    { "fs",     "Files (unified): list read write append replace mkdir delete search glob", FS_SCHEMA, tool_fs },
    { "git",    "Git (unified): status diff log checkpoint", GIT_SCHEMA, tool_git },
    { "shell",  "Shell (unified): run start list tail cancel (background jobs)", SHELL_SCHEMA, tool_shell },
    { "web",    "Web (unified): http scrape browser (open/links/follow/back) data (json/api embutido)", WEB_SCHEMA, tool_web },
    { "memory", "Memory (unified): remember update search list link unlink query insert", MEMORY_SCHEMA, tool_memory },
    { "code",   "Code (unified): structure symbol tests", CODE_SCHEMA, tool_code },
    { "assert", "Assertions: equals contains", ASSERT_SCHEMA, tool_assert },
    { "agent",  "Agent (unified): plan subagent skill", AGENT_SCHEMA, tool_agent },
    { "extend", "Extensions (unified): mcp plugin", EXTEND_SCHEMA, tool_extend },
    { "skills", "Skills: list read create update delete search (SKILL.md)", SKILLS_SCHEMA, tool_skills },
    { "mcp",    "MCP servers: list add remove tools call (~/.deltaCore/mcp.json)", MCP_SCHEMA, tool_mcp },
    { NULL, NULL, NULL, NULL }
};

const Tool *tools_ext_all(void) { return EXT_TOOLS; }
