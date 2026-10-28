/* agent.h - configuracao, sessao e loop do agente */
#ifndef DELTA_AGENT_H
#define DELTA_AGENT_H

#include "provider.h"
#include "util.h"

typedef struct {
    char  *provider;      /* anthropic | openai */
    char  *model;
    char  *api_key;
    char  *base_url;
    int    max_tokens;
    double temperature;
    char  *system_extra;
    char  *language;        /* pt-BR (padrao), pt, en, es, zh, ja */
    char  *thinking_level;
    char  *headers;       /* JSON com cabecalhos HTTP extras por provedor */
    int    supports_image; /* o modelo atual aceita imagem */
    int    check_updates;  /* 1 = procura versao nova no boot (config: updateCheck) */
    char  *subagent_model; /* modelo dos subagentes (config: subagentModel) */
    char  *project_dir;   /* pasta do projeto escolhida com /projects */
} Config;

/* ~/.deltaCore/config.json + variaveis de ambiente. Explica em `note` o que usou. */
int  config_load(Config *c, char *note, size_t note_len);
void config_free(Config *c);
int  config_save(const Config *c);

typedef struct {
    MsgList  msgs;
    Config  *cfg;
    char    *cwd;
    char    *system_prompt;
    long     total_in, total_out, total_cost_cache_read, total_cache_write;
    int      turn_count;
} Agent;

typedef struct {
    void (*on_text)(void *ud, const char *s, size_t n);
    void (*on_thinking)(void *ud, const char *s, size_t n);
    void (*on_tool_start)(void *ud, const char *name, const char *args_json);
    void (*on_tool_end)(void *ud, const char *name, const char *output, size_t out_len,
                        int is_error, double seconds);
    void (*on_turn_start)(void *ud);
    /* tentativa N de 3 falhou; a proxima comeca em 5 s */
    void (*on_retry)(void *ud, int tentativa, int total, const char *erro);
    /* chamado no intervalo entre passos (apos tools, antes do proximo
       thinking): o momento de injetar mensagens enfileiradas pelo usuario */
    void (*on_interval)(void *ud);
    int  (*should_abort)(void *ud);
    /* 1 = ha' mensagem do usuario na fila. Nesse caso o corte do stream NAO
       encerra o turno: a mensagem e' entregue e o modelo roda de novo. */
    int  (*has_steer)(void *ud);
    void *ud;
} AgentCB;

void agent_init(Agent *a, Config *cfg, const char *cwd);
/* Teto de ferramentas por turno (0 = sem limite). /forlimit usa isto. */
void agent_set_tool_limit(int n);
int  agent_tool_limit(void);
/* Troca a pasta de trabalho e remonta o prompt de sistema (usado no /projects). */
void agent_set_cwd(Agent *a, const char *cwd);
/* Remonta o prompt de sistema (lista de tools/skills pode ter mudado). */
void agent_rebuild_prompt(Agent *a);
void agent_free(Agent *a);
/* Executa um turno completo (com tool calls). 1 = ok. */
int  agent_run(Agent *a, const char *user_text, const AgentCB *cb, Str *err);
void agent_add_user(Agent *a, const char *text);
/* Registra a mensagem do usuario no historico E na sessao, sem rodar o turno. */
void agent_push_user(Agent *a, const char *text);
/* Igual, mas com imagens anexadas (caminhos em disco). */
void agent_push_user_imgs(Agent *a, const char *text, char **imgs, int n_imgs);
const char *agent_context_usage(const Agent *a, long *used, long *window);

/* Credenciais por provedor (~/.deltaCore/auth.json). */
char *auth_path(void);
char *auth_get_key(const char *provider);          /* alocado ou NULL */
int   auth_set_key(const char *provider, const char *key);
int   auth_remove_key(const char *provider);
int   auth_has_key(const char *provider);

/* Sessao em disco (~/.deltaCore/sessions/...). */
void session_append(const Agent *a, const char *type, const Msg *m);
void session_set_id(const char *id);
const char *session_id(void);
void session_start(const char *cwd);
/* Retoma a sessao mais recente de `cwd` (0 = nao havia). */
int  session_resume_last(const char *cwd, MsgList *msgs);

#endif /* DELTA_AGENT_H */
