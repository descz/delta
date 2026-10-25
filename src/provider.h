/* provider.h - streaming de LLM (OpenAI-compativel e Anthropic) via WinHTTP */
#ifndef DELTA_PROVIDER_H
#define DELTA_PROVIDER_H

#include "util.h"
#include "json.h"

typedef enum { ROLE_SYSTEM, ROLE_USER, ROLE_ASSISTANT, ROLE_TOOL } Role;

typedef struct {
    char *id;
    char *name;
    char *args;      /* JSON */
} ToolCall;

typedef struct {
    Role     role;
    Str      text;
    Str      thinking;
    ToolCall *calls;
    size_t   n_calls;
    char    *tool_call_id;   /* ROLE_TOOL */
    char    *tool_name;      /* ROLE_TOOL */
    int      is_error;
    char   **imgs;           /* imagens anexadas (caminhos) */
    int      n_imgs;
} Msg;

typedef struct { Msg *items; size_t n, cap; } MsgList;

void msglist_init(MsgList *l);
void msglist_free(MsgList *l);
Msg *msglist_add(MsgList *l, Role r);
void msg_free(Msg *m);
void msg_add_call(Msg *m, const char *id, const char *name, const char *args_json);

typedef struct {
    Str  text;
    Str  thinking;
    Msg  calls;          /* reaproveita Msg apenas pelos tool calls */
    Str  stop_reason;
    long usage_in, usage_out, cache_read, cache_write;
} Turn;

void turn_init(Turn *t);
void turn_free(Turn *t);

typedef struct {
    char *provider;    /* "anthropic" ou "openai" */
    char *model;
    char *api_key;
    char *base_url;    /* ex.: https://api.anthropic.com */
    int   max_tokens;
    double temperature;
    const char *extra_headers_json;   /* ex.: {"x-opencode-session":"abc"} */
    const char *session_id;           /* id da conversa (provedores opencode) */
    int   support_image;              /* o modelo aceita imagem na entrada */
    /* avisa a UI a cada tentativa que falhou (retry automatico) */
    void (*on_retry)(void *ud, int tentativa, int total, const char *erro);
    void *retry_ud;
} Provider;

/* Callback de streaming: kind 0 = texto, 1 = pensamento. */
typedef void (*DeltaCB)(void *ud, int kind, const char *utf8, size_t len);

/* Executa um turno. Retorna 1 em sucesso; em erro, `err` recebe a mensagem. */
int provider_run(const Provider *p, const MsgList *msgs, const char *system_prompt,
                 Turn *out, DeltaCB cb, void *ud, Str *err);

/* O callback de streaming marca isto para cortar a leitura no meio: o pedido
   e' abandonado e provider_run volta 0 com err "interrompido". */
extern int provider_stream_abort;

/* GET simples (usado para baixar o catalogo models.dev). */
int http_get_text(const char *url, Str *out, Str *err);

/* Monta o array de tools no formato do provedor. */
void provider_tools_json(const char *provider, Str *out);

#endif /* DELTA_PROVIDER_H */
