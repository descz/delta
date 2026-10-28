/* modelsdev.h - catalogo de provedores/modelos do models.dev
 * Le o JSON em streaming e guarda apenas o que o Delta usa. */
#ifndef DELTA_MODELSDEV_H
#define DELTA_MODELSDEV_H

#include "util.h"

#define MODELSDEV_URL "https://models.dev/api.json"

/* Niveis de raciocinio, um bit por nivel (mesma ordem da TUI). */
#define THINK_OFF      (1u << 0)
#define THINK_MINIMAL  (1u << 1)
#define THINK_LOW      (1u << 2)
#define THINK_MEDIUM   (1u << 3)
#define THINK_HIGH     (1u << 4)
#define THINK_XHIGH    (1u << 5)
#define THINK_MAX      (1u << 6)
#define THINK_ALL      0x7Fu
#define CATALOG_TTL_SECONDS (7LL * 24 * 60 * 60)

typedef struct {
    char  *id;
    char  *name;
    char  *canonical;    /* canonical_model_id (usado so no parse) */
    int    ctx;          /* limit.context */
    int    out;          /* limit.output */
    double cost_in;      /* USD por milhao de tokens */
    double cost_out;
    double cost_cache_read;
    double cost_cache_write;
    int    reasoning;
    int    tool_call;
    int    primary;      /* canonical_model_id pertence a este provedor */
    /* o que o modelo aceita de raciocinio, segundo reasoning_options */
    int      think_kind;   /* 0 desconhecido, 1 effort, 2 toggle, 3 budget_tokens */
    unsigned think_mask;   /* bits dos niveis aceitos */
    int      budget_min, budget_max;
    int      image_in;     /* modalities.input contem "image" */
} MdModel;

typedef struct {
    char      *id;
    char      *name;
    char      *api;      /* base URL (pode ser vazio) */
    char      *env;      /* primeira variavel de ambiente */
    MdModel   *models;
    size_t     n_models;
    int        n_primary;
} MdProvider;

typedef struct {
    MdProvider *providers;
    size_t      n_providers;
    long long   fetched;   /* epoch da captura */
} Catalog;

void catalog_init(Catalog *c);
void catalog_free(Catalog *c);

/* Carrega do cache (~/.deltaCore/models.json); se ausente/expirado (ou
 * force_refresh), baixa do models.dev e regrava o cache.
 * Retorna 1 em sucesso. Se a rede falhar mas houver cache velho, usa o cache. */
int  catalog_load(Catalog *c, int force_refresh, Str *err);
/* Somente do cache local (sem rede) — usado na inicializacao. */
int  catalog_load_cached(Catalog *c);

/* Mantem so' os provedores aprovados por `keep` (libera o resto e os
   modelos deles). Retorna quantos sobraram. Usado para o /models trabalhar
   apenas com os provedores que o usuario conectou. */
int  catalog_keep(Catalog *c, int (*keep)(const char *id, void *ud), void *ud);
/* Total de modelos no catalogo (soma de todos os provedores). */
size_t catalog_models_total(const Catalog *c);

const MdProvider *catalog_provider(const Catalog *c, const char *id);
const MdModel    *catalog_model(const Catalog *c, const char *provider, const char *model);
/* Base URL do provedor: campo `api` do models.dev ou o mapa embutido. */
const char       *catalog_endpoint(const Catalog *c, const char *provider);
/* Variavel de ambiente que guarda a chave do provedor. */
const char       *catalog_env_var(const Catalog *c, const char *provider);
/* 1 = protocolo Anthropic; 0 = OpenAI-compativel. */
int               catalog_anthropic_style(const char *provider);
/* Modelo padrao sugerido para um provedor (o primeiro com tool_call). */
const MdModel    *catalog_default_model(const Catalog *c, const char *provider);

char *catalog_cache_path(void);

#endif /* DELTA_MODELSDEV_H */
