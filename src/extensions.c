/* extensions.c - registro de extensoes do Delta
 *
 * Uma extensao pode ser:
 *   kind=tool     -> comando externo que vira ferramenta (arg = linha de comando,
 *                    recebe o JSON dos argumentos como 1o parametro)
 *   kind=skill    -> SKILL.md (arg = conteudo markdown)
 *   kind=command  -> comando /nome na TUI (arg = prompt enviado)
 *   kind=mcp      -> servidor MCP (arg = URL; headers opcionais em "desc")
 *
 * Tudo fica em ~/.deltaCore/extensions.json, com "enabled" persistido entre sessoes.
 */
#include "tools.h"
#include "json.h"
#include "agent.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define EXT_MAX 128

static const char *ext_arg_s(JValue *a, const char *k)
{
    const char *v = json_str(json_get(a, k));
    return v ? v : "";
}

static void ext_err(ToolResult *r, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[1024];
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    str_adds(&r->output, buf);
    r->is_error = 1;
}

typedef struct {
    char kind[12];
    char name[64];
    char arg[2048];
    char desc[240];
    char schema[512];
    int  enabled;
} Ext;

static char *ext_path(void)
{
    char *dir = delta_home();
    dir_make(dir);
    char *p = path_join(dir, "extensions.json");
    free(dir);
    return p;
}

static int ext_load(Ext *arr, int max)
{
    char *p = ext_path();
    size_t len = 0;
    char *txt = file_read(p, &len);
    free(p);
    if (!txt) return 0;
    JValue *v = json_parse(txt, len);
    free(txt);
    int n = 0;
    if (v && v->kind == J_ARR) {
        for (size_t i = 0; i < v->count && n < max; i++) {
            JValue *it = v->items[i];
            memset(&arr[n], 0, sizeof arr[n]);
            snprintf(arr[n].kind, sizeof arr[n].kind, "%s", json_str(json_get(it, "kind")));
            snprintf(arr[n].name, sizeof arr[n].name, "%s", json_str(json_get(it, "name")));
            snprintf(arr[n].arg, sizeof arr[n].arg, "%s", json_str(json_get(it, "arg")));
            snprintf(arr[n].desc, sizeof arr[n].desc, "%s", json_str(json_get(it, "desc")));
            snprintf(arr[n].schema, sizeof arr[n].schema, "%s", json_str(json_get(it, "schema")));
            JValue *e = json_get(it, "enabled");
            arr[n].enabled = e ? json_bool(e, 1) : 1;
            if (arr[n].name[0] && arr[n].kind[0]) n++;
        }
    }
    json_free(v);
    return n;
}

static int ext_save(const Ext *arr, int n)
{
    Str out; str_init(&out);
    str_addc(&out, '[');
    for (int i = 0; i < n; i++) {
        if (i) str_addc(&out, ',');
        str_adds(&out, "{\"kind\":");
        json_write_string(&out, arr[i].kind, strlen(arr[i].kind));
        str_adds(&out, ",\"name\":");
        json_write_string(&out, arr[i].name, strlen(arr[i].name));
        str_adds(&out, ",\"arg\":");
        json_write_string(&out, arr[i].arg, strlen(arr[i].arg));
        str_adds(&out, ",\"desc\":");
        json_write_string(&out, arr[i].desc, strlen(arr[i].desc));
        str_adds(&out, ",\"schema\":");
        json_write_string(&out, arr[i].schema, strlen(arr[i].schema));
        str_addf(&out, ",\"enabled\":%s}", arr[i].enabled ? "true" : "false");
    }
    str_addc(&out, ']');
    char *p = ext_path();
    int ok = file_write_atomic(p, out.data, out.len);
    free(p);
    str_free(&out);
    return ok;
}

static int ext_index(const Ext *arr, int n, const char *name)
{
    for (int i = 0; i < n; i++) if (str_eq(arr[i].name, name)) return i;
    return -1;
}

int ext_count(void)
{
    Ext arr[EXT_MAX];
    return ext_load(arr, EXT_MAX);
}

int ext_get(int i, char *name, size_t nn, char *kind, size_t nk, int *enabled)
{
    Ext arr[EXT_MAX];
    int n = ext_load(arr, EXT_MAX);
    if (i < 0 || i >= n) return 0;
    snprintf(name, nn, "%s", arr[i].name);
    snprintf(kind, nk, "%s", arr[i].kind);
    if (enabled) *enabled = arr[i].enabled;
    return 1;
}

int ext_set_enabled(const char *name, int on)
{
    Ext arr[EXT_MAX];
    int n = ext_load(arr, EXT_MAX);
    int i = ext_index(arr, n, name);
    if (i < 0) return 0;
    arr[i].enabled = on ? 1 : 0;
    return ext_save(arr, n);
}

int ext_remove(const char *name)
{
    Ext arr[EXT_MAX];
    int n = ext_load(arr, EXT_MAX);
    int i = ext_index(arr, n, name);
    if (i < 0) return 0;
    for (int k = i; k < n - 1; k++) arr[k] = arr[k + 1];
    return ext_save(arr, n - 1);
}

int ext_add(const char *kind, const char *name, const char *arg, const char *desc, const char *schema)
{
    Ext arr[EXT_MAX];
    int n = ext_load(arr, EXT_MAX);
    int i = ext_index(arr, n, name);
    if (i < 0) {
        if (n >= EXT_MAX) return 0;
        i = n++;
        memset(&arr[i], 0, sizeof arr[i]);
    }
    snprintf(arr[i].kind, sizeof arr[i].kind, "%s", kind);
    snprintf(arr[i].name, sizeof arr[i].name, "%s", name);
    snprintf(arr[i].arg, sizeof arr[i].arg, "%s", arg ? arg : "");
    snprintf(arr[i].desc, sizeof arr[i].desc, "%s", desc ? desc : "");
    snprintf(arr[i].schema, sizeof arr[i].schema, "%s", schema ? schema : "");
    arr[i].enabled = 1;
    return ext_save(arr, n);
}

/* prompt de um comando registrado (kind=command, habilitado) */
int ext_command_prompt(const char *name, char *out, size_t n)
{
    Ext arr[EXT_MAX];
    int total = ext_load(arr, EXT_MAX);
    for (int i = 0; i < total; i++) {
        if (arr[i].enabled && str_eq(arr[i].kind, "command") && str_eq(arr[i].name, name)) {
            snprintf(out, n, "%s", arr[i].arg);
            return 1;
        }
    }
    return 0;
}

int ext_is_enabled(const char *name)
{
    Ext arr[EXT_MAX];
    int n = ext_load(arr, EXT_MAX);
    int i = ext_index(arr, n, name);
    return i < 0 ? 1 : arr[i].enabled;   /* nao registrada = habilitada */
}

/* serializa um JValue (para passar argumentos ao comando externo) */
static void ext_json_dump(Str *out, JValue *v)
{
    if (!v) { str_adds(out, "null"); return; }
    switch (v->kind) {
        case J_NULL: str_adds(out, "null"); break;
        case J_BOOL: str_adds(out, v->boolean ? "true" : "false"); break;
        case J_NUM:  str_addf(out, "%.17g", v->num); break;
        case J_STR:  json_write_string(out, v->str ? v->str : "", v->str ? strlen(v->str) : 0); break;
        case J_ARR:
            str_addc(out, '[');
            for (size_t i = 0; i < v->count; i++) { if (i) str_addc(out, ','); ext_json_dump(out, v->items[i]); }
            str_addc(out, ']');
            break;
        case J_OBJ:
            str_addc(out, '{');
            for (size_t i = 0; i < v->count; i++) {
                if (i) str_addc(out, ',');
                json_write_string(out, v->pairs[i].key, strlen(v->pairs[i].key));
                str_addc(out, ':');
                ext_json_dump(out, v->pairs[i].val);
            }
            str_addc(out, '}');
            break;
    }
}

/* --- handler das tools dinamicas (kind=tool) --- */
static int ext_tool_run(Tool *self, JValue *args, const char *cwd, ToolResult *res)
{
    Ext arr[EXT_MAX];
    int n = ext_load(arr, EXT_MAX);
    int i = ext_index(arr, n, self->name);
    if (i < 0 || !arr[i].enabled) {
        str_addf(&res->output, "Erro: extensao '%s' nao esta habilitada.", self->name);
        res->is_error = 1;
        return 0;
    }
    Str json; str_init(&json);
    ext_json_dump(&json, args);
    Str cmd; str_init(&cmd);
#ifdef _WIN32
    str_addf(&cmd, "%s \"%s\"", arr[i].arg, json.data ? json.data : "{}");
#else
    str_addf(&cmd, "%s '%s'", arr[i].arg, json.data ? json.data : "{}");
#endif
    int code = 0;
    int ok = tools_exec(cwd, cmd.data, 120, &res->output, &code);
    res->exit_code = code;
    if (!ok) { str_adds(&res->output, "Erro: falha ao executar a extensao."); res->is_error = 1; }
    else if (code != 0) res->is_error = 1;
    str_free(&cmd);
    str_free(&json);
    return ok;
}

/* --- tabela de tools dinamicas --- */
static Tool g_ext_tools[EXT_MAX];
static int  g_ext_n = 0;
static int  g_ext_prontas = 0;

const Tool *ext_tools_all(void)
{
    if (!g_ext_prontas) {
        Ext arr[EXT_MAX];
        int n = ext_load(arr, EXT_MAX);
        g_ext_n = 0;
        for (int i = 0; i < n && g_ext_n < EXT_MAX - 1; i++) {
            if (!arr[i].enabled || !str_eq(arr[i].kind, "tool")) continue;
            Tool *t = &g_ext_tools[g_ext_n++];
            t->name = str_dup(arr[i].name);
            t->description = str_dup(arr[i].desc[0] ? arr[i].desc : "extensao externa");
            t->schema = str_dup(arr[i].schema[0] ? arr[i].schema
                                                 : "{\"type\":\"object\",\"properties\":{}}");
            t->run = ext_tool_run;
        }
        memset(&g_ext_tools[g_ext_n], 0, sizeof g_ext_tools[0]);
        g_ext_prontas = 1;
    }
    return g_ext_tools;
}

/* recarrega a tabela dinamica (apos criar/remover/ligar/desligar) */
void ext_reload(void)
{
    for (int i = 0; i < g_ext_n; i++) {
        free((void *)g_ext_tools[i].name);
        free((void *)g_ext_tools[i].description);
        free((void *)g_ext_tools[i].schema);
    }
    g_ext_n = 0;
    g_ext_prontas = 0;
    /* a tabela unificada de tools.c guarda copias destes ponteiros: se nao
       for descartada, a tool nova fica invisivel e a antiga vira dangling. */
    tools_invalidate();
}

/* --- tool "extensions" --- */
static const char *ext_kind_nome(const char *k)
{
    if (str_eq(k, "tool")) return "tool";
    if (str_eq(k, "skill")) return "skill";
    if (str_eq(k, "command")) return "comando";
    if (str_eq(k, "mcp")) return "mcp";
    return k;
}

static int extensions_op(JValue *a, const char *cwd, ToolResult *res)
{
    const char *op = ext_arg_s(a, "op");
    if (!*op) op = "list";
    Ext arr[EXT_MAX];
    int n = ext_load(arr, EXT_MAX);

    if (str_eq(op, "list")) {
        if (!n) { str_adds(&res->output, "(nenhuma extensao — use op=add)\n"); return 1; }
        for (int i = 0; i < n; i++)
            str_addf(&res->output, "[%s] %-8s %-20s %s\n", arr[i].enabled ? "on " : "off",
                     ext_kind_nome(arr[i].kind), arr[i].name, arr[i].desc);
        return 1;
    }
    if (str_eq(op, "enable") || str_eq(op, "disable")) {
        const char *nome = ext_arg_s(a, "name");
        int i = ext_index(arr, n, nome);
        if (i < 0) { ext_err(res, "Erro: extensao '%s' nao existe.", nome); return 0; }
        arr[i].enabled = str_eq(op, "enable");
        if (!ext_save(arr, n)) { ext_err(res, "Erro: nao consegui gravar."); return 0; }
        ext_reload();
        str_addf(&res->output, "%s: %s\n", arr[i].enabled ? "ligada" : "desligada", nome);
        return 1;
    }
    if (str_eq(op, "add")) {
        const char *kind = ext_arg_s(a, "kind");
        const char *nome = ext_arg_s(a, "name");
        const char *arg = ext_arg_s(a, "arg");
        const char *desc = ext_arg_s(a, "desc");
        if (!*kind || !*nome) { ext_err(res, "Erro: 'kind' e 'name' sao obrigatorios."); return 0; }
        if (str_eq(kind, "skill")) {
            /* skill: grava skills/<nome>/SKILL.md */
            char *dir = path_join(cwd, "skills");
            dir_make(dir);
            char *sd = path_join(dir, nome);
            dir_make(sd);
            char *md = path_join(sd, "SKILL.md");
            Str out; str_init(&out);
            str_addf(&out, "---\nname: %s\ndescription: %s\n---\n\n%s", nome, *desc ? desc : nome, arg);
            file_write(md, out.data, out.len);
            str_free(&out);
            free(md); free(sd); free(dir);
        }
        if (!ext_add(kind, nome, arg, desc, ext_arg_s(a, "schema"))) { ext_err(res, "Erro: nao consegui gravar."); return 0; }
        ext_reload();
        str_addf(&res->output, "Extensao criada: %s (%s)\n", nome, kind);
        return 1;
    }
    if (str_eq(op, "remove")) {
        const char *nome = ext_arg_s(a, "name");
        if (!ext_remove(nome)) { ext_err(res, "Erro: extensao '%s' nao existe.", nome); return 0; }
        ext_reload();
        str_addf(&res->output, "Extensao removida: %s\n", nome);
        return 1;
    }
    if (str_eq(op, "run")) {
        /* roda uma extensao kind=tool pelo nome (atalho) */
        const char *nome = ext_arg_s(a, "name");
        Tool *t = (Tool *)tool_find(nome);
        if (!t) { ext_err(res, "Erro: tool '%s' nao encontrada.", nome); return 0; }
        JValue *args = json_get(a, "args");
        JValue *vazio = NULL;
        if (!args || args->kind != J_OBJ) { vazio = json_parse_cstr("{}"); args = vazio; }
        int ok = t->run(t, args, cwd, res);
        if (vazio) json_free(vazio);
        return ok;
    }
    ext_err(res, "Erro: op desconhecida de extensions: %s", op);
    return 0;
}

static int tool_extensions(Tool *self, JValue *a, const char *cwd, ToolResult *res)
{
    (void)self;
    return extensions_op(a, cwd, res);
}

static const char EXTENSIONS_SCHEMA[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"op\":{\"type\":\"string\",\"enum\":[\"list\",\"enable\",\"disable\",\"add\",\"remove\",\"run\"]},"
    "\"kind\":{\"type\":\"string\",\"enum\":[\"tool\",\"skill\",\"command\",\"mcp\"]},"
    "\"name\":{\"type\":\"string\"},"
    "\"arg\":{\"type\":\"string\",\"description\":\"comando (tool), markdown (skill), prompt (command) ou URL (mcp)\"},"
    "\"desc\":{\"type\":\"string\"},"
    "\"schema\":{\"type\":\"string\",\"description\":\"JSON Schema da tool criada\"},"
    "\"args\":{\"type\":\"object\",\"description\":\"argumentos para op=run\"}},"
    "\"required\":[\"op\"]}";

static const Tool EXT_TABLE[] = {
    { "extensions", "Extensions: list enable disable add remove run (tool/skill/command/mcp)", EXTENSIONS_SCHEMA, tool_extensions },
    { NULL, NULL, NULL, NULL }
};

const Tool *extensions_all(void) { return EXT_TABLE; }
