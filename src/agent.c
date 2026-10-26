/* agent.c - config, sessao, prompt de sistema e loop do agente */
#include "agent.h"
#include "tools.h"
#include "json.h"
#include "codegraph.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

/* ================================================================== */
/* Config                                                              */
/* ================================================================== */
static char *config_path(void)
{
    char *dir = delta_home();
    dir_make(dir);
    char *p = path_join(dir, "config.json");
    free(dir);
    return p;
}

static const char *default_base(const char *provider)
{
    return str_istarts(provider, "anthropic") ? "https://api.anthropic.com" : "https://api.openai.com/v1";
}

static const char *default_model(const char *provider)
{
    return str_istarts(provider, "anthropic") ? "claude-sonnet-4-5" : "gpt-4o";
}

int config_load(Config *c, char *note, size_t note_len)
{
    memset(c, 0, sizeof(*c));
    c->max_tokens = 8192;
    c->temperature = 0;
    c->thinking_level = str_dup("off");

    char *path = config_path();
    size_t len = 0;
    char *text = file_read(path, &len);
    int chave_do_arquivo = 0;
    if (text) {
        JValue *v = json_parse(text, len);
        if (v) {
            const char *s;
            if ((s = json_str(json_get(v, "provider")))[0]) c->provider = str_dup(s);
            if ((s = json_str(json_get(v, "model")))[0]) c->model = str_dup(s);
            if ((s = json_str(json_get(v, "apiKey")))[0]) { c->api_key = str_dup(s); chave_do_arquivo = 1; }
            if ((s = json_str(json_get(v, "baseUrl")))[0]) c->base_url = str_dup(s);
            if ((s = json_str(json_get(v, "systemPrompt")))[0]) c->system_extra = str_dup(s);
            if ((s = json_str(json_get(v, "language")))[0]) c->language = str_dup(s);
            if ((s = json_str(json_get(v, "projectDir")))[0]) c->project_dir = str_dup(s);
            if ((s = json_str(json_get(v, "thinkingLevel")))[0]) { free(c->thinking_level); c->thinking_level = str_dup(s); }
            JValue *hv = json_get(v, "headers");
            if (hv && hv->kind == J_OBJ && hv->count > 0) {
                Str h; str_init(&h);
                str_addc(&h, '{');
                for (size_t i = 0; i < hv->count; i++) {
                    JValue *val = hv->pairs[i].val;
                    if (i) str_addc(&h, ',');
                    json_write_string(&h, hv->pairs[i].key, strlen(hv->pairs[i].key));
                    str_addc(&h, ':');
                    if (val->kind == J_STR) json_write_string(&h, val->str ? val->str : "",
                                                              val->str ? strlen(val->str) : 0);
                    else if (val->kind == J_NUM) str_addf(&h, "%g", val->num);
                    else str_adds(&h, "null");
                }
                str_addc(&h, '}');
                c->headers = str_steal(&h);
            }
            double mt = json_num(json_get(v, "maxTokens"), 0);
            if (mt > 0) c->max_tokens = (int)mt;
            json_free(v);
        }
        free(text);
    }
    free(path);

    /* ambiente tem prioridade */
    const char *env_src = NULL;
    if (!c->api_key) {
        char *k = env_get("ANTHROPIC_API_KEY");
        if (k && *k) { c->api_key = k; c->provider = c->provider ? c->provider : str_dup("anthropic"); env_src = "ANTHROPIC_API_KEY"; }
        else free(k);
    }
    if (!c->api_key) {
        char *k = env_get("OPENAI_API_KEY");
        if (k && *k) { c->api_key = k; c->provider = c->provider ? c->provider : str_dup("openai"); env_src = "OPENAI_API_KEY"; }
        else free(k);
    }
    if (!c->provider) {
        char *p = env_get("DELTA_PROVIDER");
        c->provider = (p && *p) ? p : str_dup("anthropic");
        free(p && *p ? NULL : p);
    }
    if (!c->model) {
        char *m = env_get("DELTA_MODEL");
        c->model = (m && *m) ? m : str_dup(default_model(c->provider));
        if (m && *m) {} else free(m);
    }
    if (!c->base_url) c->base_url = str_dup(default_base(c->provider));

    /* migra a chave antiga do config.json (nunca a do ambiente) para o auth.json */
    if (chave_do_arquivo && c->api_key && c->provider && !auth_has_key(c->provider))
        auth_set_key(c->provider, c->api_key);

    if (note && note_len) {
        snprintf(note, note_len, "%s/%s%s%s", c->provider, c->model,
                 c->api_key ? "" : " (sem API key)",
                 env_src ? " [via env]" : "");
    }
    return c->api_key != NULL;
}

int config_save(const Config *c)
{
    Str o; str_init(&o);
    str_adds(&o, "{\n  \"provider\": ");
    json_write_string(&o, c->provider ? c->provider : "", strlen(c->provider ? c->provider : ""));
    str_adds(&o, ",\n  \"model\": ");
    json_write_string(&o, c->model ? c->model : "", strlen(c->model ? c->model : ""));
    str_adds(&o, ",\n  \"baseUrl\": ");
    json_write_string(&o, c->base_url ? c->base_url : "", strlen(c->base_url ? c->base_url : ""));
    str_adds(&o, ",\n  \"language\": ");
    json_write_string(&o, c->language ? c->language : "pt-BR", strlen(c->language ? c->language : "pt-BR"));
    str_adds(&o, ",\n");
    str_adds(&o, "  \"projectDir\": ");
    json_write_string(&o, c->project_dir ? c->project_dir : "",
                      strlen(c->project_dir ? c->project_dir : ""));
    str_adds(&o, ",\n  \"thinkingLevel\": ");
    json_write_string(&o, c->thinking_level ? c->thinking_level : "off",
                      strlen(c->thinking_level ? c->thinking_level : "off"));
    str_adds(&o, ",\n  \"maxTokens\": ");
    str_addf(&o, "%d", c->max_tokens);
    if (c->headers && *c->headers) {
        str_adds(&o, ",\n  \"headers\": ");
        str_adds(&o, c->headers);
    }
    str_adds(&o, "\n}\n");
    char *p = config_path();
    int ok = file_write(p, o.data, o.len);
    free(p);
    str_free(&o);
    return ok;
}

void config_free(Config *c)
{
    free(c->provider);
    free(c->model);
    free(c->api_key);
    free(c->base_url);
    free(c->system_extra);
    free(c->language);
    free(c->thinking_level);
    free(c->headers);
    free(c->project_dir);
    memset(c, 0, sizeof(*c));
}

/* ================================================================== */
/* Credenciais por provedor (auth.json)                                */
/* ================================================================== */
char *auth_path(void)
{
    char *dir = delta_home();
    dir_make(dir);
    char *p = path_join(dir, "auth.json");
    free(dir);
    return p;
}

/* auth.json fica em cache: auth_has_key() era chamada por provedor em cada
   frame (232 leituras + parse de arquivo por render = 140 ms de lag no
   /models). Quem escreve invalida o cache. */
static JValue *g_auth = NULL;
static int     g_auth_loaded = 0;

static JValue *auth_parse_file(void)
{
    char *path = auth_path();
    size_t len = 0;
    char *text = file_read(path, &len);
    free(path);
    if (!text) return NULL;
    JValue *v = json_parse(text, len);
    free(text);
    if (!v || v->kind != J_OBJ) { json_free(v); return NULL; }
    return v;
}

static JValue *auth_load(void)
{
    if (g_auth_loaded) return g_auth;
    g_auth_loaded = 1;
    g_auth = auth_parse_file();
    return g_auth;
}

/* Copia nova do disco, para quem vai modificar (nao mexe no cache). */
static JValue *auth_load_fresh(void) { return auth_parse_file(); }

static void auth_forget(void)
{
    if (g_auth) json_free(g_auth);
    g_auth = NULL;
    g_auth_loaded = 0;
}

char *auth_get_key(const char *provider)
{
    if (!provider || !*provider) return NULL;
    JValue *v = auth_load();
    if (!v) return NULL;
    const char *k = json_str(json_get(v, provider));
    return *k ? str_dup(k) : NULL;          /* cache: nao liberar */
}

int auth_has_key(const char *provider)
{
    char *k = auth_get_key(provider);
    int has = k != NULL;
    free(k);
    return has;
}

static int auth_write(JValue *v)
{
    Str o; str_init(&o);
    str_adds(&o, "{\n");
    for (size_t i = 0; i < v->count; i++) {
        const char *key = v->pairs[i].key;
        const char *val = json_str(v->pairs[i].val);
        if (i) str_adds(&o, ",\n");
        str_adds(&o, "  ");
        json_write_string(&o, key, strlen(key));
        str_adds(&o, ": ");
        json_write_string(&o, val, strlen(val));
    }
    str_adds(&o, "\n}\n");
    char *path = auth_path();
    int ok = file_write(path, o.data, o.len);
    free(path);
    str_free(&o);
    return ok;
}

int auth_set_key(const char *provider, const char *key)
{
    if (!provider || !*provider) return 0;
    JValue *v = auth_load_fresh();
    if (!v) v = json_parse_cstr("{}");
    /* atualiza ou acrescenta */
    int found = 0;
    for (size_t i = 0; i < v->count; i++) {
        if (str_eq(v->pairs[i].key, provider)) {
            json_free(v->pairs[i].val);
            JValue *nv = (JValue *)calloc(1, sizeof(JValue));
            nv->kind = J_STR;
            nv->str = str_dup(key ? key : "");
            v->pairs[i].val = nv;
            found = 1;
            break;
        }
    }
    if (!found) {
        v->pairs = (JPair *)realloc(v->pairs, (v->count + 1) * sizeof(JPair));
        v->pairs[v->count].key = str_dup(provider);
        JValue *nv = (JValue *)calloc(1, sizeof(JValue));
        nv->kind = J_STR;
        nv->str = str_dup(key ? key : "");
        v->pairs[v->count].val = nv;
        v->count++;
    }
    int ok = auth_write(v);
    json_free(v);
    auth_forget();
    return ok;
}

int auth_remove_key(const char *provider)
{
    if (!provider || !*provider) return 0;
    JValue *v = auth_load_fresh();
    if (!v) return 0;
    size_t j = 0;
    for (size_t i = 0; i < v->count; i++) {
        if (str_eq(v->pairs[i].key, provider)) {
            free(v->pairs[i].key);
            json_free(v->pairs[i].val);
            continue;
        }
        v->pairs[j++] = v->pairs[i];
    }
    v->count = j;
    int ok = auth_write(v);
    json_free(v);
    auth_forget();
    return ok;
}

/* ================================================================== */
/* Sessao                                                              */
/* ================================================================== */
static char *g_session_id = NULL;
static char *g_session_dir = NULL;
static char *g_session_file = NULL;

static char *sessions_root(void)
{
    char *home = home_get();
    char *root = path_join(delta_home(), "sessions");
    free(home);
    return root;
}

/* --<caminho>-- com / \ : trocados por -, como no Delta */
/* --<caminho>-- com / \ : trocados por '-' (um diretorio por pasta). */
static char *cwd_slug(const char *cwd)
{
    Str s; str_init(&s);
    for (const char *p = cwd; *p; p++) {
        char c = *p;
        if (c == '/' || c == '\\' || c == ':') c = '-';
        str_addc(&s, c);
    }
    if (s.len == 0) str_adds(&s, "root");
    return str_steal(&s);
}

/* Versoes anteriores guardavam tudo em sessions/--cwd--. Copia para a pasta
   certa as sessoes cujo cabecalho declara este mesmo cwd. */
static void migrate_legacy_sessions(const char *root, const char *slug_dir,
                                    const char *cwd)
{
    char *old = path_join(root, "--cwd--");
    if (!path_is_dir(old) || str_eq(old, slug_dir)) { free(old); return; }
    dir_make(slug_dir);
    size_t n = 0;
    char **files = dir_list(old, &n);
    int copiou = 0;
    for (size_t i = 0; files && i < n; i++) {
        if (!strstr(files[i], ".jsonl")) continue;
        char *src = path_join(old, files[i]);
        size_t len = 0;
        char *txt = file_read(src, &len);
        if (txt) {
            char *eol = strchr(txt, '\n');
            if (eol) *eol = 0;
            JValue *v = json_parse_cstr(txt);
            const char *c = v ? json_str(json_get(v, "cwd")) : "";
            if (*c && str_eq(c, cwd)) {
                char *dst = path_join(slug_dir, files[i]);
                if (!file_exists(dst)) {
                    file_write(dst, txt, strlen(txt));      /* cabecalho */
                    if (eol) file_append(dst, eol + 1, len - (size_t)(eol - txt) - 1);
                    copiou++;
                }
                free(dst);
            }
            if (v) json_free(v);
            free(txt);
        }
        free(src);
    }
    (void)copiou;
    if (files) dir_list_free(files, n);
    free(old);
}

void session_start(const char *cwd)
{
    char id[37];
    uuid_v4(id);
    free(g_session_id);
    g_session_id = str_dup(id);

    char *root = sessions_root();
    char *slug = cwd_slug(cwd);
    Str dir; str_init(&dir);
    str_adds(&dir, "--");
    str_adds(&dir, slug);
    str_adds(&dir, "--");
    free(slug);
    char *full = path_join(root, dir.data);
    migrate_legacy_sessions(root, full, cwd);
    dir_make(full);
    free(g_session_dir);
    g_session_dir = full;
    str_free(&dir);
    free(root);

    /* cabecalho */
    Str line; str_init(&line);
    str_adds(&line, "{\"type\":\"session\",\"version\":3,\"id\":");
    json_write_string(&line, g_session_id, strlen(g_session_id));
    time_t t = time(NULL);
    str_addf(&line, ",\"timestamp\":%lld,\"cwd\":", (long long)t);
    json_write_string(&line, cwd, strlen(cwd));
    str_adds(&line, "}\n");

    /* nome do arquivo: timestamp seguro para o Windows (sem ':') */
    char iso[32];
    iso_now(iso);
    for (char *p = iso; *p; p++) if (*p == ':') *p = '-';

    free(g_session_file);
    {
        char name[128];
        snprintf(name, sizeof name, "%s_%s.jsonl", iso, g_session_id);
        g_session_file = path_join(g_session_dir, name);
    }
    file_write(g_session_file, line.data, line.len);
    str_free(&line);
}

const char *session_id(void) { return g_session_id ? g_session_id : ""; }

void session_set_id(const char *id)
{
    free(g_session_id);
    g_session_id = str_dup(id);
}

void session_append(const Agent *a, const char *type, const Msg *m)
{
    (void)a;
    if (!g_session_file) return;
    const char *path = g_session_file;

    Str o; str_init(&o);
    str_adds(&o, "{\"type\":\"");
    str_adds(&o, type);
    str_adds(&o, "\",\"timestamp\":");
    {
        char iso[32];
        iso_now(iso);
        json_write_string(&o, iso, strlen(iso));
    }
    if (m) {
        str_adds(&o, ",\"message\":{\"role\":\"");
        switch (m->role) {
            case ROLE_USER:      str_adds(&o, "user"); break;
            case ROLE_ASSISTANT: str_adds(&o, "assistant"); break;
            case ROLE_TOOL:      str_adds(&o, "toolResult"); break;
            default:             str_adds(&o, "system"); break;
        }
        str_adds(&o, "\",\"content\":");
        json_write_string(&o, m->text.data ? m->text.data : "", m->text.len);
        if (m->role == ROLE_TOOL) {
            str_adds(&o, ",\"toolCallId\":");
            json_write_string(&o, m->tool_call_id ? m->tool_call_id : "", strlen(m->tool_call_id ? m->tool_call_id : ""));
            str_adds(&o, ",\"toolName\":");
            json_write_string(&o, m->tool_name ? m->tool_name : "", strlen(m->tool_name ? m->tool_name : ""));
            str_adds(&o, ",\"isError\":");
            str_adds(&o, m->is_error ? "true" : "false");
        }
        if (m->n_imgs > 0) {
            str_adds(&o, ",\"images\":[");
            for (int i = 0; i < m->n_imgs; i++) {
                if (i) str_addc(&o, ',');
                json_write_string(&o, m->imgs[i], strlen(m->imgs[i]));
            }
            str_addc(&o, ']');
        }
        if (m->n_calls) {
            str_adds(&o, ",\"toolCalls\":[");
            for (size_t i = 0; i < m->n_calls; i++) {
                if (i) str_addc(&o, ',');
                str_adds(&o, "{\"id\":");
                json_write_string(&o, m->calls[i].id, strlen(m->calls[i].id));
                str_adds(&o, ",\"name\":");
                json_write_string(&o, m->calls[i].name, strlen(m->calls[i].name));
                str_adds(&o, ",\"arguments\":");
                json_write_string(&o, m->calls[i].args, strlen(m->calls[i].args));
                str_addc(&o, '}');
            }
            str_addc(&o, ']');
        }
        str_addc(&o, '}');
    }
    str_adds(&o, "}\n");

    FILE *f = fopen(path, "ab");
    if (f) { fwrite(o.data, 1, o.len, f); fclose(f); }
    str_free(&o);
}

/* ================================================================== */
/* Retomar a ultima sessao da pasta                                    */
/* ================================================================== */
/* Carrega o .jsonl mais recente de `cwd` em `msgs` e passa a gravar nele.
   Devolve o numero de mensagens carregadas (0 = nao havia sessao). */
int session_resume_last(const char *cwd, MsgList *msgs)
{
    char *root = sessions_root();
    char *slug = cwd_slug(cwd);
    Str dirname; str_init(&dirname);
    str_adds(&dirname, "--");
    str_adds(&dirname, slug);
    str_adds(&dirname, "--");
    free(slug);
    char *dir = path_join(root, dirname.data);
    str_free(&dirname);
    migrate_legacy_sessions(root, dir, cwd);
    free(root);
    if (!path_is_dir(dir)) { free(dir); return 0; }

    size_t cnt = 0;
    char **files = dir_list(dir, &cnt);
    char *best = NULL;
    for (size_t i = 0; files && i < cnt; i++) {
        const char *nm = files[i];
        size_t nl = strlen(nm);
        if (nl < 6 || !str_ieq(nm + nl - 6, ".jsonl")) continue;
        if (!best || strcmp(nm, best) > 0) {          /* nome comeca com a data */
            free(best);
            best = str_dup(nm);
        }
    }
    if (files) dir_list_free(files, cnt);

    int n = 0;
    if (best) {
        char *file = path_join(dir, best);
        size_t len = 0;
        char *txt = file_read(file, &len);
        if (txt) {
            size_t pos = 0;
            while (pos < len) {
                size_t eol = pos;
                while (eol < len && txt[eol] != '\n') eol++;
                size_t ll = eol - pos;
                if (ll > 2) {
                    char save = txt[pos + ll];
                    txt[pos + ll] = 0;
                    JValue *v = json_parse_cstr(txt + pos);
                    txt[pos + ll] = save;
                    JValue *m = v ? json_get(v, "message") : NULL;
                    if (m && m->kind == J_OBJ) {
                        const char *role = json_str(json_get(m, "role"));
                        Msg *msg = NULL;
                        if (str_eq(role, "user")) msg = msglist_add(msgs, ROLE_USER);
                        else if (str_eq(role, "assistant")) msg = msglist_add(msgs, ROLE_ASSISTANT);
                        else if (str_eq(role, "toolResult")) msg = msglist_add(msgs, ROLE_TOOL);
                        if (msg) {
                            const char *content = json_str(json_get(m, "content"));
                            str_adds(&msg->text, content);
                            const char *th = json_str(json_get(m, "thinking"));
                            if (*th) str_adds(&msg->thinking, th);
                            if (msg->role == ROLE_TOOL) {
                                const char *id = json_str(json_get(m, "toolCallId"));
                                const char *tn = json_str(json_get(m, "toolName"));
                                msg->tool_call_id = str_dup(id);
                                msg->tool_name = str_dup(tn);
                                JValue *ie = json_get(m, "isError");
                                msg->is_error = ie ? json_bool(ie, 0) : 0;
                            }
                            JValue *calls = json_get(m, "toolCalls");
                            if (calls && calls->kind == J_ARR) {
                                for (size_t k = 0; k < calls->count; k++) {
                                    JValue *c = calls->items[k];
                                    msg_add_call(msg, json_str(json_get(c, "id")),
                                                 json_str(json_get(c, "name")),
                                                 json_str(json_get(c, "arguments")));
                                }
                            }
                            n++;
                        }
                    }
                    if (v) json_free(v);
                }
                pos = eol + 1;
            }
            free(txt);
        }
        /* continua gravando no mesmo arquivo */
        free(g_session_file);
        g_session_file = file;
        free(g_session_dir);
        g_session_dir = str_dup(dir);
        const char *base = path_basename(file);
        if (base) {
            /* id = trecho depois do '_' (nome: iso_id.jsonl) */
            const char *us = strrchr(base, '_');
            if (us && strlen(us + 1) > 5) {
                char idbuf[64];
                snprintf(idbuf, sizeof idbuf, "%s", us + 1);
                char *dot = strstr(idbuf, ".jsonl");
                if (dot) *dot = 0;
                free(g_session_id);
                g_session_id = str_dup(idbuf);
            }
        }
        free(best);
    } else {
        free(best);
    }
    free(dir);
    return n;
}

/* ================================================================== */
/* Prompt de sistema                                                   */
/* ================================================================== */
static void build_system_prompt(Agent *a)
{
    Str s; str_init(&s);
    str_adds(&s,
        "You are Delta Agent, a coding agent running in the user's terminal. "
        "You help with software engineering tasks: reading and writing code, running commands, "
        "and explaining what you find.\n\n");

    str_adds(&s,
        "Response style (strict, always):\n"
        "- Serious, professional tone by default: no jokes, no small talk, no exclamation marks, "
        "no emojis or emoticons.\n"
        "- Address the user as \"chefe\" at least once per reply, naturally inside a sentence.\n"
        "- Be concise: reply only in paragraphs of 2 to 3 lines. Do not use bullet lists, "
        "numbered lists, headings or tables; fenced code blocks are the only exception.\n"
        "- Use **bold** for the essential term and *italic* for the nuance or caveat.\n"
        "- Put commands, paths, diffs and code inside fenced blocks with a language tag "
        "(```c, ```bash) or inline `code`.\n"
        "- The terminal renders markdown: write the formatting directly, never escape or explain it.\n\n");

    str_adds(&s, "Available tools:\n");
    for (const Tool *t = tools_all(); t->name; t++)
        str_addf(&s, "- %s: %s\n", t->name, t->description);
    str_adds(&s,
        "\nGuidelines:\n"
        "- Use read to examine files instead of cat or sed.\n"
        "- Use edit for precise changes (edits[].oldText must match exactly).\n"
        "- When changing multiple separate locations in one file, use one edit call with multiple entries in edits[].\n"
        "- Keep edits[].oldText as small as possible while still being unique in the file.\n"
        "- Use write only for new files or complete rewrites.\n"
        "- Show file paths clearly when working with files.\n"
        "- The unified families cover the rest: fs (files), shell (commands and background jobs), "
        "git, web, skills, mcp, memory, code, assert, agent and extend — each takes an \"op\" field.\n"
        "- Durable facts from the user are saved automatically in memory (search it before asking again). "
        "When a workflow repeats, save it as a skill with the skills tool.\n");

    {
        char *idx = tools_skills_index(a->cwd, 2400);
        if (idx && *idx) {
            str_adds(&s, "\nSkills available (read one with the skills tool, op=read):\n");
            str_adds(&s, idx);
        }
        free(idx);
    }

    str_adds(&s, "\nEnvironment:\n");
    str_addf(&s, "- Working directory: %s\n", a->cwd);
#ifdef _WIN32
    str_adds(&s, "- Platform: Windows (shell: cmd.exe)\n");
#else
    str_adds(&s, "- Platform: POSIX (shell: /bin/sh)\n");
#endif
    {
        char iso[32];
        iso_now(iso);
        str_addf(&s, "- Current date: %s\n", iso);
    }

    if (a->cfg->system_extra && *a->cfg->system_extra) {
        str_adds(&s, "\n");
        str_adds(&s, a->cfg->system_extra);
        str_addc(&s, '\n');
    }

    a->system_prompt = str_steal(&s);
}

/* ================================================================== */
/* Agente                                                              */
/* ================================================================== */
void agent_init(Agent *a, Config *cfg, const char *cwd)
{
    memset(a, 0, sizeof(*a));
    msglist_init(&a->msgs);
    a->cfg = cfg;
    a->cwd = str_dup(cwd);
    build_system_prompt(a);
}

void agent_set_cwd(Agent *a, const char *cwd)
{
    if (!a || !cwd || !*cwd) return;
    free(a->cwd);
    a->cwd = str_dup(cwd);
    free(a->system_prompt);
    build_system_prompt(a);
}

void agent_free(Agent *a)
{
    msglist_free(&a->msgs);
    free(a->cwd);
    free(a->system_prompt);
}

void agent_add_user(Agent *a, const char *text)
{
    Msg *m = msglist_add(&a->msgs, ROLE_USER);
    str_adds(&m->text, text);
}

void agent_push_user(Agent *a, const char *text)
{
    agent_add_user(a, text);
    session_append(a, "message", &a->msgs.items[a->msgs.n - 1]);
}

void agent_push_user_imgs(Agent *a, const char *text, char **imgs, int n_imgs)
{
    agent_add_user(a, text);
    Msg *m = &a->msgs.items[a->msgs.n - 1];
    for (int i = 0; i < n_imgs; i++) {
        m->imgs = (char **)realloc(m->imgs, (size_t)(m->n_imgs + 1) * sizeof(char *));
        m->imgs[m->n_imgs++] = str_dup(imgs[i]);
    }
    session_append(a, "message", m);
}

const char *agent_context_usage(const Agent *a, long *used, long *window)
{
    (void)a;
    if (used) *used = a->total_in;
    if (window) *window = 200000;
    return "";
}

/* limite de ferramentas por turno (0 = sem limite) */
static int g_tool_limit = 0;
void agent_set_tool_limit(int n) { g_tool_limit = n < 0 ? 0 : n; }
int  agent_tool_limit(void) { return g_tool_limit; }

static const AgentCB *g_bridge_cb = NULL;
static void *g_bridge_ud = NULL;

static void steer_dbg(const char *fmt, ...)
{
    const char *p = getenv("DELTA_STEER_DEBUG");
    if (!p || !*p) return;
    FILE *f = fopen(p, "ab");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static void agent_stream_trampoline(void *ud, int kind, const char *s, size_t n)
{
    (void)ud;
    if (!g_bridge_cb) return;
    if (kind == 0 && g_bridge_cb->on_text) g_bridge_cb->on_text(g_bridge_ud, s, n);
    else if (kind == 1 && g_bridge_cb->on_thinking) g_bridge_cb->on_thinking(g_bridge_ud, s, n);

    /* A cada pedaco do stream: parar se o usuario mandou parar (Ctrl+C, /stop)
       ou se ele digitou uma mensagem nova enquanto o modelo escrevia. Corte
       aqui significa: antes da mensagem final do agente. */
    int ab = (g_bridge_cb->should_abort && g_bridge_cb->should_abort(g_bridge_ud));
    int st = (g_bridge_cb->has_steer && g_bridge_cb->has_steer(g_bridge_ud));
    steer_dbg("chunk kind=%d ab=%d steer=%d n=%zu", kind, ab, st, n);
    if (ab || st) provider_stream_abort = 1;
}

int agent_run(Agent *a, const char *user_text, const AgentCB *cb, Str *err)
{
    if (user_text && *user_text) agent_push_user(a, user_text);

    const int MAX_STEPS = 60;
    int usadas = 0;                    /* ferramentas executadas neste turno */
    for (int step = 0; step < MAX_STEPS; step++) {
        /* intervalo entre thinking/tools: injeta mensagens enfileiradas */
        if (cb && cb->on_interval) cb->on_interval(cb->ud);
        if (cb && cb->should_abort && cb->should_abort(cb->ud)) {
            if (err) str_adds(err, "interrompido");
            return 0;
        }
        if (cb && cb->on_turn_start) cb->on_turn_start(cb->ud);

        Provider p;
        memset(&p, 0, sizeof p);
        p.provider = a->cfg->provider;
        p.model = a->cfg->model;
        p.api_key = a->cfg->api_key;
        p.base_url = a->cfg->base_url;
        p.max_tokens = a->cfg->max_tokens;
        p.temperature = a->cfg->temperature;
        p.extra_headers_json = a->cfg->headers;
        p.session_id = session_id();
        p.support_image = a->cfg->supports_image;
        p.on_retry = cb ? cb->on_retry : NULL;
        p.retry_ud = cb ? cb->ud : NULL;

        Turn turn;
        turn_init(&turn);

        g_bridge_cb = cb;
        g_bridge_ud = cb ? cb->ud : NULL;
        int ok = provider_run(&p, &a->msgs, a->system_prompt, &turn,
                              agent_stream_trampoline, NULL, err);
        g_bridge_cb = NULL;
        g_bridge_ud = NULL;
        if (!ok) {
            /* O corte pode ter vindo de uma mensagem nova do usuario: nesse
               caso NAO termina o turno — guarda o texto parcial e roda de
               novo com a mensagem, ainda no meio da task. */
            int steer = cb && cb->has_steer && cb->has_steer(cb->ud);
            if (steer) {
                if (turn.text.len || turn.thinking.len) {
                    Msg *am2 = msglist_add(&a->msgs, ROLE_ASSISTANT);
                    str_add(&am2->text, turn.text.data ? turn.text.data : "", turn.text.len);
                    str_add(&am2->thinking, turn.thinking.data ? turn.thinking.data : "",
                            turn.thinking.len);
                    session_append(a, "message", am2);
                }
                turn_free(&turn);
                if (cb->on_interval) cb->on_interval(cb->ud);
                str_clear(err);
                continue;
            }
            turn_free(&turn);
            return 0;
        }

        a->total_in += turn.usage_in;
        a->total_out += turn.usage_out;

        /* registra a mensagem do assistente */
        Msg *am = msglist_add(&a->msgs, ROLE_ASSISTANT);
        str_add(&am->text, turn.text.data ? turn.text.data : "", turn.text.len);
        str_add(&am->thinking, turn.thinking.data ? turn.thinking.data : "", turn.thinking.len);
        for (size_t i = 0; i < turn.calls.n_calls; i++)
            msg_add_call(am, turn.calls.calls[i].id, turn.calls.calls[i].name, turn.calls.calls[i].args);
        session_append(a, "message", am);

        if (turn.calls.n_calls == 0) {
            turn_free(&turn);
            return 1;
        }

        /* executa as ferramentas */
        for (size_t i = 0; i < turn.calls.n_calls; i++) {
            ToolCall *tc = &turn.calls.calls[i];
            if (cb && cb->should_abort && cb->should_abort(cb->ud)) {
                turn_free(&turn);
                if (err) str_adds(err, "interrompido");
                return 0;
            }
            /* /forlimit: acima do teto, a ferramenta não roda — o modelo é
               avisado e deve fechar o turno com o que já tem */
            if (g_tool_limit > 0 && usadas >= g_tool_limit) {
                Msg *tm = msglist_add(&a->msgs, ROLE_TOOL);
                str_addf(&tm->text,
                         "Limite de ferramentas por turno atingido (%d). "
                         "Não chame mais ferramentas: responda com o que você já sabe "
                         "ou peça ao usuário para usar /forlimit para desligar o limite.",
                         g_tool_limit);
                tm->tool_call_id = str_dup(tc->id);
                tm->tool_name = str_dup(tc->name);
                tm->is_error = 1;
                session_append(a, "message", tm);
                if (cb && cb->on_tool_end)
                    cb->on_tool_end(cb->ud, tc->name, tm->text.data ? tm->text.data : "",
                                    tm->text.len, 1, 0.0);
                continue;
            }
            usadas++;
            if (cb && cb->on_tool_start) cb->on_tool_start(cb->ud, tc->name, tc->args);

            const Tool *tool = tool_find(tc->name);
            ToolResult tr;
            tool_result_init(&tr);
            uint64_t t0 = now_ms();
            if (!tool) {
                str_addf(&tr.output, "Erro: ferramenta desconhecida '%s'", tc->name);
                tr.is_error = 1;
            } else {
                JValue *args = json_parse_cstr(tc->args);
                if (!args) {
                    str_adds(&tr.output, "Erro: argumentos em JSON invalido");
                    tr.is_error = 1;
                } else {
                    tool->run((Tool *)tool, args, a->cwd, &tr);
                    json_free(args);
                }
            }
            double secs = (double)(now_ms() - t0) / 1000.0;

            Msg *tm = msglist_add(&a->msgs, ROLE_TOOL);
            str_add(&tm->text, tr.output.data ? tr.output.data : "", tr.output.len);
            tm->tool_call_id = str_dup(tc->id);
            tm->tool_name = str_dup(tc->name);
            tm->is_error = tr.is_error;
            session_append(a, "message", tm);

            if (cb && cb->on_tool_end)
                cb->on_tool_end(cb->ud, tc->name, tr.output.data ? tr.output.data : "", tr.output.len,
                                tr.is_error, secs);
            tool_result_free(&tr);
        }
        turn_free(&turn);
    }

    if (err) str_adds(err, "limite de passos atingido");
    return 1;
}
