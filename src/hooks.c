/* hooks.c - eventos do harness via extensao kind="hook". Ver hooks.h.
 *
 * Filosofia: o core fica minimo e quem adapta o comportamento e' a extensao
 * ("change the harness, not your workflow"). Nada aqui e' obrigatorio: sem
 * extensao kind=hook habilitada, cada funcao sai no primeiro if e o custo e'
 * uma leitura de extensions.json.
 */
#include "hooks.h"
#include "tools.h"
#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HOOK_TIMEOUT  5            /* s: hook lento nao pode travar o agente */
#define HOOK_OUT_MAX  (8 * 1024)   /* resposta maior que isso e' ignorada */
#define HOOK_EVENT_MAX 16

/* grava o payload num arquivo em <core>/hooks e devolve o caminho (o hook
   recebe esse caminho em argv[1]); NULL se nao der para escrever */
static char *payload_file(const char *payload, size_t len)
{
    char *home = delta_home();
    char *dir = path_join(home, "hooks");
    dir_make(dir);
    static int seq;
    char nome[64];
    snprintf(nome, sizeof nome, "hook-%llu-%d.json",
             (unsigned long long)now_ms(), ++seq);
    char *p = path_join(dir, nome);
    if (!file_write_atomic(p, payload, len)) {
        free(p);
        p = NULL;
    }
    free(dir);
    free(home);
    return p;
}

/* acha o objeto JSON na saida do hook (ele pode ter logado antes) */
static JValue *parse_reply(const Str *out)
{
    if (!out || !out->data || out->len < 2) return NULL;
    const char *ini = strchr(out->data, '{');
    const char *fim = strrchr(out->data, '}');
    if (!ini || !fim || fim <= ini) return NULL;
    return json_parse(ini, (size_t)(fim - ini + 1));
}

/* roda todos os hooks habilitados do evento; a ultima resposta JSON valida
   vence. Devolve 1 se algum hook respondeu. */
static int dispatch(const char *event, const char *payload, JValue **reply)
{
    int n = ext_count();
    int rodou = 0;
    *reply = NULL;
    for (int i = 0; i < n; i++) {
        char nome[64] = { 0 }, kind[16] = { 0 };
        int enabled = 0;
        if (!ext_get(i, nome, sizeof nome, kind, sizeof kind, &enabled)) continue;
        if (!enabled || !str_eq(kind, "hook") || !str_eq(nome, event)) continue;

        char *cmd = malloc(2048);
        if (!cmd) return rodou;
        if (!ext_get_arg(i, cmd, 2048) || !*cmd) { free(cmd); continue; }

        char *arq = payload_file(payload, strlen(payload));
        if (!arq) { free(cmd); continue; }

        size_t cl = strlen(cmd) + strlen(arq) + 8;
        char *linha = malloc(cl);
        if (linha) {
            snprintf(linha, cl, "%s \"%s\"", cmd, arq);
            Str out; str_init(&out);
            int code = 0;
            if (tools_exec(NULL, linha, HOOK_TIMEOUT, &out, &code) && out.len &&
                out.len <= HOOK_OUT_MAX) {
                JValue *v = parse_reply(&out);
                if (v) {
                    if (*reply) json_free(*reply);
                    *reply = v;
                    rodou = 1;
                }
            }
            str_free(&out);
            free(linha);
        }
        remove(arq);
        free(arq);
        free(cmd);
    }
    return rodou;
}

/* payload comum: {"event":..., "tool":..., ...} */
static void payload_begin(Str *p, const char *event, const char *tool)
{
    str_adds(p, "{\"event\":");
    json_write_string(p, event, strlen(event));
    if (tool) {
        str_adds(p, ",\"tool\":");
        json_write_string(p, tool, strlen(tool));
    }
}

int hooks_tool_call(const char *tool, const char *args_json, const char *cwd,
                    Str *args_out, Str *reason)
{
    Str p; str_init(&p);
    payload_begin(&p, "tool_call", tool);
    str_adds(&p, ",\"args\":");
    if (args_json) json_write_string(&p, args_json, strlen(args_json));
    else           str_adds(&p, "null");
    if (cwd && *cwd) {
        str_adds(&p, ",\"cwd\":");
        json_write_string(&p, cwd, strlen(cwd));
    }
    str_adds(&p, "}");

    JValue *r = NULL;
    int rodou = dispatch("tool_call", p.data ? p.data : "", &r);
    str_free(&p);
    if (!rodou || !r) { if (r) json_free(r); return 1; }

    int seguir = 1;
    JValue *blk = json_get(r, "block");
    if (blk && json_bool(blk, 0)) {
        seguir = 0;
        if (reason) {
            const char *motivo = json_str(json_get(r, "reason"));
            if (motivo && *motivo) str_adds(reason, motivo);
        }
    } else if (args_out) {
        const char *novo = json_str(json_get(r, "args"));   /* string com JSON */
        if (novo && *novo) str_adds(args_out, novo);
    }
    json_free(r);
    return seguir;
}

int hooks_tool_result(const char *tool, const char *output, int is_error, Str *output_out)
{
    Str p; str_init(&p);
    payload_begin(&p, "tool_result", tool);
    str_addf(&p, ",\"is_error\":%d,\"output\":", is_error ? 1 : 0);
    if (output) json_write_string(&p, output, strlen(output));
    else        str_adds(&p, "\"\"");
    str_adds(&p, "}");

    JValue *r = NULL;
    int rodou = dispatch("tool_result", p.data ? p.data : "", &r);
    str_free(&p);
    if (!rodou || !r) { if (r) json_free(r); return 0; }

    int mudou = 0;
    const char *novo = json_str(json_get(r, "output"));
    const char *mais = json_str(json_get(r, "append"));
    if (novo && *novo) {
        str_adds(output_out, novo);
        mudou = 1;
    } else if (mais && *mais) {
        str_adds(output_out, output ? output : "");
        str_adds(output_out, mais);
        mudou = 1;
    }
    json_free(r);
    return mudou;
}

void hooks_turn_end(void)
{
    Str p; str_init(&p);
    payload_begin(&p, "turn_end", NULL);
    str_adds(&p, "}");
    JValue *r = NULL;
    dispatch("turn_end", p.data ? p.data : "", &r);
    if (r) json_free(r);
    str_free(&p);
}

void hooks_session_start(const char *cwd)
{
    Str p; str_init(&p);
    payload_begin(&p, "session_start", NULL);
    if (cwd && *cwd) {
        str_adds(&p, ",\"cwd\":");
        json_write_string(&p, cwd, strlen(cwd));
    }
    str_adds(&p, "}");
    JValue *r = NULL;
    dispatch("session_start", p.data ? p.data : "", &r);
    if (r) json_free(r);
    str_free(&p);
}

int hooks_user_message(const char *text, const char *cwd, Str *reason)
{
    Str p; str_init(&p);
    payload_begin(&p, "user_message", NULL);
    str_adds(&p, ",\"text\":");
    if (text) json_write_string(&p, text, strlen(text));
    else      str_adds(&p, "\"\"");
    if (cwd && *cwd) {
        str_adds(&p, ",\"cwd\":");
        json_write_string(&p, cwd, strlen(cwd));
    }
    str_adds(&p, "}");

    JValue *r = NULL;
    int rodou = dispatch("user_message", p.data ? p.data : "", &r);
    str_free(&p);
    if (!rodou || !r) { if (r) json_free(r); return 1; }

    int seguir = 1;
    JValue *blk = json_get(r, "block");
    if (blk && json_bool(blk, 0)) {
        seguir = 0;
        const char *motivo = json_str(json_get(r, "reason"));
        if (reason) str_adds(reason, (motivo && *motivo) ? motivo : "mensagem recusada pelo hook");
    }
    json_free(r);
    return seguir;
}

void hooks_turn_start(int step)
{
    Str p; str_init(&p);
    payload_begin(&p, "turn_start", NULL);
    str_addf(&p, ",\"step\":%d}", step);
    JValue *r = NULL;
    dispatch("turn_start", p.data ? p.data : "", &r);
    if (r) json_free(r);
    str_free(&p);
}
