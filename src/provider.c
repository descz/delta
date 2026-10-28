/* provider.c - streaming SSE para OpenAI-compativel e Anthropic.
 * Rede via WinHTTP (DLL do Windows) / POSIX: nao ha dependencia externa. */
#include "provider.h"
#include "tools.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#  include <winhttp.h>
#else
#  include <unistd.h>
#  include <string.h>
#  include <fcntl.h>
#  include <errno.h>
#endif

/* ------------------------------------------------------------------ */
/* Mensagens                                                           */
/* ------------------------------------------------------------------ */
void msglist_init(MsgList *l) { memset(l, 0, sizeof(*l)); }

void msg_free(Msg *m)
{
    for (int i = 0; i < m->n_imgs; i++) free(m->imgs[i]);
    free(m->imgs);
    str_free(&m->text);
    str_free(&m->thinking);
    for (size_t i = 0; i < m->n_calls; i++) {
        free(m->calls[i].id);
        free(m->calls[i].name);
        free(m->calls[i].args);
    }
    free(m->calls);
    free(m->tool_call_id);
    free(m->tool_name);
    memset(m, 0, sizeof(*m));
}

void msglist_free(MsgList *l)
{
    for (size_t i = 0; i < l->n; i++) msg_free(&l->items[i]);
    free(l->items);
    memset(l, 0, sizeof(*l));
}

Msg *msglist_add(MsgList *l, Role r)
{
    if (l->n + 1 > l->cap) {
        l->cap = l->cap ? l->cap * 2 : 16;
        l->items = (Msg *)realloc(l->items, l->cap * sizeof(Msg));
    }
    Msg *m = &l->items[l->n++];
    memset(m, 0, sizeof(*m));
    m->role = r;
    str_init(&m->text);
    str_init(&m->thinking);
    return m;
}

void msg_add_call(Msg *m, const char *id, const char *name, const char *args_json)
{
    m->calls = (ToolCall *)realloc(m->calls, (m->n_calls + 1) * sizeof(ToolCall));
    ToolCall *c = &m->calls[m->n_calls++];
    c->id = str_dup(id ? id : "");
    c->name = str_dup(name ? name : "");
    c->args = str_dup(args_json ? args_json : "{}");
}

void turn_init(Turn *t)
{
    memset(t, 0, sizeof(*t));
    str_init(&t->text);
    str_init(&t->thinking);
    str_init(&t->stop_reason);
    str_init(&t->calls.text);
    str_init(&t->calls.thinking);
}

void turn_free(Turn *t)
{
    str_free(&t->text);
    str_free(&t->thinking);
    str_free(&t->stop_reason);
    msg_free(&t->calls);
}

/* ------------------------------------------------------------------ */
/* Imagens anexadas no texto como "[imagem: <caminho>]"                */
/* ------------------------------------------------------------------ */
#define MAX_IMGS 4

/* Junta os anexos da mensagem com os caminhos citados no texto. */
static int extract_images(const Msg *m, const char *text, char paths[MAX_IMGS][512])
{
    int n = 0;
    if (m && m->n_imgs > 0) {
        for (int i = 0; i < m->n_imgs && n < MAX_IMGS; i++) {
            if (!m->imgs[i]) continue;
            snprintf(paths[n], 512, "%s", m->imgs[i]);
            n++;
        }
        return n;
    }
    /* anexo antigo: caminho vindo no texto */
    if (!text) return 0;
    const char *p = text;
    while (n < MAX_IMGS && (p = strstr(p, "[imagem: ")) != NULL) {
        p += 9;
        const char *fim = strchr(p, ']');
        if (!fim) break;
        size_t l = (size_t)(fim - p);
        if (l > 0 && l < 511) { memcpy(paths[n], p, l); paths[n][l] = 0; n++; }
        p = fim;
    }
    return n;
}

/* Escreve o valor de "content" de uma mensagem do usuario: texto puro, ou
   um array com o texto e as imagens em base64. */
static const char *mime_da_imagem(const char *path)
{
    const char *p = strrchr(path, '.');
    if (!p) return "image/png";
    if (str_ieq(p, ".jpg") || str_ieq(p, ".jpeg")) return "image/jpeg";
    if (str_ieq(p, ".gif")) return "image/gif";
    if (str_ieq(p, ".webp")) return "image/webp";
    if (str_ieq(p, ".bmp")) return "image/bmp";
    return "image/png";
}

static void emit_user_content(Str *o, const Msg *m, const Provider *prov, int anthropic)
{
    const char *text = m->text.data ? m->text.data : "";
    char paths[MAX_IMGS][512];
    int n = prov->support_image ? extract_images(m, text, paths) : 0;
    if (n == 0) {
        json_write_string(o, text, m->text.len);
        return;
    }

    str_addc(o, '[');
    str_adds(o, "{\"type\":\"text\",\"text\":");
    json_write_string(o, text, m->text.len);
    str_addc(o, '}');

    for (int i = 0; i < n; i++) {
        size_t len = 0;
        char *dados = file_read(paths[i], &len);
        if (!dados) continue;
        Str b64; str_init(&b64);
        base64_encode(&b64, (const unsigned char *)dados, len);
        free(dados);
        const char *mime = mime_da_imagem(paths[i]);
        if (anthropic) {
            str_adds(o, ",{\"type\":\"image\",\"source\":{\"type\":\"base64\",\"media_type\":\"");
            str_adds(o, mime);
            str_adds(o, "\",\"data\":\"");
            str_add(o, b64.data, b64.len);
            str_adds(o, "\"}}");
        } else {
            str_adds(o, ",{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:");
            str_adds(o, mime);
            str_adds(o, ";base64,");
            str_add(o, b64.data, b64.len);
            str_adds(o, "\"}}");
        }
        str_free(&b64);
    }
    str_addc(o, ']');
}

/* ------------------------------------------------------------------ */
/* Montagem do corpo (OpenAI)                                          */
/* ------------------------------------------------------------------ */
static void json_role(Str *o, Role r)
{
    switch (r) {
        case ROLE_SYSTEM:    str_adds(o, "system"); break;
        case ROLE_USER:      str_adds(o, "user"); break;
        case ROLE_ASSISTANT: str_adds(o, "assistant"); break;
        case ROLE_TOOL:      str_adds(o, "tool"); break;
    }
}

static void build_openai_body(const Provider *p, const MsgList *msgs, const char *system_prompt,
                              const char *tools_json, Str *o)
{
    str_adds(o, "{\"model\":");
    json_write_string(o, p->model, strlen(p->model));
    str_adds(o, ",\"stream\":true,\"stream_options\":{\"include_usage\":true}");

    str_adds(o, ",\"messages\":[");
    int first = 1;
    if (system_prompt && *system_prompt) {
        str_adds(o, "{\"role\":\"system\",\"content\":");
        json_write_string(o, system_prompt, strlen(system_prompt));
        str_addc(o, '}');
        first = 0;
    }
    for (size_t i = 0; i < msgs->n; i++) {
        const Msg *m = &msgs->items[i];
        if (m->role == ROLE_SYSTEM) continue;
        if (!first) str_addc(o, ',');
        first = 0;
        str_adds(o, "{\"role\":\"");
        json_role(o, m->role);
        str_adds(o, "\",");
        if (m->role == ROLE_TOOL) {
            str_adds(o, "\"tool_call_id\":");
            json_write_string(o, m->tool_call_id ? m->tool_call_id : "", strlen(m->tool_call_id ? m->tool_call_id : ""));
            str_adds(o, ",\"content\":");
            json_write_string(o, m->text.data ? m->text.data : "", m->text.len);
            str_addc(o, '}');
            continue;
        }
        if (m->role == ROLE_ASSISTANT && m->thinking.len) {
            str_adds(o, "\"reasoning_content\":");
            json_write_string(o, m->thinking.data, m->thinking.len);
            str_addc(o, ',');
        }
        if (m->role == ROLE_ASSISTANT && m->n_calls > 0) {
            str_adds(o, "\"content\":");
            if (m->text.len) json_write_string(o, m->text.data, m->text.len);
            else str_adds(o, "null");
            str_adds(o, ",\"tool_calls\":[");
            for (size_t k = 0; k < m->n_calls; k++) {
                if (k) str_addc(o, ',');
                str_adds(o, "{\"id\":");
                json_write_string(o, m->calls[k].id, strlen(m->calls[k].id));
                str_adds(o, ",\"type\":\"function\",\"function\":{\"name\":");
                json_write_string(o, m->calls[k].name, strlen(m->calls[k].name));
                str_adds(o, ",\"arguments\":");
                json_write_string(o, m->calls[k].args, strlen(m->calls[k].args));
                str_adds(o, "}}");
            }
            str_adds(o, "]}");
            continue;
        }
        str_adds(o, "\"content\":");
        if (m->role == ROLE_USER) emit_user_content(o, m, p, 0);
        else json_write_string(o, m->text.data ? m->text.data : "", m->text.len);
        str_addc(o, '}');
    }
    str_addc(o, ']');

    if (tools_json && *tools_json) {
        str_adds(o, ",\"tools\":");
        str_adds(o, tools_json);
    }
    if (p->max_tokens > 0) {
        char tmp[64];
        snprintf(tmp, sizeof tmp, ",\"max_tokens\":%d", p->max_tokens);
        str_adds(o, tmp);
    }
    str_addc(o, '}');
}

/* ------------------------------------------------------------------ */
/* Montagem do corpo (Anthropic)                                       */
/* ------------------------------------------------------------------ */
static void build_anthropic_body(const Provider *p, const MsgList *msgs, const char *system_prompt,
                                 const char *tools_json, Str *o)
{
    str_adds(o, "{\"model\":");
    json_write_string(o, p->model, strlen(p->model));
    {
        char tmp[64];
        snprintf(tmp, sizeof tmp, ",\"max_tokens\":%d,\"stream\":true", p->max_tokens > 0 ? p->max_tokens : 8192);
        str_adds(o, tmp);
    }
    if (system_prompt && *system_prompt) {
        str_adds(o, ",\"system\":");
        json_write_string(o, system_prompt, strlen(system_prompt));
    }
    if (tools_json && *tools_json) {
        str_adds(o, ",\"tools\":");
        str_adds(o, tools_json);
    }

    str_adds(o, ",\"messages\":[");
    int first = 1;
    for (size_t i = 0; i < msgs->n; i++) {
        const Msg *m = &msgs->items[i];
        if (m->role == ROLE_SYSTEM) continue;
        if (!first) str_addc(o, ',');
        first = 0;
        str_adds(o, "{\"role\":\"");
        str_adds(o, m->role == ROLE_ASSISTANT ? "assistant" : "user");
        str_adds(o, "\",\"content\":[");
        int fc = 1;
        if (m->role == ROLE_TOOL) {
            str_adds(o, "{\"type\":\"tool_result\",\"tool_use_id\":");
            json_write_string(o, m->tool_call_id ? m->tool_call_id : "", strlen(m->tool_call_id ? m->tool_call_id : ""));
            if (m->is_error) str_adds(o, ",\"is_error\":true");
            str_adds(o, ",\"content\":");
            json_write_string(o, m->text.data ? m->text.data : "", m->text.len);
            str_addc(o, '}');
            fc = 0;
        } else {
            if (m->thinking.len) {
                str_adds(o, "{\"type\":\"thinking\",\"thinking\":");
                json_write_string(o, m->thinking.data, m->thinking.len);
                str_adds(o, "}");
                fc = 0;
            }
            if (m->text.len) {
                if (!fc) str_addc(o, ',');
                fc = 0;
                if (m->role == ROLE_USER) {
                    /* pode virar [texto, imagem...] */
                    Str tmp; str_init(&tmp);
                    emit_user_content(&tmp, m, p, 1);
                    if (tmp.data && tmp.data[0] == '[') {
                        str_add(o, tmp.data + 1, tmp.len - 2);   /* tira os [] externos */
                    } else {
                        str_adds(o, "{\"type\":\"text\",\"text\":");
                        json_write_string(o, m->text.data, m->text.len);
                        str_adds(o, "}");
                    }
                    str_free(&tmp);
                } else {
                    str_adds(o, "{\"type\":\"text\",\"text\":");
                    json_write_string(o, m->text.data, m->text.len);
                    str_adds(o, "}");
                }
            }
            for (size_t k = 0; k < m->n_calls; k++) {
                if (!fc) str_addc(o, ',');
                fc = 0;
                str_adds(o, "{\"type\":\"tool_use\",\"id\":");
                json_write_string(o, m->calls[k].id, strlen(m->calls[k].id));
                str_adds(o, ",\"name\":");
                json_write_string(o, m->calls[k].name, strlen(m->calls[k].name));
                str_adds(o, ",\"input\":");
                str_adds(o, (m->calls[k].args && *m->calls[k].args) ? m->calls[k].args : "{}");
                str_adds(o, "}");
            }
        }
        if (fc) str_adds(o, "{\"type\":\"text\",\"text\":\"\"}");
        str_adds(o, "]}");
    }
    str_addc(o, ']');
    str_addc(o, '}');
}

void provider_tools_json(const char *provider, Str *out)
{
    int anthropic = str_istarts(provider, "anthropic");
    str_addc(out, '[');
    int first = 1;
    for (const Tool *t = tools_all(); t->name; t++) {
        JValue *schema = json_parse_cstr(t->schema);
        if (!schema) continue;
        if (!first) str_addc(out, ',');
        first = 0;
        if (anthropic) {
            str_adds(out, "{\"name\":");
            json_write_string(out, t->name, strlen(t->name));
            str_adds(out, ",\"description\":");
            json_write_string(out, t->description, strlen(t->description));
            str_adds(out, ",\"input_schema\":");
            str_adds(out, t->schema);
            str_addc(out, '}');
        } else {
            str_adds(out, "{\"type\":\"function\",\"function\":{\"name\":");
            json_write_string(out, t->name, strlen(t->name));
            str_adds(out, ",\"description\":");
            json_write_string(out, t->description, strlen(t->description));
            str_adds(out, ",\"parameters\":");
            str_adds(out, t->schema);
            str_adds(out, "}}");
        }
        json_free(schema);
    }
    str_addc(out, ']');
}

/* ------------------------------------------------------------------ */
/* Parser de eventos SSE (compartilhado)                               */
/* ------------------------------------------------------------------ */
typedef struct {
    const char *provider;
    Turn *turn;
    DeltaCB cb;
    void *ud;
    /* acumuladores de tool calls do OpenAI: index -> dados */
    struct { int index; char *id; Str name; Str args; } *pend;
    size_t n_pend, cap_pend;
    int current_block_is_tool;
    Str current_tool_id;
    Str current_tool_name;
    Str current_tool_args;
    int in_tool;
    int abortado;             /* o usuario cortou (Ctrl+C) */
} StreamState;

static void pend_free(StreamState *st)
{
    for (size_t i = 0; i < st->n_pend; i++) {
        free(st->pend[i].id);
        str_free(&st->pend[i].name);
        str_free(&st->pend[i].args);
    }
    free(st->pend);
    st->pend = NULL;
    st->n_pend = 0;
    st->cap_pend = 0;
}

static void stream_state_init(StreamState *st, const char *provider, Turn *turn, DeltaCB cb, void *ud)
{
    memset(st, 0, sizeof(*st));
    st->provider = provider;
    st->turn = turn;
    st->cb = cb;
    st->ud = ud;
    str_init(&st->current_tool_id);
    str_init(&st->current_tool_name);
    str_init(&st->current_tool_args);
}

static void stream_state_free(StreamState *st)
{
    pend_free(st);
    str_free(&st->current_tool_id);
    str_free(&st->current_tool_name);
    str_free(&st->current_tool_args);
}

int provider_stream_abort = 0;

static void (*g_wait_tick)(void *) = NULL;
static void *g_wait_ud = NULL;

void provider_set_wait_tick(void (*fn)(void *), void *ud)
{
    g_wait_tick = fn;
    g_wait_ud = ud;
}

/* Um "respiro" da leitura: a UI desenha e le o teclado. */
static void wait_tick(void)
{
    if (g_wait_tick) g_wait_tick(g_wait_ud);
}

static void emit(StreamState *st, int kind, const char *s, size_t n)
{
    if (provider_stream_abort) return;      /* o leitor ja vai cortar */
    if (st->cb && n) st->cb(st->ud, kind, s, n);
}

static void handle_openai_delta(StreamState *st, JValue *delta)
{
    JValue *content = json_get(delta, "content");
    if (content && content->kind == J_STR && content->str && *content->str) {
        str_adds(&st->turn->text, content->str);
        emit(st, 0, content->str, strlen(content->str));
    }
    JValue *reasoning = json_get(delta, "reasoning_content");
    if (!reasoning) reasoning = json_get(delta, "reasoning");   /* OpenRouter */
    if (reasoning && reasoning->kind == J_STR && reasoning->str && *reasoning->str) {
        str_adds(&st->turn->thinking, reasoning->str);
        emit(st, 1, reasoning->str, strlen(reasoning->str));
    }
    JValue *calls = json_get(delta, "tool_calls");
    if (calls && calls->kind == J_ARR) {
        for (size_t i = 0; i < calls->count; i++) {
            JValue *c = calls->items[i];
            int index = (int)json_num(json_get(c, "index"), (double)i);
            if (st->n_pend + 1 > st->cap_pend) {
                st->cap_pend = st->cap_pend ? st->cap_pend * 2 : 4;
                st->pend = (void *)realloc(st->pend, st->cap_pend * sizeof(st->pend[0]));
            }
            /* procura por index */
            size_t slot = st->n_pend;
            for (size_t k = 0; k < st->n_pend; k++) if (st->pend[k].index == index) { slot = k; break; }
            if (slot == st->n_pend) {
                st->pend[slot].index = index;
                st->pend[slot].id = NULL;
                str_init(&st->pend[slot].name);
                str_init(&st->pend[slot].args);
                st->n_pend++;
            }
            JValue *id = json_get(c, "id");
            if (id && id->kind == J_STR && id->str && *id->str) {
                free(st->pend[slot].id);
                st->pend[slot].id = str_dup(id->str);
            }
            JValue *fn = json_get(c, "function");
            if (fn) {
                JValue *nm = json_get(fn, "name");
                JValue *ar = json_get(fn, "arguments");
                if (nm && nm->kind == J_STR && nm->str) str_adds(&st->pend[slot].name, nm->str);
                if (ar && ar->kind == J_STR && ar->str) str_adds(&st->pend[slot].args, ar->str);
            }
        }
    }
}

static void handle_anthropic_event(StreamState *st, const char *evname, JValue *v)
{
    if (str_eq(evname, "message_start")) {
        JValue *msg = json_get(v, "message");
        JValue *u = msg ? json_get(msg, "usage") : NULL;
        if (u) {
            st->turn->usage_in = (long)json_num(json_get(u, "input_tokens"), 0);
            st->turn->cache_read = (long)json_num(json_get(u, "cache_read_input_tokens"), 0);
            st->turn->cache_write = (long)json_num(json_get(u, "cache_creation_input_tokens"), 0);
        }
        return;
    }
    if (str_eq(evname, "content_block_start")) {
        JValue *blk = json_get(v, "content_block");
        JValue *type = blk ? json_get(blk, "type") : NULL;
        if (type && str_eq(json_str(type), "tool_use")) {
            st->in_tool = 1;
            str_clear(&st->current_tool_id);
            str_clear(&st->current_tool_name);
            str_clear(&st->current_tool_args);
            str_adds(&st->current_tool_id, json_str(json_get(blk, "id")));
            str_adds(&st->current_tool_name, json_str(json_get(blk, "name")));
        }
        return;
    }
    if (str_eq(evname, "content_block_delta")) {
        JValue *delta = json_get(v, "delta");
        JValue *type = delta ? json_get(delta, "type") : NULL;
        const char *t = json_str(type);
        if (str_eq(t, "text_delta")) {
            const char *s = json_str(json_get(delta, "text"));
            str_adds(&st->turn->text, s);
            emit(st, 0, s, strlen(s));
        } else if (str_eq(t, "thinking_delta")) {
            const char *s = json_str(json_get(delta, "thinking"));
            str_adds(&st->turn->thinking, s);
            emit(st, 1, s, strlen(s));
        } else if (str_eq(t, "input_json_delta")) {
            str_adds(&st->current_tool_args, json_str(json_get(delta, "partial_json")));
        }
        return;
    }
    if (str_eq(evname, "content_block_stop")) {
        if (st->in_tool) {
            msg_add_call(&st->turn->calls, st->current_tool_id.data, st->current_tool_name.data,
                         st->current_tool_args.len ? st->current_tool_args.data : "{}");
            st->in_tool = 0;
        }
        return;
    }
    if (str_eq(evname, "message_delta")) {
        JValue *delta = json_get(v, "delta");
        if (delta) {
            const char *sr = json_str(json_get(delta, "stop_reason"));
            if (*sr) str_adds(&st->turn->stop_reason, sr);
        }
        JValue *u = json_get(v, "usage");
        if (u) {
            st->turn->usage_out = (long)json_num(json_get(u, "output_tokens"), 0);
            double in = json_num(json_get(u, "input_tokens"), -1);
            if (in >= 0) st->turn->usage_in = (long)in;
        }
        return;
    }
}

/* Processa uma linha "data: {...}" ou "event: nome". */
static void handle_sse_line(StreamState *st, const char *line, size_t len, Str *evname)
{
    if (len == 0) return;
    if (len >= 6 && memcmp(line, "event:", 6) == 0) {
        size_t i = 6;
        while (i < len && line[i] == ' ') i++;
        str_clear(evname);
        str_add(evname, line + i, len - i);
        return;
    }
    if (len < 5 || memcmp(line, "data:", 5) != 0) {
        /* Alguns provedores mandam JSON puro por linha */
        if (line[0] == '{') {
            JValue *v = json_parse(line, len);
            if (v) {
                if (str_istarts(st->provider, "anthropic")) handle_anthropic_event(st, evname->data ? evname->data : "", v);
                else {
                    JValue *choices = json_get(v, "choices");
                    JValue *c0 = json_at(choices, 0);
                    JValue *delta = c0 ? json_get(c0, "delta") : NULL;
                    if (delta) handle_openai_delta(st, delta);
                }
                json_free(v);
            }
        }
        return;
    }
    size_t i = 5;
    while (i < len && line[i] == ' ') i++;
    if (i >= len) return;
    if (i + 6 <= len && memcmp(line + i, "[DONE]", 6) == 0) return;

    JValue *v = json_parse(line + i, len - i);
    if (!v) return;

    if (str_istarts(st->provider, "anthropic")) {
        handle_anthropic_event(st, evname->data ? evname->data : "", v);
    } else {
        JValue *choices = json_get(v, "choices");
        JValue *c0 = json_at(choices, 0);
        if (c0) {
            JValue *delta = json_get(c0, "delta");
            if (delta) handle_openai_delta(st, delta);
            const char *fr = json_str(json_get(c0, "finish_reason"));
            if (*fr) { str_clear(&st->turn->stop_reason); str_adds(&st->turn->stop_reason, fr); }
        }
        JValue *u = json_get(v, "usage");
        if (u) {
            st->turn->usage_in = (long)json_num(json_get(u, "prompt_tokens"), st->turn->usage_in);
            st->turn->usage_out = (long)json_num(json_get(u, "completion_tokens"), st->turn->usage_out);
            JValue *det = json_get(u, "prompt_tokens_details");
            if (det) st->turn->cache_read = (long)json_num(json_get(det, "cached_tokens"), 0);
        }
        JValue *err = json_get(v, "error");
        if (err) {
            const char *msg = json_str(json_get(err, "message"));
            str_addf(&st->turn->text, "[erro do provedor] %s", msg);
        }
    }
    json_free(v);
}

/* Alimenta o parser SSE com bytes crus (linhas incompletas ficam no linebuf).
   Usado pelas duas plataformas. */
static void feed_sse(StreamState *st, Str *evname, Str *linebuf, const char *buf, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        char c = buf[i];
        if (c == '\n') {
            size_t ll = linebuf->len;
            while (ll > 0 && linebuf->data[ll - 1] == '\r') ll--;
            if (ll == 0) str_clear(evname);
            else handle_sse_line(st, linebuf->data, ll, evname);
            str_clear(linebuf);
        } else {
            str_addc(linebuf, c);
        }
    }
}

/* ------------------------------------------------------------------ */
/* HTTP                                                                */
/* ------------------------------------------------------------------ */
typedef struct {
    char *scheme;   /* https */
    char *host;
    int   port;
    char *path;
} Url;

static int parse_url(const char *url, Url *out)
{
    memset(out, 0, sizeof(*out));
    const char *p = strstr(url, "://");
    if (!p) return 0;
    out->scheme = str_ndup(url, (size_t)(p - url));
    p += 3;
    const char *slash = strchr(p, '/');
    const char *host_end = slash ? slash : p + strlen(p);
    const char *colon = NULL;
    for (const char *q = p; q < host_end; q++) if (*q == ':') colon = q;
    if (colon) {
        out->host = str_ndup(p, (size_t)(colon - p));
        out->port = atoi(colon + 1);
    } else {
        out->host = str_ndup(p, (size_t)(host_end - p));
        out->port = str_istarts(out->scheme, "https") ? 443 : 80;
    }
    out->path = str_dup(slash ? slash : "/");
    return 1;
}

static void url_free(Url *u) { free(u->scheme); free(u->host); free(u->path); }

/* Verifica se um cabecalho ja esta definido no JSON (case-insensitive). */
static int json_tem_chave(const char *json, const char *name)
{
    if (!json || !*json || !name) return 0;
    size_t n = strlen(name);
    for (const char *s = json; *s; s++) {
        if (*s != '"') continue;
        size_t i = 0;
        while (i < n && s[1 + i]) {
            char a = s[1 + i], b = name[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
            i++;
        }
        if (i == n && s[1 + n] == '"') return 1;
    }
    return 0;
}

/* Cabecalhos HTTP do streaming (usados pelo WinHTTP e pelo curl no POSIX). */
static void provider_headers(const Provider *p, const char *session_id,
                             const char *url, Str *headers)
{
    str_adds(headers, "Content-Type: application/json\r\n");
    str_adds(headers, "Accept: text/event-stream\r\n");
    if (p->extra_headers_json && *p->extra_headers_json) {
        JValue *h = json_parse_cstr(p->extra_headers_json);
        if (h && h->kind == J_OBJ) {
            for (size_t i = 0; i < h->count; i++) {
                JValue *v = h->pairs[i].val;
                if (v->kind != J_STR || !v->str) continue;
                str_adds(headers, h->pairs[i].key);
                str_adds(headers, ": ");
                str_adds(headers, v->str);
                str_adds(headers, "\r\n");
            }
        }
        json_free(h);
    }
    if (p->provider &&
        (str_eq(p->provider, "opencode") || str_eq(p->provider, "opencode-go") ||
         str_eq(p->provider, "opencode-zen") || (url && strstr(url, "opencode.ai")))) {
        if (session_id && *session_id) {
            if (!json_tem_chave(p->extra_headers_json, "x-opencode-session")) {
                str_adds(headers, "x-opencode-session: ");
                str_adds(headers, session_id);
                str_adds(headers, "\r\n");
            }
            if (!json_tem_chave(p->extra_headers_json, "x-opencode-client"))
                str_adds(headers, "x-opencode-client: pi\r\n");
        }
    }
    if (str_istarts(p->provider, "anthropic")) {
        str_adds(headers, "x-api-key: ");
        str_adds(headers, p->api_key ? p->api_key : "");
        str_adds(headers, "\r\nanthropic-version: 2023-06-01\r\n");
    } else {
        str_adds(headers, "Authorization: Bearer ");
        str_adds(headers, p->api_key ? p->api_key : "");
        str_adds(headers, "\r\n");
    }
}

#ifdef _WIN32
/* ------------------------------------------------------------------ */
/* Bomba: TODO o I/O de rede roda numa thread                          */
/* ------------------------------------------------------------------ */
/* WinHttpReadData/WinHttpReceiveResponse bloqueiam ate' o proximo byte e
   `WinHttpSetTimeouts` de recepcao NAO interrompe essa espera (medido: 1 s
   configurado num servidor que segura 3 s, zero timeouts). Como a TUI
   precisa do thread da UI livre para animar o spinner e ler o teclado, a
   thread faz envio, cabecalhos e leitura; ela enche um buffer e a UI drena
   a cada ~15 ms. O parser SSE continua na thread da UI. */
typedef struct {
    HINTERNET req, conn, session;
    WCHAR   *whdr;              /* cabecalhos (dono: a bomba) */
    DWORD    whlen;
    char    *corpo;             /* corpo do POST (dono: a bomba) */
    size_t   corpo_len;
    CRITICAL_SECTION cs;
    Str      buf;               /* bytes ainda nao consumidos (protegido por cs) */
    int      fim;               /* terminou (fim normal, erro ou abort) */
    int      enviou;            /* 1 = pedido enviado e resposta recebida */
    DWORD    status;            /* HTTP status (0 = sem resposta) */
    DWORD    erro;              /* GetLastError da falha (0 = sem falha) */
    volatile LONG parar;        /* o consumidor pediu para encerrar */
} Bomba;

static DWORD WINAPI bomba_main(LPVOID pp)
{
    Bomba *b = (Bomba *)pp;
    char tmp[8192];

    if (!WinHttpSendRequest(b->req, b->whdr, b->whlen, (LPVOID)b->corpo,
                            (DWORD)b->corpo_len, (DWORD)b->corpo_len, 0)) {
        b->erro = GetLastError();
        goto fim;
    }
    if (!WinHttpReceiveResponse(b->req, NULL)) {
        b->erro = GetLastError();
        goto fim;
    }
    {
        DWORD st = 0, sl = sizeof st;
        WinHttpQueryHeaders(b->req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &st, &sl, WINHTTP_NO_HEADER_INDEX);
        b->status = st;
        b->enviou = 1;
    }
    for (;;) {
        DWORD got = 0;
        int grande = 0;
        if (b->parar || provider_stream_abort) break;
        if (!WinHttpReadData(b->req, tmp, sizeof tmp, &got)) { b->erro = GetLastError(); break; }
        if (got == 0) break;
        EnterCriticalSection(&b->cs);
        str_add(&b->buf, tmp, got);
        grande = (b->status >= 300 && b->buf.len > 8192);
        LeaveCriticalSection(&b->cs);
        if (grande) break;      /* corpo de erro: 8 KB bastam para a mensagem */
    }

fim:
    EnterCriticalSection(&b->cs);
    b->fim = 1;
    LeaveCriticalSection(&b->cs);
    WinHttpCloseHandle(b->req);
    WinHttpCloseHandle(b->conn);
    WinHttpCloseHandle(b->session);
    free(b->whdr);
    free(b->corpo);
    return 0;
}

static void bomba_free(Bomba *b)
{
    if (!b) return;
    str_free(&b->buf);
    DeleteCriticalSection(&b->cs);
    free(b);
}

/* Monta o pedido e devolve a bomba pronta (ou NULL, com `err` preenchido). */
static Bomba *bomba_nova(HINTERNET req, HINTERNET conn, HINTERNET session,
                         WCHAR *whdr, DWORD whlen, const char *body, size_t body_len)
{
    Bomba *b = (Bomba *)calloc(1, sizeof *b);
    if (!b) return NULL;
    if (body_len) {
        b->corpo = (char *)malloc(body_len);
        if (!b->corpo) { free(b); return NULL; }
        memcpy(b->corpo, body, body_len);
    }
    InitializeCriticalSection(&b->cs);
    str_init(&b->buf);
    b->req = req;
    b->conn = conn;
    b->session = session;
    b->whdr = whdr;
    b->whlen = whlen;
    b->corpo_len = body_len;
    return b;
}

static int http_post_stream(const char *url, const Provider *p, const char *session_id,
                            const char *body, size_t body_len, StreamState *st, Str *err)
{
    Url u;
    if (!parse_url(url, &u)) { str_adds(err, "URL invalida"); return 0; }

    int wide_host = MultiByteToWideChar(CP_UTF8, 0, u.host, -1, NULL, 0);
    WCHAR *whost = (WCHAR *)malloc((size_t)wide_host * sizeof(WCHAR));
    MultiByteToWideChar(CP_UTF8, 0, u.host, -1, whost, wide_host);
    int wide_path = MultiByteToWideChar(CP_UTF8, 0, u.path, -1, NULL, 0);
    WCHAR *wpath = (WCHAR *)malloc((size_t)wide_path * sizeof(WCHAR));
    MultiByteToWideChar(CP_UTF8, 0, u.path, -1, wpath, wide_path);

    HINTERNET session = WinHttpOpen(L"delta-agent/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { str_adds(err, "WinHttpOpen falhou"); free(whost); free(wpath); url_free(&u); return 0; }
    /* conecta/entrega com teto; recepcao longa (o modelo pode demorar entre
       tokens), mas nao infinita, para a thread nao ficar presa para sempre */
    WinHttpSetTimeouts(session, 30000, 30000, 30000, 300000);

    HINTERNET conn = WinHttpConnect(session, whost, (INTERNET_PORT)u.port, 0);
    if (!conn) { str_adds(err, "WinHttpConnect falhou"); WinHttpCloseHandle(session); free(whost); free(wpath); url_free(&u); return 0; }

    HINTERNET req = WinHttpOpenRequest(conn, L"POST", wpath, NULL, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       str_istarts(u.scheme, "https") ? WINHTTP_FLAG_SECURE : 0);
    if (!req) { str_adds(err, "WinHttpOpenRequest falhou"); WinHttpCloseHandle(conn); WinHttpCloseHandle(session); free(whost); free(wpath); url_free(&u); return 0; }

    Str headers; str_init(&headers);
    provider_headers(p, session_id, url, &headers);
    /* cabecalhos extras do config.json (provedores que exigem os seus) */
    if (p->extra_headers_json && *p->extra_headers_json) {
        JValue *h = json_parse_cstr(p->extra_headers_json);
        if (h && h->kind == J_OBJ) {
            for (size_t i = 0; i < h->count; i++) {
                JValue *v = h->pairs[i].val;
                if (v->kind != J_STR || !v->str) continue;
                str_adds(&headers, h->pairs[i].key);
                str_adds(&headers, ": ");
                str_adds(&headers, v->str);
                str_adds(&headers, "\r\n");
            }
        }
        json_free(h);
    }
    /* opencode.ai roteia por conversa e exige o id da sessao (como o Delta faz) */
    if (p->provider &&
        (str_eq(p->provider, "opencode") || str_eq(p->provider, "opencode-go") ||
         str_eq(p->provider, "opencode-zen") || strstr(url, "opencode.ai"))) {
        if (session_id && *session_id) {
            if (!json_tem_chave(p->extra_headers_json, "x-opencode-session")) {
                str_adds(&headers, "x-opencode-session: ");
                str_adds(&headers, session_id);
                str_adds(&headers, "\r\n");
            }
            if (!json_tem_chave(p->extra_headers_json, "x-opencode-client"))
                str_adds(&headers, "x-opencode-client: pi\r\n");
        }
    }
    if (str_istarts(p->provider, "anthropic")) {
        str_adds(&headers, "x-api-key: ");
        str_adds(&headers, p->api_key ? p->api_key : "");
        str_adds(&headers, "\r\nanthropic-version: 2023-06-01\r\n");
    } else {
        str_adds(&headers, "Authorization: Bearer ");
        str_adds(&headers, p->api_key ? p->api_key : "");
        str_adds(&headers, "\r\n");
    }

    int wlen = MultiByteToWideChar(CP_UTF8, 0, headers.data, (int)headers.len, NULL, 0);
    WCHAR *whdr = (WCHAR *)malloc(((size_t)wlen + 1) * sizeof(WCHAR));
    MultiByteToWideChar(CP_UTF8, 0, headers.data, (int)headers.len, whdr, wlen);
    whdr[wlen] = 0;
    str_free(&headers);
    free(whost);
    free(wpath);
    url_free(&u);

    Bomba *b = bomba_nova(req, conn, session, whdr, (DWORD)wlen, body, body_len);
    if (!b) {
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(session);
        free(whdr);
        str_adds(err, "sem memoria para o pedido");
        return 0;
    }

    Str evname; str_init(&evname);
    Str linebuf; str_init(&linebuf);
    Str corpo_erro; str_init(&corpo_erro);
    DWORD status = 0, erro = 0;
    int enviou = 0;
    int abortado = 0;

    HANDLE th = CreateThread(NULL, 0, bomba_main, b, 0, NULL);
    if (!th) {
        /* sem thread (raro): faz o mesmo caminho na propria thread da UI */
        bomba_main(b);
    }

    for (;;) {
        Str chunk; str_init(&chunk);
        int acabou = 0;
        EnterCriticalSection(&b->cs);
        if (b->buf.len) {
            str_add(&chunk, b->buf.data, b->buf.len);
            str_clear(&b->buf);
        }
        acabou = b->fim;
        status = b->status;
        erro = b->erro;
        enviou = b->enviou;
        LeaveCriticalSection(&b->cs);

        if (chunk.len) {
            if (status >= 200 && status < 300) {
                feed_sse(st, &evname, &linebuf, chunk.data, chunk.len);
            } else if (corpo_erro.len < 4096) {
                str_add(&corpo_erro, chunk.data, chunk.len < 4096 ? chunk.len : 4096);
            }
        }
        str_free(&chunk);

        if (provider_stream_abort) { abortado = 1; break; }
        if (acabou) break;
        wait_tick();              /* desenha o spinner e le o teclado */
        if (chunk.len == 0) msleep(15);
    }

    if (th) {
        InterlockedExchange(&b->parar, 1);
        /* espera so' um instante: no caminho normal a bomba ja' terminou. Num
           abort ela pode estar presa num WinHttpReadData (o servidor pode
           demorar minutos para mandar o proximo byte) — nesse caso a bomba
           fecha os proprios handles e o `b` fica orfaoo (dezenas de bytes)
           em vez de travar a UI esperando. */
        if (WaitForSingleObject(th, 150) == WAIT_OBJECT_0) {
            CloseHandle(th);
            bomba_free(b);
            b = NULL;
        } else {
            CloseHandle(th);
            b = NULL;
        }
    }

    str_free(&evname);
    str_free(&linebuf);

    if (abortado) {
        provider_stream_abort = 0;
        st->abortado = 1;
        str_free(&corpo_erro);
        if (err) str_adds(err, "interrompido");
        return 0;
    }
    if (status && (status < 200 || status >= 300)) {
        str_addf(err, "HTTP %lu: ", (unsigned long)status);
        if (corpo_erro.len) str_add(err, corpo_erro.data, corpo_erro.len);
        str_free(&corpo_erro);
        return 0;
    }
    str_free(&corpo_erro);
    if (!enviou) {
        str_addf(err, "falha na requisicao (erro %lu)", (unsigned long)erro);
        return 0;
    }
    return 1;
}
#else
static int http_post_stream(const char *url, const Provider *p, const char *session_id,
                            const char *body, size_t body_len, StreamState *st, Str *err)
{
    /* POSIX: streaming por curl (sem libcurl, sem dependencia de build). O
       corpo vai num arquivo temporario e o SSE e lido do pipe, linha a linha. */
    char tpl[64], tbody[256], thdr[256];
    snprintf(tpl, sizeof tpl, "/tmp/delta-%lu", (unsigned long)getpid());
    snprintf(tbody, sizeof tbody, "%s.body", tpl);
    snprintf(thdr, sizeof thdr, "%s.hdr", tpl);

    if (!file_write(tbody, body, body_len)) {
        str_adds(err, "nao consegui gravar o corpo da requisicao");
        return 0;
    }

    Str cmd; str_init(&cmd);
    str_adds(&cmd, "curl -sS -N --max-time 900 -X POST -D '");
    str_adds(&cmd, thdr);
    str_adds(&cmd, "' '");
    str_adds(&cmd, url);
    str_adds(&cmd, "'");
    {
        Str h; str_init(&h);
        provider_headers(p, session_id, url, &h);
        size_t pos = 0;
        while (pos < h.len) {
            size_t eol = pos;
            while (eol < h.len && h.data[eol] != '\n') eol++;
            size_t ll = eol - pos;
            while (ll && h.data[pos + ll - 1] == '\r') ll--;
            if (ll) {
                str_adds(&cmd, " -H '");
                str_add(&cmd, h.data + pos, ll);
                str_adds(&cmd, "'");
            }
            pos = eol + 1;
        }
        str_free(&h);
    }
    str_adds(&cmd, " --data-binary @'");
    str_adds(&cmd, tbody);
    str_adds(&cmd, "' 2>/dev/null");

    FILE *f = popen(cmd.data, "r");
    str_free(&cmd);
    if (!f) {
        remove(tbody);
        str_adds(err, "curl nao encontrado (instale o curl)");
        return 0;
    }

    Str evname; str_init(&evname);
    Str linebuf; str_init(&linebuf);
    char buf[8192];

    /* pipe de saida do curl em modo nao bloqueante: sem isso a leitura ficava
       parada no primeiro byte e a tela congelava (spinner e Ctrl+C mortos). */
    int fd = fileno(f);
    if (fd < 0) {
        pclose(f);
        remove(tbody);
        str_free(&evname);
        str_free(&linebuf);
        str_adds(err, "nao consegui ler a saida do curl");
        return 0;
    }
    {
        int fl = fcntl(fd, F_GETFL, 0);
        if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    }

    for (;;) {
        if (provider_stream_abort) break;
        wait_tick();
        fd_set rf;
        FD_ZERO(&rf);
        FD_SET(fd, &rf);
        struct timeval tv = { 0, 100000 };
        int r = select(fd + 1, &rf, NULL, NULL, &tv);
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (r == 0) continue;                     /* sem dados: desenha e volta */

        ssize_t got = read(fd, buf, sizeof buf);
        if (got == 0) break;
        if (got < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
            break;
        }
        feed_sse(st, &evname, &linebuf, buf, (size_t)got);
    }
    int rc = pclose(f);
    str_free(&evname);
    str_free(&linebuf);
    remove(tbody);

    if (provider_stream_abort) {
        provider_stream_abort = 0;
        st->abortado = 1;
        remove(thdr);
        str_adds(err, "interrompido");
        return 0;
    }

    int status = 0;
    {
        size_t hl = 0;
        char *htxt = file_read(thdr, &hl);
        if (htxt) {
            const char *sp = strstr(htxt, "HTTP/");
            if (sp) { sp = strchr(sp, ' '); if (sp) status = atoi(sp + 1); }
            free(htxt);
        }
        remove(thdr);
    }
    if (status && (status < 200 || status >= 300)) {
        str_addf(err, "HTTP %d", status);
        return 0;
    }
    if (rc != 0 && st->turn->text.len == 0 && st->turn->calls.n_calls == 0) {
        str_adds(err, "falha na requisicao (curl)");
        return 0;
    }
    return 1;
}
#endif

/* ------------------------------------------------------------------ */
/* GET simples                                                         */
/* ------------------------------------------------------------------ */
#ifdef _WIN32
int http_get_text_to(const char *url, Str *out, Str *err, int timeout_sec)
{
    Url u;
    if (!parse_url(url, &u)) { if (err) str_adds(err, "URL invalida"); return 0; }

    int wh = MultiByteToWideChar(CP_UTF8, 0, u.host, -1, NULL, 0);
    WCHAR *host = (WCHAR *)malloc((size_t)wh * sizeof(WCHAR));
    MultiByteToWideChar(CP_UTF8, 0, u.host, -1, host, wh);
    int wp = MultiByteToWideChar(CP_UTF8, 0, u.path, -1, NULL, 0);
    WCHAR *path = (WCHAR *)malloc((size_t)wp * sizeof(WCHAR));
    MultiByteToWideChar(CP_UTF8, 0, u.path, -1, path, wp);

    int ok = 0;
    HINTERNET session = WinHttpOpen(L"delta-agent/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session) {
        DWORD ms = (DWORD)((timeout_sec > 0 ? timeout_sec : 60) * 1000);
        WinHttpSetTimeouts(session, ms, ms, ms, ms);
    }
    HINTERNET conn = session ? WinHttpConnect(session, host, (INTERNET_PORT)u.port, 0) : NULL;
    HINTERNET req = conn ? WinHttpOpenRequest(conn, L"GET", path, NULL, WINHTTP_NO_REFERER,
                                              WINHTTP_DEFAULT_ACCEPT_TYPES,
                                              str_istarts(u.scheme, "https") ? WINHTTP_FLAG_SECURE : 0) : NULL;
    if (req) {
        /* o GitHub responde 302 para objects.githubusercontent.com: sem isto
           o download do binario morria no redirecionamento */
        DWORD politica = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
        WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &politica, sizeof politica);
    }
    if (req && WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                  WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(req, NULL)) {
        DWORD status = 0, slen = sizeof status;
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &slen, WINHTTP_NO_HEADER_INDEX);
        Str body; str_init(&body);
        for (;;) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
            char *buf = (char *)malloc(avail + 1);
            DWORD got = 0;
            if (!WinHttpReadData(req, buf, avail, &got) || got == 0) { free(buf); break; }
            str_add(&body, buf, got);
            free(buf);
        }
        if (status >= 200 && status < 300) {
            str_add(&body, "", 0);
            *out = body;
            ok = 1;
        } else {
            if (err) str_addf(err, "HTTP %lu", (unsigned long)status);
            str_free(&body);
        }
    } else if (err) {
        str_addf(err, "falha na requisicao (erro %lu)", (unsigned long)GetLastError());
    }
    if (req) WinHttpCloseHandle(req);
    if (conn) WinHttpCloseHandle(conn);
    if (session) WinHttpCloseHandle(session);
    free(host);
    free(path);
    url_free(&u);
    return ok;
}
#else
int http_get_text_to(const char *url, Str *out, Str *err, int timeout_sec)
{
    /* POSIX: usa curl, que ja esta em qualquer distro */
    Str cmd; str_init(&cmd);
    str_addf(&cmd, "curl -fsSL --max-time %d '", timeout_sec > 0 ? timeout_sec : 60);
    str_adds(&cmd, url);
    str_adds(&cmd, "' 2>/dev/null");
    FILE *f = popen(cmd.data, "r");
    str_free(&cmd);
    if (!f) { if (err) str_adds(err, "curl indisponivel"); return 0; }
    char buf[8192];
    size_t got;
    while ((got = fread(buf, 1, sizeof buf, f)) > 0) str_add(out, buf, got);
    int rc = pclose(f);
    if (rc != 0) { if (err) str_adds(err, "curl falhou"); return 0; }
    return 1;
}
#endif

int http_get_text(const char *url, Str *out, Str *err)
{
    return http_get_text_to(url, out, err, 60);
}

/* ------------------------------------------------------------------ */
/* Execucao do turno                                                   */
/* ------------------------------------------------------------------ */
/* Corpo de erro em JSON vira uma mensagem legivel:
   "HTTP 400: {\"error\":{\"message\":\"...\"}}" -> "HTTP 400: ..." */
static void tidy_http_error(Str *e)
{
    const char *s = e->data;
    if (!s || e->len == 0) return;
    const char *body = strstr(s, ": {");
    if (!body) return;
    body += 2;

    JValue *v = json_parse_cstr(body);
    if (!v) return;

    const char *msg = NULL;
    char tipo[128] = "";
    JValue *er = json_get(v, "error");
    if (er && er->kind == J_OBJ) {
        msg = json_str(json_get(er, "message"));
        const char *t = json_str(json_get(er, "type"));
        if (*t) snprintf(tipo, sizeof tipo, "%s - ", t);
    } else if (er && er->kind == J_STR) {
        msg = er->str;
    } else {
        msg = json_str(json_get(v, "message"));
    }

    if (msg && *msg) {
        Str tmp; str_init(&tmp);
        str_add(&tmp, s, (size_t)(body - s) - 1);   /* "HTTP 400:" */
        str_addc(&tmp, ' ');
        str_adds(&tmp, tipo);
        str_adds(&tmp, msg);
        str_free(e);
        *e = tmp;
    }
    json_free(v);
}

/* 429/5xx/rede costumam ser passageiros; 4xx (modelo invalido, chave errada)
   nao melhora repetindo — e o usuario espera 5 s a cada tentativa. */
static int vale_retry(const char *msg)
{
    if (!msg) return 1;
    const char *h = strstr(msg, "HTTP ");
    if (!h) return 1;                       /* falha de rede */
    long status = atol(h + 5);
    return status == 429 || status >= 500;
}

int provider_run(const Provider *p, const MsgList *msgs, const char *system_prompt,
                 Turn *out, DeltaCB cb, void *ud, Str *err)
{
    Str tools; str_init(&tools);
    provider_tools_json(p->provider, &tools);

    Str body; str_init(&body);
    int anthropic = str_istarts(p->provider, "anthropic");
    if (anthropic) build_anthropic_body(p, msgs, system_prompt, tools.data, &body);
    else build_openai_body(p, msgs, system_prompt, tools.data, &body);

    Str url; str_init(&url);
    {
        const char *base = (p->base_url && *p->base_url) ? p->base_url : "";
        size_t bl = strlen(base);
        while (bl > 0 && base[bl - 1] == '/') bl--;
        str_add(&url, base, bl);
        str_adds(&url, anthropic ? "/v1/messages" : "/chat/completions");
    }

    StreamState st;
    Str local_err; str_init(&local_err);
    int ok = 0;

    /* 429/5xx costumam ser passageiros (gateway do provedor): tenta de novo
       com espera crescente, desde que nada tenha sido transmitido ainda. */
    const int TENTATIVAS = 3;
    for (int tentativa = 1; ; tentativa++) {
        memset(&st, 0, sizeof st);
        stream_state_init(&st, p->provider, out, cb, ud);
        str_clear(&local_err);
        ok = http_post_stream(url.data, p, p->session_id, body.data, body.len, &st, &local_err);

        int vazio = (out->text.len == 0 && out->thinking.len == 0 && out->calls.n_calls == 0);
        int falhou = (!ok || local_err.len);
        if (st.abortado) break;             /* Ctrl+C: nao tenta de novo */
        if (!falhou || !vazio || tentativa >= TENTATIVAS) break;
        if (local_err.len && !vale_retry(local_err.data)) break;

        if (local_err.len) tidy_http_error(&local_err);
        if (p->on_retry)
            p->on_retry(p->retry_ud, tentativa, TENTATIVAS,
                        local_err.len ? local_err.data : "falha de conexao");
        stream_state_free(&st);
        memset(&st, 0, sizeof st);
        msleep(5000);          /* 5 s entre as tentativas */
    }

    /* fecha as tool calls do OpenAI */
    for (size_t i = 0; i < st.n_pend; i++) {
        msg_add_call(&out->calls, st.pend[i].id ? st.pend[i].id : "", st.pend[i].name.data,
                     st.pend[i].args.len ? st.pend[i].args.data : "{}");
    }

    if (local_err.len) tidy_http_error(&local_err);

    if (!ok) {
        const char *msg = local_err.len ? local_err.data : "falha na conexao";
        if (err) str_adds(err, msg);
    } else if (local_err.len) {
        /* corpo de erro HTTP */
        if (err) str_adds(err, local_err.data);
        ok = 0;
    }

    if (!ok && err) {
        if (strstr(err->data, "HTTP 429"))
            str_adds(err, "\n[dica] 429 = cota/limite do provedor (ou upstream fora do ar). "
                          "Tente outro modelo (/models) ou repita em alguns segundos.\n");
        else if (strstr(err->data, "Endpoint is unavailable"))
            str_adds(err, "\n[dica] o gateway do provedor nao alcancou o modelo configurado; "
                          "confira o nome com /models.\n");
    }
    str_free(&local_err);
    str_free(&url);
    str_free(&body);
    str_free(&tools);
    return ok;
}
