/* main.c - Delta Agent: agente de codificacao leve (TUI, derivado do Delta).
 * C11, sem dependencias externas. */
#include "agent.h"
#include "clipboard.h"
#include "modelsdev.h"
#include "term.h"
#include "tools.h"
#include "theme.h"
#include "ui.h"
#include "json.h"
#include "i18n.h"
#include "setup.h"
#include "update.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

#define VERSION DELTA_VERSION

/* ================================================================== */
/* Modelo da conversa                                                  */
/* ================================================================== */
typedef enum { B_USER, B_ASSISTANT, B_THINKING, B_TOOL, B_ERROR, B_INFO, B_RETRY } BlockKind;

typedef struct {
    BlockKind kind;
    Str       text;
    char     *tool_name;
    char     *tool_args;
    Str       tool_output;
    int       tool_state;     /* 0 pendente, 1 ok, 2 erro */
    double    tool_secs;
    uint64_t  tool_t0;        /* inicio da execucao (ms) */
    Str       meta;           /* rodape: modelo/provedor/thinking/tokens/tempo/custo */
    int       queued;         /* 1 = na fila (dim), 2 = devolvida ao editor */
    int       tr_y;           /* linha onde o bloco comeca no transcript */
} Block;

typedef struct {
    Config   cfg;
    Agent    agent;
    Theme    theme;
    Screen   screen;
    Screen   tr;
    int      tr_dirty;

    Block   *blocks;
    size_t   n_blocks;

    Str      plan;              /* ~/.deltaCore/plan.md (agent op=plan) */

    Str      input;
    size_t   cursor;
    Str      paste_text;        /* texto real por tras do {paste N caracteres} */
    uint64_t ctrl_key_ms;       /* quando o Ctrl sozinho foi pressionado */
    uint64_t last_clip_ms;      /* ultima colagem tratada (EV_CTRL v ou hotkey) */
    Str      ultimo_colado;     /* ultimo texto colado (evita colar duas vezes) */
    uint64_t ultimo_colado_ms;
    Str      ultimo_colado_img; /* ultima imagem colada (nome = hash do conteudo) */
    uint64_t ultimo_colado_img_ms;
    char    *imgs[8];           /* imagens anexadas nesta mensagem */
    int      n_imgs;

    int      ext_mode;          /* painel de extensoes aberto */
    int      ext_sel;

    int      working;
    Str      work_msg;

    int      scroll;
    Str      status;
    uint64_t status_until;

    /* vigia do stream: quando o provedor aceita a conexao e para de mandar
       bytes (fila do provedor, rede caida), avisa que esta' esperando */
    uint64_t last_activity_ms;
    int      stall_warned;

    int      abort_requested;
    int      quit;
    char    *theme_name;

    /* menu de comandos / seletor de modelos */
    int      menu_visible;
    int      menu_sel;
    int      menu_scroll;
    int      menu_suppressed;
    int      picker;            /* 0 nenhum, 1 modelo, 2 login, 3 chave, 4 thinking, 5 logout */
    int      menu_y0, menu_rows; /* geometria na tela (para o mouse) */
    Str      picker_filter;

    /* catalogo models.dev */
    Catalog  catalog;
    int      catalog_loaded;
    int      catalog_filtrado;   /* /models deixou so' os provedores conectados */
    long     catalog_models;
    char    *login_provider;    /* provedor escolhido no /login */
    int      tr_width;          /* largura com que o transcript foi montado */
    size_t   tr_done;           /* quantos blocos ja' estao no transcript */
    int      tr_end_y;          /* linha onde o ultimo bloco terminou */
    int      tr_fast;           /* 1 = so' repintar a partir de tr_fast_from */
    size_t   tr_fast_from;

    /* selecao no transcript: arrasto do mouse; Ctrl+C copia, clique limpa */
    int      sel_active;
    int      sel_ax, sel_ay;    /* ancora (onde o arrasto comecou) */
    int      sel_hx, sel_hy;    /* cabeca (onde o mouse esta) */
    int      tr_start;          /* linha do transcript que aparece na linha 0 da tela */
    int      tr_view;           /* linhas visiveis do transcript */
    int      tr_total_prev;     /* linhas do transcript no frame anterior (ancora do scroll) */
    int      tr_prev_width;     /* largura daquele frame */
} App;

static App app;

/* ================================================================== */
/* Comandos disponiveis (nome, descricao, dica de argumento)           */
/* ================================================================== */
typedef struct {
    const char *name;
    const char *desc;
    const char *hint;
    int         takes_arg;
} Cmd;

/* chave i18n de cada comando, na mesma ordem da tabela */
static const char *const CMD_KEYS[] = {
    "cmd.help", "cmd.login", "cmd.logout", "cmd.models", "cmd.thinking",
    "cmd.verbosity", "cmd.projects", "cmd.terminal", "cmd.status", "cmd.session", "cmd.new",
    "cmd.clear", "cmd.skills", "cmd.extensions", "cmd.translator",
    "cmd.reload", "cmd.quit", "cmd.subagentmodel", "cmd.deltagroup", "cmd.undo", "cmd.update"
};


static const Cmd COMMANDS[] = {
    { "help",      "Mostra os comandos e atalhos",                    NULL,                 0 },
    { "login",     "Escolhe um provedor e salva a chave de API",     NULL,                 0 },
    { "logout",    "Remove a chave do provedor atual",                NULL,                 0 },
    { "models",    "Todos os modelos de todos os provedores",         "<filtro>",           1 },
    { "thinking",  "Nível de raciocínio",                             "off|low|medium|high", 1 },
    { "verbosity", "Quanta prosa na resposta (o raciocínio não muda)",  "low|normal|high",    1 },
    { "projects",  "Define a pasta onde o Delta trabalha",             "<pasta>",           1 },
    { "terminal",  "Executa um comando no shell e mostra a saida",     "<comando>",          1 },
    { "status",    "Mostra a configuração e o uso",                    NULL,                 0 },
    { "session",   "Informações da sessão",                           NULL,                 0 },
    { "new",       "Começa uma sessão nova",                          NULL,                 0 },
    { "clear",     "Limpa a conversa",                                NULL,                 0 },
    { "skills",    "Lista as skills disponiveis",                      NULL,                 0 },
    { "extensions","Painel de extensões (ou /extensions <pedido> cria)", NULL,               1 },
    { "translator","Troca o idioma da interface",                      "pt-BR|pt|en|es|zh|ja", 1 },
    { "reload",    "Recarrega config e tema do disco",                NULL,                 0 },
    { "quit",      "Sai do Delta Agent",                              NULL,                 0 },
    { "subagentmodel", "Modelo em que os subagentes rodam (ele pergunta antes)", NULL,       0 },
    { "deltagroup", "Grupo do Delta no Telegram · convite e avisos",    NULL,                 0 },
    { "undo",      "Restaura o conteúdo anterior de um arquivo",       "[arquivo]",          1 },
    { "update",    "Instala a versão mais nova do GitHub",              "[--check]",          1 },
};
#define N_COMMANDS ((int)(sizeof COMMANDS / sizeof COMMANDS[0]))

static const char *cmd_desc(const Cmd *c)
{
    for (size_t i = 0; COMMANDS[i].name; i++)
        if (c == &COMMANDS[i])
            return i < sizeof CMD_KEYS / sizeof CMD_KEYS[0] ? tr(CMD_KEYS[i]) : c->desc;
    return c->desc;
}


/* Itens do menu (reaproveitados entre frames, sem alocar por frame) */
typedef struct {
    const char       *name;
    const char       *desc;      /* estatico (comandos) */
    const char       *value;     /* valor a gravar (ex.: nivel de thinking) */
    const MdModel    *model;
    const MdProvider *provider;
    int               current;
    int               kind;      /* 0 comando, 1 modelo, 2 provedor, 3 nivel */
} Item;

static Item  *g_items;
static size_t g_items_cap, g_items_n;

/* Cache da lista do seletor: refiltrar 9 mil modelos a cada frame custava
   ~12 ms (seta/roda pareciam travadas). Invalidado pelo filtro/owner. */
static int  g_items_owner = 0;      /* 'm' modelos, 'p' provedores, 0 nada */
static char g_items_key[160];

static void items_reset(void) { g_items_n = 0; g_items_owner = 0; }

static Item *items_add(void)
{
    if (g_items_n + 1 > g_items_cap) {
        g_items_cap = g_items_cap ? g_items_cap * 2 : 64;
        g_items = (Item *)realloc(g_items, g_items_cap * sizeof(Item));
    }
    Item *it = &g_items[g_items_n++];
    memset(it, 0, sizeof(*it));
    return it;
}

static int icontains(const char *hay, const char *needle)
{
    if (!needle || !*needle) return 1;
    if (!hay) return 0;
    size_t nl = strlen(needle);
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < nl && p[i]) {
            char a = p[i], b = needle[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
            i++;
        }
        if (i == nl) return 1;
    }
    return 0;
}

static int istarts_ci(const char *s, const char *prefix)
{
    if (!prefix || !*prefix) return 1;
    while (*prefix) {
        if (!*s) return 0;
        char a = *s, b = *prefix;
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b) return 0;
        s++; prefix++;
    }
    return 1;
}

/* Comandos cujo nome comeca com o filtro. */
static int fill_commands(const char *filter)
{
    items_reset();
    for (int i = 0; i < N_COMMANDS; i++) {
        if (!istarts_ci(COMMANDS[i].name, filter)) continue;
        Item *it = items_add();
        it->name = COMMANDS[i].name;
        it->desc = cmd_desc(&COMMANDS[i]);
    }
    return (int)g_items_n;
}

/* ================================================================== */
/* Catalogo models.dev (carregado sob demanda)                         */
/* ================================================================== */
/* ================================================================== */
/* Plano (agent op=plan grava ~/.deltaCore/plan.md)                      */
/* ================================================================== */
static void plan_refresh(void)
{
    char *home = home_get();
    char *p = path_join(delta_home(), "plan.md");
    size_t n = 0;
    char *txt = file_read(p, &n);
    free(p);
    free(home);

    if (txt) {
        if (!app.plan.data || app.plan.len != n || memcmp(app.plan.data, txt, n) != 0) {
            str_clear(&app.plan);
            str_add(&app.plan, txt, n);
            app.tr_dirty = 1;
        }
        free(txt);
    } else if (app.plan.len) {
        str_clear(&app.plan);
        app.tr_dirty = 1;
    }
}

/* Conta "- [x]" / "- [ ]" no plano. */
static void plan_counts(int *pend, int *done)
{
    int p = 0, d = 0;
    const char *c = app.plan.data ? app.plan.data : "";
    while ((c = strstr(c, "- [")) != NULL) {
        if (c[4] == ']') {
            if (c[3] == 'x' || c[3] == 'X') d++;
            else p++;
        }
        c += 3;
    }
    if (pend) *pend = p;
    if (done) *done = d;
}

/* Desenha o painel do plano no topo da conversa. Retorna linhas usadas. */
static int plan_draw(Screen *s, int y, int width)
{
    if (app.plan.len == 0) return 0;
    int pend = 0, done = 0, linhas = 0;
    plan_counts(&pend, &done);

    Style st_t = { tcolor_hex(theme_tok(&app.theme, TK_ACCENT)), COL_DEFAULT, ATTR_BOLD };
    Style st_i = { tcolor_hex(theme_tok(&app.theme, TK_TEXT)), COL_DEFAULT, 0 };
    Style st_d = { tcolor_hex(theme_tok(&app.theme, TK_DIM)), COL_DEFAULT, ATTR_STRIKE };

    char hdr[96];
    snprintf(hdr, sizeof hdr, "Plano  %d/%d", done, done + pend);
    screen_text(s, 2, y, hdr, strlen(hdr), st_t, width - 4);
    y++;
    linhas++;

    const char *p = app.plan.data;
    while (p && *p && linhas <= 8) {
        const char *eol = strchr(p, '\n');
        size_t ll = eol ? (size_t)(eol - p) : strlen(p);
        while (ll && (p[ll - 1] == '\r' || p[ll - 1] == ' ')) ll--;
        if (ll) {
            int feito = (ll > 4 && p[0] == '-' && p[1] == ' ' && p[2] == '[' && p[3] == ']');
            screen_text(s, 2, y, p, ll, feito ? st_d : st_i, width - 4);
            y++;
            linhas++;
        }
        p = eol ? eol + 1 : NULL;
    }
    if (p && *p) {
        screen_text(s, 2, y, "...", 3, st_i, 3);
        y++;
        linhas++;
    }
    return linhas;
}

static void ext_panel_draw(Screen *s, int ey, int cols);
static int  fila_count(void);
static void render(void);
static void sel_clear(void);
static void render_selection(void);
static int  sel_has_text(void);
static void copy_selection(void);
static void set_status(const char *fmt, ...);
static void notify(const char *fmt, ...);
static void handle_event(const Event *ev);
static char *paste_dir(void);
static void expand_paste(Str *text);
static void inserir_colagem(const Clip *c);
static void input_insert_text(const char *t, size_t n);
static void open_picker(int kind);
/* Rola a conversa: +1 = roda para cima (conteudo mais antigo). */
static void scroll_wheel(int shift);

#define PASTE_MIN_EVENTOS 8      /* lote a partir disso e colagem, nao digitacao */
#define PASTE_MIN_CHARS   200    /* a partir disso vira {paste N caracteres} */
static long catalog_model_count(void);

/* O /models deixa o catalogo so' com os provedores conectados (lista curta).
   Quem precisa resolver um provedor/modelo especifico chama isto: recarrega a
   lista completa do cache, sem rede. */
static void catalog_ensure_full(void)
{
    if (!app.catalog_filtrado) return;
    if (catalog_load_cached(&app.catalog)) app.catalog_loaded = 1;
    app.catalog_filtrado = 0;
}

static int catalog_ensure(void)
{
    if (app.catalog_loaded) return 1;
    notify("baixando catalogo do models.dev...");
    render();

    Str err; str_init(&err);
    if (!catalog_load(&app.catalog, 0, &err)) {
        notify("catalogo indisponivel: %s", err.len ? err.data : "erro de rede");
        str_free(&err);
        render();
        return 0;
    }
    str_free(&err);
    app.catalog_loaded = 1;
    notify("%zu provedores e %ld modelos do models.dev",
               app.catalog.n_providers, catalog_model_count());
    return 1;
}

static long catalog_model_count(void)
{
    long n = 0;
    for (size_t i = 0; i < app.catalog.n_providers; i++)
        n += (long)app.catalog.providers[i].n_models;
    return n;
}

/* Descricao de um item, formatada na hora (so para o que aparece). */
static const char *item_desc(const Item *it, char *buf, size_t n)
{
    if (it->model) {
        const MdModel *m = it->model;
        /* preco total por milhao de tokens: entrada + saida + cache */
        double total = m->cost_in + m->cost_out + m->cost_cache_read + m->cost_cache_write;
        char preco[64];
        if (total > 0) snprintf(preco, sizeof preco, "$%.2f/M", total);
        else           snprintf(preco, sizeof preco, "preco n/d");
        snprintf(buf, n, "%s%s", preco, it->current ? " · atual" : "");
        return buf;
    }
    if (it->kind == 2 && it->provider) {
        /* lista do /login, como no Delta:
           Nome [API key] ✓ env: VAR  |  ✓ stored  |  • not configured */
        const MdProvider *p = it->provider;
        const char *env = catalog_env_var(&app.catalog, p->id);
        char *de_env = (env && *env) ? env_get(env) : NULL;
        int tem_env = de_env && *de_env;
        int guardada = auth_has_key(p->id);
        int ativo = app.cfg.provider && str_eq(app.cfg.provider, p->id);
        free(de_env);

        snprintf(buf, n, "[API key] %s%s%s%s",
                 tem_env ? "\xe2\x9c\x93 env: " : (guardada ? "\xe2\x9c\x93 stored" : "\xe2\x80\xa2 not configured"),
                 tem_env && env ? env : "",
                 ativo ? "  \xe2\x86\x90 ativo" : "",
                 "");
        return buf;
    }
    return it->desc ? it->desc : "";
}

/* Provedor pronto para uso: chave no ambiente ou salva no auth.json. */
static int provider_conectado(const MdProvider *p)
{
    if (!p || !p->id) return 0;
    const char *env = catalog_env_var(&app.catalog, p->id);
    if (env && *env) {
        char *v = env_get(env);
        int tem = v && *v;
        free(v);
        if (tem) return 1;
    }
    return auth_has_key(p->id);
}

/* usado por catalog_keep (modelsdev.c nao conhece auth): mantem so' quem
   tem chave salva ou no ambiente */
static int keep_provider_conectado(const char *id, void *ud)
{
    (void)ud;
    return provider_conectado(catalog_provider(&app.catalog, id));
}

/* Modelos que o usuario realmente pode usar (provedores com chave). */
static long modelos_conectados(void)
{
    long n = 0;
    for (size_t i = 0; i < app.catalog.n_providers; i++)
        if (provider_conectado(&app.catalog.providers[i]))
            n += (long)app.catalog.providers[i].n_models;
    return n;
}

static int catalog_provedores_conectados(void)
{
    int n = 0;
    for (size_t i = 0; i < app.catalog.n_providers; i++)
        if (provider_conectado(&app.catalog.providers[i])) n++;
    return n;
}

/* Modelos dos provedores JA CONECTADOS; o atual vem primeiro. */
static long fill_models(const char *filter)
{
    items_reset();
    Catalog *c = &app.catalog;
    long total = 0;

    /* o modelo atual so entra se o provedor dele estiver conectado */
    if (app.cfg.model && app.cfg.provider) {
        const MdProvider *cp = catalog_provider(c, app.cfg.provider);
        const MdModel *cur = cp ? catalog_model(c, app.cfg.provider, app.cfg.model) : NULL;
        if (cur && cp && provider_conectado(cp) &&
            (icontains(cur->id, filter) || icontains(app.cfg.provider, filter))) {
            Item *it = items_add();
            it->name = cur->id;
            it->model = cur;
            it->provider = cp;
            it->kind = 1;
            it->current = 1;
            total++;
        }
    }

    for (size_t i = 0; i < c->n_providers; i++) {
        const MdProvider *p = &c->providers[i];
        if (!provider_conectado(p)) continue;   /* so os configurados */
        int ativo = app.cfg.provider && str_eq(app.cfg.provider, p->id);
        int prov_match = icontains(p->id, filter) || icontains(p->name, filter);
        for (size_t k = 0; k < p->n_models; k++) {
            const MdModel *m = &p->models[k];
            if (!prov_match && !icontains(m->id, filter)) continue;
            if (ativo && app.cfg.model && str_eq(m->id, app.cfg.model))
                continue;                     /* ja foi posto no topo */
            Item *it = items_add();
            it->name = m->id;
            it->model = m;
            it->provider = p;
            it->kind = 1;
            total++;
        }
    }
    return total;
}

/* Provedores para o /login (todos) ou /logout (so os conectados). */
static int fill_providers(const char *filter)
{
    items_reset();
    Catalog *c = &app.catalog;
    for (size_t i = 0; i < c->n_providers; i++) {
        const MdProvider *p = &c->providers[i];
        if (app.picker == 5 && !provider_conectado(p)) continue;
        if (!icontains(p->id, filter) && !icontains(p->name, filter)) continue;
        Item *it = items_add();
        it->name = p->name;
        it->provider = p;
        it->kind = 2;
        it->current = app.cfg.provider && str_eq(app.cfg.provider, p->id);
    }
    return (int)g_items_n;
}

/* Niveis de raciocinio (mesmos nomes dos tokens de tema do Delta). */
static const struct { const char *id; const char *desc; } THINKING_LEVELS[] = {
    { "off",     "Desliga o raciocinio" },
    { "minimal", "Pensamento minimo" },
    { "low",     "Pensamento curto" },
    { "medium",  "Equilibrado" },
    { "high",    "Pensamento longo" },
    { "xhigh",   "Pensamento muito longo" },
    { "max",     "Sem limite" },
};
#define N_THINKING ((int)(sizeof THINKING_LEVELS / sizeof THINKING_LEVELS[0]))

/* /verbosity: quanta prosa em volta do resultado. Nao toca no raciocinio
   (/thinking) nem no trabalho — so' no texto que vem junto. */
static const struct { const char *id; const char *desc; } VERBOSITY_LEVELS[] = {
    { "low",    "Menos prosa: 1-2 frases (codigo e trabalho inteiros)" },
    { "normal", "Estilo atual: paragrafos de 2-3 linhas" },
    { "high",   "Explica a escolha, as alternativas e as ressalvas" },
};
#define N_VERBOSITY ((int)(sizeof VERBOSITY_LEVELS / sizeof VERBOSITY_LEVELS[0]))

/* Niveis de thinking que O MODELO ATUAL aceita (reasoning_options do models.dev). */
static int fill_thinking(const char *filter)
{
    items_reset();
    const MdModel *m = NULL;
    if (app.catalog_loaded && app.cfg.provider && app.cfg.model)
        m = catalog_model(&app.catalog, app.cfg.provider, app.cfg.model);
    unsigned mask = (m && m->think_mask) ? m->think_mask : THINK_ALL;

    for (int i = 0; i < N_THINKING; i++) {
        if (!(mask & (1u << i))) continue;
        if (!icontains(THINKING_LEVELS[i].id, filter)) continue;
        Item *it = items_add();
        it->name = THINKING_LEVELS[i].id;
        it->desc = THINKING_LEVELS[i].desc;
        it->value = THINKING_LEVELS[i].id;
        it->kind = 3;
        it->current = app.cfg.thinking_level && str_eq(app.cfg.thinking_level, THINKING_LEVELS[i].id);
    }
    return (int)g_items_n;
}

/* Decide qual lista mostrar (ou nenhuma) e devolve o total. */
static int menu_prepare(void)
{
    /* seletor fechado: a proxima abertura remonta a lista (modelo/provedor
       atual pode ter mudado) */
    if (!app.picker) g_items_owner = 0;
    if (app.picker == 1 || app.picker == 6) {
        /* 1 = modelo do chat, 6 = modelo dos subagentes: mesma lista */
        const char *f = app.picker_filter.data ? app.picker_filter.data : "";
        if (g_items_owner == 'm' && str_eq(g_items_key, f)) return (int)g_items_n;
        long n = fill_models(f);
        g_items_owner = 'm';
        snprintf(g_items_key, sizeof g_items_key, "%s", f);
        return (int)n;
    }
    if (app.picker == 2 || app.picker == 5) {
        /* o /models deixa o catalogo so' com os conectados; para escolher um
           provedor novo (ou desconectar) a lista completa volta do cache */
        catalog_ensure_full();
        const char *f = app.picker_filter.data ? app.picker_filter.data : "";
        if (g_items_owner == 'p' && str_eq(g_items_key, f)) return (int)g_items_n;
        int n = fill_providers(f);
        g_items_owner = 'p';
        snprintf(g_items_key, sizeof g_items_key, "%s", f);
        return n;
    }
    if (app.picker == 4) return fill_thinking(app.picker_filter.data ? app.picker_filter.data : "");
    if (app.picker == 3) return 0;          /* digitando a chave */
    if (app.menu_suppressed) return 0;
    if (app.input.len < 1 || app.input.data[0] != '/') return 0;
    if (strchr(app.input.data, ' ')) return 0;      /* ja tem argumento */
    if (app.input.len > 32) return 0;
    return fill_commands(app.input.data + 1);
}

static const char *menu_title(void)
{
    if (app.picker == 1) {
        static char buf[200];
        snprintf(buf, sizeof buf, "modelos · %d de %d provedores conectados",
                 (int)g_items_n, catalog_provedores_conectados());
        return buf;
    }
    if (app.picker == 2) return "Select provider to configure:";
    if (app.picker == 4) {
        static char buf[260];
        const MdModel *m = app.catalog_loaded
            ? catalog_model(&app.catalog, app.cfg.provider, app.cfg.model) : NULL;
        char extra[140] = "";
        if (m && m->think_kind == 1) {
            char vals[110] = "";
            unsigned mask = m->think_mask;
            for (int i = 0; i < N_THINKING; i++) {
                if (!(mask & (1u << i)) || i == 0) continue;   /* off nao conta */
                size_t l = strlen(vals);
                snprintf(vals + l, sizeof vals - l, "%s%s", l ? ", " : "",
                         THINKING_LEVELS[i].id);
            }
            snprintf(extra, sizeof extra, " · o modelo aceita: %s", *vals ? vals : "?");
        } else if (m && m->think_kind == 2) {
            snprintf(extra, sizeof extra, " · o modelo so liga/desliga");
        } else if (m && m->think_kind == 3) {
            if (m->budget_max > 0)
                snprintf(extra, sizeof extra, " · orcamento de %d a %d tokens",
                         m->budget_min, m->budget_max);
            else
                snprintf(extra, sizeof extra, " · orcamento de tokens");
        } else if (m && m->reasoning) {
            snprintf(extra, sizeof extra, " · o modelo anuncia reasoning");
        }
        snprintf(buf, sizeof buf, "thinking · %s%s",
                 app.cfg.model ? app.cfg.model : "(sem modelo)", extra);
        return buf;
    }
    if (app.picker == 5) return "Select provider to disconnect:";
    if (app.picker == 6) {
        static char buf[220];
        snprintf(buf, sizeof buf, "modelo dos subagentes · Enter confirma, Esc cancela%s%s",
                 app.cfg.subagent_model && *app.cfg.subagent_model ? " · atual: " : "",
                 app.cfg.subagent_model ? app.cfg.subagent_model : "");
        return buf;
    }
    if (app.picker == 3) {
        static char buf[160];
        const MdProvider *p = catalog_provider(&app.catalog, app.login_provider);
        const char *env = catalog_env_var(&app.catalog, app.login_provider);
        snprintf(buf, sizeof buf, "chave de API do %s%s%s · Enter salva, Esc cancela",
                 p ? p->name : app.login_provider,
                 env && *env ? " · " : "", env && *env ? env : "");
        return buf;
    }
    return NULL;
}

/* ================================================================== */
/* Temas: "mono" e o padrao (branco e cinzas); dark/light vem do Delta    */
/* ================================================================== */
static int load_theme_named(const char *name, Theme *out)
{
    if (!name || !*name || str_eq(name, "mono")) {
        theme_load_builtin_mono(out);
        return 1;
    }
    char *path = theme_find_path(name);
    int ok = path ? theme_load(out, path) : 0;
    free(path);
    if (ok) return 1;
    if (str_eq(name, "dark")) { theme_load_builtin_dark(out); return 1; }
    theme_load_builtin_mono(out);
    return 0;   /* nao encontrado: fica no mono */
}

/* ================================================================== */
/* Blocos                                                              */
/* ================================================================== */
static void fmt_tok(char *b, size_t n, long v)
{
    if (v >= 1000) snprintf(b, n, "%.1fk", (double)v / 1000.0);
    else snprintf(b, n, "%ld", v);
}

static void fmt_dur(char *b, size_t n, double s)
{
    if (s < 0) s = 0;
    if (s < 1) snprintf(b, n, "%dms", (int)(s * 1000.0 + 0.5));
    else if (s < 60) snprintf(b, n, "%.1fs", s);
    else snprintf(b, n, "%dm%02ds", (int)(s / 60), (int)s % 60);
}

static void tr_touch(size_t i);

/* Rodape da resposta final: modelo, provedor, thinking, tokens, tempo e custo. */
static void turn_meta_attach(size_t from, uint64_t t0, long in0, long out0)
{
    Block *last = NULL;
    for (size_t i = from; i < app.n_blocks; i++)
        if (app.blocks[i].kind == B_ASSISTANT && app.blocks[i].text.len) last = &app.blocks[i];
    if (!last) return;

    long din = app.agent.total_in - in0;
    long dout = app.agent.total_out - out0;
    if (din < 0) din = 0;
    if (dout < 0) dout = 0;

    char tin[24], tout[24], dur[24];
    fmt_tok(tin, sizeof tin, din);
    fmt_tok(tout, sizeof tout, dout);
    fmt_dur(dur, sizeof dur, (double)(now_ms() - t0) / 1000.0);

    const MdModel *m = catalog_model(&app.catalog, app.cfg.provider, app.cfg.model);
    char custo[48];
    if (m && (m->cost_in > 0 || m->cost_out > 0)) {
        double c = (double)din * m->cost_in / 1e6 + (double)dout * m->cost_out / 1e6;
        snprintf(custo, sizeof custo, "$%.4f", c);
    } else {
        snprintf(custo, sizeof custo, "custo n/d");
    }

    str_clear(&last->meta);
    str_addf(&last->meta, "%s · %s · thinking %s · %s in / %s out · %s · %s",
             app.cfg.model ? app.cfg.model : "no-model",
             app.cfg.provider ? app.cfg.provider : "?",
             app.cfg.thinking_level ? app.cfg.thinking_level : "off",
             tin, tout, dur, custo);
    tr_touch((size_t)(last - app.blocks));
}

static Block *block_add(BlockKind kind)
{
    app.blocks = (Block *)realloc(app.blocks, (app.n_blocks + 1) * sizeof(Block));
    Block *b = &app.blocks[app.n_blocks++];
    memset(b, 0, sizeof(*b));
    b->kind = kind;
    b->tr_y = 0;
    str_init(&b->text);
    str_init(&b->tool_output);
    str_init(&b->meta);
    /* bloco novo = repintar so' a partir dele (nao a conversa inteira) */
    app.tr_fast = 1;
    app.tr_fast_from = app.n_blocks - 1;
    return b;
}

/* Marca que o bloco `i` mudou: repinta dali para baixo. */
static void tr_touch(size_t i)
{
    app.tr_fast = 1;
    if (!app.tr_dirty) app.tr_fast_from = i;
    else if (i < app.tr_fast_from) app.tr_fast_from = i;
}

static void block_free(Block *b)
{
    str_free(&b->text);
    str_free(&b->tool_output);
    str_free(&b->meta);
    free(b->tool_name);
    free(b->tool_args);
}

static void blocks_clear(void)
{
    for (size_t i = 0; i < app.n_blocks; i++) block_free(&app.blocks[i]);
    free(app.blocks);
    app.blocks = NULL;
    app.n_blocks = 0;
    app.tr_dirty = 1;
    app.scroll = 0;
}

/* Mensagens de estado: NAO aparecem no rodape (nao ha mais linha de
   notificacao). Ficam no app.status para quem precisar (titulos) e as
   mensagens que importam vao para o transcript como linha discreta. */
static void set_status(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    str_clear(&app.status);
    char buf[512];
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    str_adds(&app.status, buf);
    app.status_until = 0;
}

/* Aviso visivel: entra no transcript (dim) em vez do rodape. */
static void notify(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[512];
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (*buf) {
        Block *b = block_add(B_INFO);
        str_adds(&b->text, buf);
    }
    set_status("%s", buf);
}

/* Aviso passageiro: aparece so' na borda do editor por alguns segundos.
   Copia/selecao usam isto para nao sujar a conversa com um bloco a cada
   Ctrl+X (que era o que acontecia com notify()). */
static void notify_status(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[512];
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    str_clear(&app.status);
    if (*buf) str_adds(&app.status, buf);
    app.status_until = now_ms() + 3000;
}

/* ================================================================== */
/* Modelo dos subagentes: o Delta PERGUNTA antes de criar              */
/* ================================================================== */
/* Chamado de dentro da tool de subagente (no meio do turno). Abre o
   seletor de modelos, roda um mini laco de eventos ate' a escolha (ou Esc)
   e devolve. Em -p/--replay nao ha' terminal: usa o padrao ja' salvo. */
static int  g_tui_ativa = 0;
static char g_subagent_escolhido[128];

static int ask_subagent_model(void *ud, char *out, size_t n)
{
    (void)ud;
    if (!g_tui_ativa) {
        if (app.cfg.subagent_model && *app.cfg.subagent_model) {
            snprintf(out, n, "%s", app.cfg.subagent_model);
            return 1;
        }
        return 0;
    }

    g_subagent_escolhido[0] = 0;
    app.menu_suppressed = 0;
    open_picker(6);
    if (app.cfg.subagent_model && *app.cfg.subagent_model) {
        for (size_t i = 0; i < g_items_n; i++) {
            Item *it = &g_items[i];
            if (it->model && str_eq(it->model->id, app.cfg.subagent_model)) {
                app.menu_sel = (int)i;
                break;
            }
        }
    }

    while (app.picker == 6 && !app.quit) {
        Event ev;
        render();
        if (!term_read(&ev, 250)) continue;
        handle_event(&ev);
    }
    if (app.picker == 6) app.picker = 0;
    app.menu_suppressed = 0;
    app.tr_dirty = 1;
    render();

    if (!g_subagent_escolhido[0]) {
        notify_status("subagente cancelado: nenhum modelo escolhido");
        return 0;
    }
    snprintf(out, n, "%s", g_subagent_escolhido);
    return 1;
}

/* Indice do quadro do spinner. Vem do RELOGIO, nao de um contador de frames:
   durante o turno o laco principal esta' bloqueado dentro do agente, entao o
   contador so' andava quando o usuario mexia no teclado e o "Working" ficava
   congelado. */
static int spin_from_clock(void)
{
    return (int)((now_ms() / 90u) % 10u);
}

/* Aviso de versao nova na inicializacao. Roda uma vez por sessao, no primeiro
   frame ocioso (a tela ja' esta' desenhada), e respeita o cache de 24 h: a
   primeira consulta do dia custa uma requisicao, as outras sao instantaneas. */
static int g_update_checked = 0;

static void update_boot_notice(void)
{
    if (!app.cfg.check_updates) return;
    char *aviso = update_notice(1);
    if (!aviso) return;
    Block *b = block_add(B_INFO);
    str_adds(&b->text, aviso);
    free(aviso);
    notify_status("versao nova disponivel \u00b7 /update instala");
    app.tr_dirty = 1;
    render();
}

/* Desenha UM bloco a partir de y; devolve a linha seguinte. */
static int draw_block(Screen *s, int y, int width, Block *b)
{
        switch (b->kind) {
            case B_USER:
                if (b->queued) {
                    /* mensagem digitada durante o turno: fica visivel no chat,
                       apagada, ate' o agente le-la (ai' vira mensagem normal) */
                    Style st_q = { tcolor_hex(theme_tok(&app.theme, TK_MUTED)), COL_DEFAULT, ATTR_DIM };
                    y += screen_para(s, 2, y, width - 4, b->text.data ? b->text.data : "",
                                     b->text.len, st_q);
                    const char *tag = b->queued == 2 ? "(devolvida ao editor)"
                                     : b->queued == 3 ? "(n\u00e3o enviada: turno interrompido)"
                                     : "\u0428 na fila \u00b7 o agente le no pr\u00f3ximo passo";
                    screen_text(s, 2, y, tag, strlen(tag), st_q, width - 4);
                    y += 1;
                    y += 1;
                    break;
                }
                y += ui_user_message(s, y, width, &app.theme, b->text.data ? b->text.data : "", b->text.len);
                y += 1;
                break;
            case B_ASSISTANT:
                y += ui_assistant_message(s, y, width, &app.theme, b->text.data ? b->text.data : "",
                                          b->text.len, 0);
                if (b->meta.len) {
                    /* uma linha em branco entre a resposta e os dados do turno */
                    Style st_meta = { tcolor_hex(theme_tok(&app.theme, TK_DIM)), COL_DEFAULT, 0 };
                    y += 1;
                    screen_text(s, 2, y, b->meta.data, b->meta.len, st_meta, width - 4);
                    y += 1;
                }
                y += 1;
                break;
            case B_RETRY: {
                Style st = { tcolor_hex(theme_tok(&app.theme, TK_DIM)), COL_DEFAULT, ATTR_ITALIC };
                y += screen_para(s, 2, y, width - 4, b->text.data ? b->text.data : "", b->text.len, st);
                y += 1;
                break;
            }
            case B_THINKING: {
                Style st = { tcolor_hex(theme_tok(&app.theme, TK_THINKING_TEXT)), COL_DEFAULT, ATTR_ITALIC };
                y += screen_para(s, 2, y, width - 4, b->text.data ? b->text.data : "", b->text.len, st);
                y += 1;
                break;
            }
            case B_TOOL: {
                /* titulo: nome + args resumidos (no /terminal, o comando cru) */
                int eh_terminal = b->tool_name && str_eq(b->tool_name, "terminal");
                Str args; str_init(&args);
                if (eh_terminal) {
                    str_adds(&args, "$ ");
                    str_adds(&args, b->tool_args ? b->tool_args : "");
                } else {
                JValue *j = json_parse_cstr(b->tool_args ? b->tool_args : "{}");
                if (j && j->kind == J_OBJ) {
                    for (size_t k = 0; k < j->count; k++) {
                        if (k) str_addc(&args, ' ');
                        str_addf(&args, "%s=", j->pairs[k].key);
                        JValue *v = j->pairs[k].val;
                        if (v->kind == J_STR) {
                            str_addc(&args, '"');
                            size_t max = v->str ? strlen(v->str) : 0;
                            if (max > 60) max = 60;
                            if (v->str) str_add(&args, v->str, max);
                            if (v->str && strlen(v->str) > 60) str_adds(&args, "...");
                            str_addc(&args, '"');
                        } else {
                            str_adds(&args, "…");
                        }
                    }
                } else {
                    str_adds(&args, b->tool_args ? b->tool_args : "");
                }
                json_free(j);
                }
                {
                    /* tempo da tool: ao vivo enquanto roda, total depois,
                       desenhado dentro da caixa (alinhado a direita) */
                    double secs = b->tool_state == 0
                                      ? (double)(now_ms() - b->tool_t0) / 1000.0
                                      : b->tool_secs;
                    char tmp[32];
                    fmt_dur(tmp, sizeof tmp, secs);
                    y += ui_tool_box(s, y, width, &app.theme, b->tool_state,
                                     b->tool_name ? b->tool_name : "",
                                     args.data, b->tool_output.data, b->tool_output.len,
                                     eh_terminal ? 24 : 12, tmp);
                }
                str_free(&args);
                y += 1;
                break;
            }
            case B_ERROR: {
                Style st = { tcolor_hex(theme_tok(&app.theme, TK_ERROR)), COL_DEFAULT, ATTR_ITALIC };
                y += screen_para(s, 2, y, width - 4, b->text.data ? b->text.data : "", b->text.len, st);
                y += 1;
                break;
            }
            case B_INFO: {
                Style st = { tcolor_hex(theme_tok(&app.theme, TK_DIM)), COL_DEFAULT, 0 };
                y += screen_para(s, 2, y, width - 4, b->text.data ? b->text.data : "", b->text.len, st);
                y += 1;
                break;
            }
        }
    return y;
}

/* Monta o transcript. `from == 0` remonta tudo (cabecalho incluso);
   `from > 0` repinta so' desse bloco para baixo — e' o que mantem o
   streaming leve: antes cada frame reprocessava o markdown da conversa
   inteira, e' esse o lag que aparecia em conversas longas. */
static void render_transcript(int width, size_t from)
{
    Screen *s = &app.tr;
    if (s->cols != width) {
        screen_free(s);
        screen_init(s, width, 8);
        screen_set_grow(s, 1);
        /* reinit: screen_init zera o flag */
        s->grow = 1;
        from = 0;                       /* largura nova: tudo de novo */
    }

    int y;
    if (from == 0) {
        s->rows = 0;                    /* reinicia o conteudo */
        screen_set_grow(s, 1);
        screen_clear(s);
        y = 0;
        /* cabecalho de boas-vindas */
        y += ui_welcome(s, y, width, &app.theme, app.cfg.model, app.agent.cwd);
        /* primeira execucao (sem chave): diz o que fazer, sem precisar /status */
        if (!app.cfg.api_key) {
            Style st_h = { tcolor_hex(theme_tok(&app.theme, TK_DIM)), COL_DEFAULT, ATTR_ITALIC };
            const char *hint = "sem chave de API · use /login para colar a chave do provedor";
            screen_text(s, 2, y, hint, strlen(hint), st_h, width - 4);
            y += 1;
        }
        y += 1;
        if (app.plan.len) {
            y += plan_draw(s, y, width);
            y += 1;
        }
        app.tr_done = 0;
    } else {
        if (from > app.n_blocks) from = app.n_blocks;
        if (from > app.tr_done) from = app.tr_done;     /* ainda nao montado */
        y = (from < app.tr_done) ? app.blocks[from].tr_y : app.tr_end_y;
        if (y < 0) y = 0;
        /* apaga o que foi desenhado abaixo: o bloco pode ter encolhido */
        if (y < s->rows) {
            Style blank = { COL_DEFAULT, COL_DEFAULT, 0 };
            screen_fill_rect(s, 0, y, s->cols, s->rows - y, blank);
        }
        s->rows = y;
        app.tr_done = from;
    }

    for (size_t i = app.tr_done; i < app.n_blocks; i++) {
        app.blocks[i].tr_y = y;
        y = draw_block(s, y, width, &app.blocks[i]);
    }
    app.tr_done = app.n_blocks;
    app.tr_end_y = y;


    if (y == 0) {
        screen_ensure_rows(s, 1);
        s->rows = 1;
    } else {
        screen_ensure_rows(s, y);
        s->rows = y;
    }
    app.tr_dirty = 0;
    app.tr_fast = 0;
}

/* Reconstroi o transcript da UI a partir das mensagens retomadas da sessao.
   Os argumentos das tools vem do assistant que as pediu (casando o id). */
static void blocks_from_msgs(void)
{
    const char *args_of[64];
    char *ids[64];
    size_t n_ids = 0;
    memset(args_of, 0, sizeof args_of);
    memset(ids, 0, sizeof ids);

    for (size_t i = 0; i < app.agent.msgs.n; i++) {
        Msg *m = &app.agent.msgs.items[i];
        if (m->role == ROLE_USER) {
            Block *b = block_add(B_USER);
            str_add(&b->text, m->text.data ? m->text.data : "", m->text.len);
        } else if (m->role == ROLE_ASSISTANT) {
            /* o raciocinio vem antes do texto, como no streaming ao vivo */
            if (m->thinking.len) {
                Block *b = block_add(B_THINKING);
                str_add(&b->text, m->thinking.data, m->thinking.len);
            }
            if (m->text.len) {
                Block *b = block_add(B_ASSISTANT);
                str_add(&b->text, m->text.data, m->text.len);
            }
            for (size_t k = 0; k < m->n_calls && n_ids < 64; k++) {
                ids[n_ids] = str_dup(m->calls[k].id ? m->calls[k].id : "");
                args_of[n_ids] = m->calls[k].args;
                n_ids++;
            }
        } else if (m->role == ROLE_TOOL) {
            Block *b = block_add(B_TOOL);
            b->tool_name = str_dup(m->tool_name ? m->tool_name : "tool");
            const char *args = "{}";
            for (size_t k = 0; k < n_ids; k++)
                if (ids[k] && m->tool_call_id && str_eq(ids[k], m->tool_call_id) && args_of[k])
                    args = args_of[k];
            b->tool_args = str_dup(args);
            b->tool_state = m->is_error ? 2 : 1;
            b->tool_secs = 0;
            str_add(&b->tool_output, m->text.data ? m->text.data : "", m->text.len);
        }
    }
    for (size_t k = 0; k < n_ids; k++) free(ids[k]);
}

/* ================================================================== */
/* Frame                                                               */
/* ================================================================== */
static void render(void)
{
    int cols = 0, rows = 0;
    term_size(&cols, &rows);
    if (app.screen.cols != cols || app.screen.rows != rows)
        screen_resize(&app.screen, cols, rows);
    screen_clear(&app.screen);

    if (app.tr_width != cols) {
        /* a largura mudou (ex.: entrar em tela cheia): o transcript precisa
           ser remontado, senao as caixas ficam com a largura antiga */
        app.tr_width = cols;
        app.tr_dirty = 1;
        app.tr_fast = 0;
        app.tr_done = 0;
    }
    if (app.tr_dirty) {
        render_transcript(cols, 0);
    } else if (app.tr_fast) {
        /* so' o que mudou (streaming, tool ao vivo, bloco novo) */
        render_transcript(cols, app.tr_fast_from);
    }

    /* -------- lista de comandos / seletor -------- */
    int n_items = menu_prepare();
    app.menu_visible = n_items > 0;
    int max_rows = n_items > 8 ? 8 : n_items;
    if (app.menu_visible) {
        if (app.menu_sel >= n_items) app.menu_sel = n_items - 1;
        if (app.menu_sel < 0) app.menu_sel = 0;
        if (app.menu_sel < app.menu_scroll) app.menu_scroll = app.menu_sel;
        if (app.menu_sel >= app.menu_scroll + max_rows) app.menu_scroll = app.menu_sel - max_rows + 1;
        if (app.menu_scroll > n_items - max_rows) app.menu_scroll = n_items - max_rows;
        if (app.menu_scroll < 0) app.menu_scroll = 0;
    } else {
        app.menu_scroll = 0;
        app.menu_sel = 0;
    }
    int menu_h = app.menu_visible ? max_rows + 1
                                 : ((app.picker == 1 || app.picker == 2 || app.picker == 5 || app.picker == 6) ? 1 : 0);
    if (!app.menu_visible) { app.menu_y0 = 0; app.menu_rows = 0; }

    /* altura do rodape */
    const int footer_h = 2;      /* pwd + stats; sem linha de notificacao */

    /* altura do editor (o seletor usa a linha como filtro) */
    const char *edit_text = app.input.data ? app.input.data : "";
    size_t edit_len = app.input.len;
    size_t edit_cur = app.cursor;
    if (app.picker) {
        edit_text = app.picker_filter.data ? app.picker_filter.data : "";
        edit_len = app.picker_filter.len;
        edit_cur = edit_len;
    }
    int input_rows = ui_input_rows(edit_text, edit_len, cols);
    if (input_rows > 8) input_rows = 8;
    int editor_h = input_rows + 2;

    /* -------- painel da fila: a mensagem ja' entra no chat (dim), so' o
       contador fica na borda do editor -------- */
    int fila_vis = 0;

    int tr_view = rows - footer_h - editor_h - menu_h - fila_vis;
    if (tr_view < 1) tr_view = 1;

    /* recorta a janela visivel do transcript, alinhada embaixo */
    int total = app.tr.rows;
    /* rolou para tras e o agente escreveu mais? desconta o crescimento: sem
       isso o topo escorrega para baixo a cada linha nova (ler no meio de uma
       resposta era impossivel). So' vale quando a largura nao mudou. */
    if (app.scroll > 0 && app.tr_prev_width == cols && app.tr_total_prev > 0 &&
        total > app.tr_total_prev)
        app.scroll += total - app.tr_total_prev;
    app.tr_total_prev = total;
    app.tr_prev_width = cols;
    if (app.tr_prev_width && app.tr_prev_width != cols) sel_clear();
    int max_scroll = total > tr_view ? total - tr_view : 0;
    if (app.scroll > max_scroll) app.scroll = max_scroll;
    if (app.scroll < 0) app.scroll = 0;
    int start = total - tr_view - app.scroll;
    if (start < 0) start = 0;
    screen_blit(&app.screen, &app.tr, start, 0, tr_view);
    app.tr_start = start;
    app.tr_view = tr_view;
    render_selection();

    /* editor */
    int ey = rows - footer_h - editor_h - menu_h;
    char wmsg[128];
    const char *wm = app.work_msg.data ? app.work_msg.data : "Working...";
    int mostrar_status = 0;
    int spin = -1;                       /* -1 = sem spinner (aviso estatico) */
    if (app.working) {
        spin = spin_from_clock();
        if (fila_count() > 0) {
            snprintf(wmsg, sizeof wmsg, "%s \u00b7 %d na fila", wm, fila_count());
            wm = wmsg;
        }
    } else if (app.status.len && now_ms() < app.status_until) {
        wm = app.status.data;            /* aviso passageiro (copia, etc.) */
        mostrar_status = 1;
    }
    if (fila_vis > 0) (void)0;
    ui_editor(&app.screen, ey, cols, &app.theme, edit_text, edit_len, edit_cur,
              app.working || mostrar_status, wm,
              spin, 0, 8, (const char **)app.imgs, app.n_imgs);
    if (app.ext_mode) ext_panel_draw(&app.screen, ey, cols);
    if (app.picker) {
        const char *titulo = menu_title();
        if (titulo) ui_border_label(&app.screen, ey, cols, &app.theme, titulo);
    } else if (app.scroll > 0 && !app.ext_mode && !app.working && !mostrar_status) {
        /* dica de rolagem so' quando a borda nao tem nada mais util para
           mostrar: o "Working..." e o aviso passageiro tem prioridade (antes
           o hint apagava o status e o quadrado sumia durante o trabalho) */
        char hint[110];
        snprintf(hint, sizeof hint, "\u2191 %d linhas acima \u00b7 ESC volta ao fim", app.scroll);
        ui_border_label(&app.screen, ey, cols, &app.theme, hint);
    }

    /* lista de comandos / seletor */
    if (app.menu_visible) {
        MenuItem mitens[9];
        char descbuf[9][192];
        char namebuf[9][128];
        int win = n_items - app.menu_scroll;
        if (win > max_rows) win = max_rows;
        int name_col = 12;
        for (int i = 0; i < win; i++) {
            Item *it = &g_items[app.menu_scroll + i];
            /* modelos aparecem como provedor/modelo */
            if (it->model && it->provider) {
                const char *pv = (app.cfg.provider && str_eq(app.cfg.provider, it->provider->id))
                                     ? app.cfg.provider : it->provider->id;
                snprintf(namebuf[i], sizeof namebuf[i], "%s/%s", pv, it->name);
                mitens[i].name = namebuf[i];
            } else {
                mitens[i].name = it->name;
            }
            mitens[i].desc = item_desc(it, descbuf[i], sizeof descbuf[i]);
            int w = mitens[i].name ? str_width(mitens[i].name) : 0;
            if (w > name_col) name_col = w;
        }
        int lim = cols / 3;
        if (lim < 18) lim = 18;
        if (lim > 46) lim = 46;
        if (name_col > lim) name_col = lim;
        name_col += 2;
        char counter[64];
        snprintf(counter, sizeof counter, "(%d/%d)", app.menu_sel + 1, n_items);
        app.menu_y0 = ey + editor_h;
        app.menu_rows = win;
        ui_menu(&app.screen, ey + editor_h, cols, &app.theme, mitens, win,
                app.menu_sel - app.menu_scroll, 0, win, name_col, counter);
    } else if (app.picker == 1 || app.picker == 2 || app.picker == 6) {
        /* filtro sem resultado: mostra a dica e deixa o Esc fechar */
        Style st_vazio = { tcolor_hex(theme_tok(&app.theme, TK_DIM)), COL_DEFAULT, 0 };
        static const char vazio[] = "(nada encontrado - esc cancela)";
        screen_text(&app.screen, 2, ey + editor_h, vazio, sizeof vazio - 1, st_vazio, cols - 3);
    }

    /* rodape */
    {
        char pwd[512];
        char *home = home_get();
        const char *cwd = app.agent.cwd;
        size_t hl = strlen(home);
        if (hl > 0 && strncmp(cwd, home, hl) == 0) {
            snprintf(pwd, sizeof pwd, "~%s", cwd + hl);
        } else {
            snprintf(pwd, sizeof pwd, "%s", cwd);
        }
        free(home);

        long used = app.agent.total_in;
        long win = 200000;
        char stats[256];
        double pct = win > 0 ? (100.0 * (double)used / (double)win) : 0;
        snprintf(stats, sizeof stats, "up %ld  down %ld  %.1f%%/%ldk",
                 app.agent.total_in, app.agent.total_out, pct, win / 1000);

        char right[320];
        snprintf(right, sizeof right, "(%s) %s • thinking %s",
                 app.cfg.provider ? app.cfg.provider : "?",
                 app.cfg.model ? app.cfg.model : "no-model",
                 app.cfg.thinking_level ? app.cfg.thinking_level : "off");

        ui_footer(&app.screen, rows - footer_h, cols, &app.theme, pwd, stats, right, NULL);
    }

    term_present(&app.screen);
}

/* ================================================================== */
/* Callbacks do agente                                                 */
/* ================================================================== */
static size_t g_cur_assistant = (size_t)-1;   /* indice, nao ponteiro: o array de
                                                 blocos e' realocado por block_add */
static size_t g_cur_thinking  = (size_t)-1;
static uint64_t g_last_render = 0;

static void maybe_render(int force)
{
    uint64_t now = now_ms();
    if (force || now - g_last_render > 40) {
        g_last_render = now;
        render();
    }
}

static void ui_poll_input(void);   /* le teclado durante a geracao */

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

/* Chamado de dentro da ferramenta longa (bash): repinta para o tempo andar. */
static void tools_tick_ui(void *ud)
{
    (void)ud;
    ui_poll_input();
    for (size_t i = app.n_blocks; i > 0; i--) {
        if (app.blocks[i - 1].kind == B_TOOL && app.blocks[i - 1].tool_state == 0) {
            tr_touch(i - 1);
            break;
        }
    }
    maybe_render(0);
}

/* Mesmo papel do tools_tick_ui, mas para o stream do provedor: e' chamado
   quando a leitura esta' parada esperando byte (o modelo "pensando"). Sem
   ele a tela ficava congelada e o Ctrl+C so' valia depois do primeiro token. */
static void provider_tick_ui(void *ud)
{
    (void)ud;
    ui_poll_input();
    maybe_render(0);
}

/* Gancho de teste (so' no --replay): simula o usuario digitando + Enter
   depois do N-esimo pedaco do stream, sem janela e sem SendKeys. */
static int  g_replay_steer_at = -1;
static char g_replay_steer_text[512];
static int  g_replay_chunks = 0;
static void enqueue_input(void);

static void cb_on_text(void *ud, const char *s, size_t n)
{
    (void)ud;
    app.last_activity_ms = now_ms();
    app.stall_warned = 0;
    if (g_cur_assistant == (size_t)-1) {
        block_add(B_ASSISTANT);
        g_cur_assistant = app.n_blocks - 1;
    }
    str_add(&app.blocks[g_cur_assistant].text, s, n);
    tr_touch(g_cur_assistant);
    if (g_replay_steer_at >= 0 && ++g_replay_chunks >= g_replay_steer_at) {
        g_replay_steer_at = -1;                      /* dispara uma vez */
        str_clear(&app.input);
        str_adds(&app.input, g_replay_steer_text);
        app.cursor = app.input.len;
        enqueue_input();
    }
    ui_poll_input();
    maybe_render(0);
}

static void cb_on_thinking(void *ud, const char *s, size_t n)
{
    (void)ud;
    app.last_activity_ms = now_ms();
    app.stall_warned = 0;
    if (g_cur_thinking == (size_t)-1) {
        block_add(B_THINKING);
        g_cur_thinking = app.n_blocks - 1;
    }
    str_add(&app.blocks[g_cur_thinking].text, s, n);
    tr_touch(g_cur_thinking);
    ui_poll_input();
    maybe_render(0);
}

static void cb_on_retry(void *ud, int tentativa, int total, const char *erro)
{
    (void)ud;
    Block *b = block_add(B_RETRY);
    str_addf(&b->text, "tentativa %d/%d falhou: %s", tentativa, total, erro ? erro : "?");
    maybe_render(1);
}

static void cb_on_turn_start(void *ud)
{
    (void)ud;
    g_cur_assistant = (size_t)-1;
    g_cur_thinking = (size_t)-1;
    app.working = 1;
    app.last_activity_ms = now_ms();
    app.stall_warned = 0;
    str_clear(&app.work_msg);
    str_adds(&app.work_msg, "Working...");
}

static void cb_on_tool_start(void *ud, const char *name, const char *args)
{
    (void)ud;
    app.last_activity_ms = now_ms();
    app.stall_warned = 0;
    Block *b = block_add(B_TOOL);
    b->tool_name = str_dup(name);
    b->tool_args = str_dup(args);
    b->tool_state = 0;
    b->tool_t0 = now_ms();
    str_clear(&app.work_msg);
    str_addf(&app.work_msg, "Running %s...", name);
    g_cur_assistant = (size_t)-1;
    g_cur_thinking = (size_t)-1;
    maybe_render(1);
}

static void cb_on_tool_end(void *ud, const char *name, const char *output, size_t out_len,
                           int is_error, double secs)
{
    (void)ud;
    (void)name;
    (void)out_len;
    for (size_t i = app.n_blocks; i > 0; i--) {
        Block *b = &app.blocks[i - 1];
        if (b->kind == B_TOOL && b->tool_state == 0) {
            b->tool_state = is_error ? 2 : 1;
            b->tool_secs = secs;
            str_clear(&b->tool_output);
            str_adds(&b->tool_output, output ? output : "");
            tr_touch(i - 1);
            break;
        }
    }
    maybe_render(1);
}

/* Le o teclado enquanto o agente trabalha: rolar a conversa, digitar e
   enfileirar mensagem continuam funcionando durante o streaming e durante
   as ferramentas (o bash chama isso pelo tick). */
static void ui_poll_input(void)
{
    steer_dbg("poll has=%d", term_has_input());
    while (term_has_input()) {
        Event ev;
        if (!term_read(&ev, 0)) break;
        steer_dbg("ev type=%d cp=%u working=%d", (int)ev.type, (unsigned)ev.cp, app.working);
        if (ev.type == EV_CTRL && ev.cp == 'c') {
            /* selecao ativa: Ctrl+C copia (nao interrompe o turno) */
            if (app.sel_active && sel_has_text()) {
                copy_selection();
                render();
                continue;
            }
            app.abort_requested = 1;
            notify("Interrompido");
            return;
        }
        if (ev.type == EV_RESIZE) { render(); continue; }
        if (ev.type == EV_WHEEL) {
            scroll_wheel(ev.shift);
            render();
            continue;
        }
        /* digitacao while working: texto entra na caixa e Enter enfileira.
           ESC NAO interrompe: ele volta ao fim da conversa / limpa a selecao
           (quem interrompe e' Ctrl+C, como no README). */
        handle_event(&ev);
    }
}

/* Quanto tempo sem nenhum byte do provedor para avisar (ms). DELTA_STALL_MS
   existe para o teste usar 300 ms em vez de 45 s. */
static int stall_ms(void)
{
    static int ms = -1;
    if (ms < 0) {
        char *e = env_get("DELTA_STALL_MS");
        ms = (e && *e) ? atoi(e) : 0;
        free(e);
        if (ms <= 0) ms = 45000;
    }
    return ms;
}

/* Poll de entrada durante o streaming: aborta, roda a tela e deixa o
   usuario digitar (Enter enfileira; /stop interrompe). */
static int cb_should_abort(void *ud)
{
    (void)ud;
    if (app.abort_requested) return 1;
    /* provedor aceitou e parou de mandar: avisa uma vez (o turno continua —
       Ctrl+C cancela). Antes disso a sessao ficava "parada" sem explicacao
       ate' o timeout de 5 min da conexao. */
    if (app.working && app.last_activity_ms && !app.stall_warned &&
        now_ms() - app.last_activity_ms > (uint64_t)stall_ms()) {
        app.stall_warned = 1;
        notify("sem dados do provedor ha' %d s \u00b7 Ctrl+C cancela (pode ser fila do provedor)",
               (stall_ms() + 999) / 1000);
        render();
    }
    ui_poll_input();
    /* hotkey de colagem de imagem/video (o terminal engole o chord) */
    if (term_poll_paste_hotkey() && now_ms() - app.last_clip_ms >= 300) {
        app.last_clip_ms = now_ms();
        Clip c;
        char *dir = paste_dir();
        if (clipboard_read(&c, dir) && c.kind != CLIP_TEXT) {
            inserir_colagem(&c);
            render();
        }
        clip_free(&c);
        free(dir);
    }
    /* o spinner segue o relogio (render()); aqui so' precisa redesenhar */
    maybe_render(0);
    return 0;
}

/* ================================================================== */
/* Comandos                                                           */
/* ================================================================== */
/* ================================================================== */
/* Extensoes: /extensions (painel) e /extensions <pedido> (cria)       */
/* ================================================================== */
static int ext_json_extrair(const char *txt, Str *out)
{
    const char *b = strchr(txt, '{');
    if (!b) return 0;
    int prof = 0, dentro = 0, esc = 0;
    for (const char *p = b; *p; p++) {
        char c = *p;
        if (dentro) {
            if (esc) esc = 0;
            else if (c == '\\') esc = 1;
            else if (c == '"') dentro = 0;
            continue;
        }
        if (c == '"') { dentro = 1; continue; }
        if (c == '{' || c == '[') prof++;
        else if (c == '}' || c == ']') {
            prof--;
            if (prof == 0) { str_add(out, b, (size_t)(p - b + 1)); return 1; }
        }
    }
    return 0;
}

/* pede ao modelo um spec de extensao e registra */
static int ext_criar_com_modelo(const char *pedido)
{
    if (!app.cfg.api_key) {
        notify("sem API key: use /login antes de criar extensao");
        render();
        return 0;
    }
    Provider p;
    memset(&p, 0, sizeof p);
    p.provider = app.cfg.provider;
    p.model = app.cfg.model;
    p.api_key = app.cfg.api_key;
    p.base_url = app.cfg.base_url;
    p.max_tokens = app.cfg.max_tokens ? app.cfg.max_tokens : 2048;
    p.temperature = app.cfg.temperature;
    p.extra_headers_json = app.cfg.headers;
    p.session_id = session_id();
    p.support_image = app.cfg.supports_image;

    static const char *SIS =
        "You design extensions for Delta Agent. Answer with ONE JSON object and nothing else:\n"
        "{\"kind\":\"tool|skill|command|mcp\",\"name\":\"slug\",\"desc\":\"one line\","
        "\"arg\":\"...\",\"schema\":\"{...}\"}\n"
        "- tool: arg is a shell command line that receives the JSON arguments as argv[1]; schema is a JSON Schema.\n"
        "- skill: arg is the SKILL.md markdown body.\n"
        "- command: arg is the prompt template sent when the user types /name.\n"
        "- mcp: arg is the server URL.\n";

    MsgList l;
    msglist_init(&l);
    Msg *m = msglist_add(&l, ROLE_USER);
    str_adds(&m->text, pedido);

    Turn t;
    turn_init(&t);
    Str err; str_init(&err);
    int ok = provider_run(&p, &l, SIS, &t, NULL, NULL, &err);

    int criou = 0;
    if (ok) {
        Str spec; str_init(&spec);
        if (ext_json_extrair(t.text.data ? t.text.data : "", &spec)) {
            JValue *v = json_parse_cstr(spec.data);
            if (v && v->kind == J_OBJ) {
                const char *kind = json_str(json_get(v, "kind"));
                const char *nome = json_str(json_get(v, "name"));
                const char *arg = json_str(json_get(v, "arg"));
                const char *desc = json_str(json_get(v, "desc"));
                const char *schema = json_str(json_get(v, "schema"));
                if (*kind && *nome) {
                    if (ext_add(kind, nome, arg, desc, schema)) {
                        ext_reload();
                        if (str_eq(kind, "skill")) {
                            /* grava o SKILL.md tambem */
                            Str js; str_init(&js);
                            str_adds(&js, "{\"op\":\"create\",\"name\":");
                            json_write_string(&js, nome, strlen(nome));
                            str_adds(&js, ",\"description\":");
                            json_write_string(&js, desc, strlen(desc));
                            str_adds(&js, ",\"content\":");
                            json_write_string(&js, arg, strlen(arg));
                            str_addc(&js, '}');
                            JValue *a2 = json_parse_cstr(js.data);
                            const Tool *ts = tool_find("skills");
                            if (a2 && ts) {
                                ToolResult tr;
                                tool_result_init(&tr);
                                ts->run((Tool *)ts, a2, app.agent.cwd, &tr);
                                tool_result_free(&tr);
                            }
                            if (a2) json_free(a2);
                            str_free(&js);
                        }
                        Block *b = block_add(B_INFO);
                        str_addf(&b->text, "Extensao criada: %s (%s) — %s\n", nome, kind, desc);
                        criou = 1;
                    }
                }
            }
            if (v) json_free(v);
        } else {
            Block *b = block_add(B_INFO);
            str_adds(&b->text, "O modelo nao devolveu um JSON de extensao:\n");
            str_add(&b->text, t.text.data ? t.text.data : "", t.text.len);
        }
        str_free(&spec);
    } else {
        Block *b = block_add(B_INFO);
        str_addf(&b->text, "Falha ao criar extensao: %s", err.data ? err.data : "erro");
    }
    str_free(&err);
    turn_free(&t);
    msglist_free(&l);
    app.tr_dirty = 1;
    render();
    return criou;
}

/* painel de extensoes acima do editor */
static void ext_panel_draw(Screen *s, int ey, int cols)
{
    int n = ext_count();
    int visto = n < 8 ? n : 8;
    if (visto < 1) visto = 1;
    int topo = ey - visto - 1;
    if (topo < 1) topo = 1;

    Style st_t = { tcolor_hex(theme_tok(&app.theme, TK_ACCENT)), COL_DEFAULT, ATTR_BOLD };
    Style st_s = { tcolor_hex(theme_tok(&app.theme, TK_DIM)), COL_DEFAULT, 0 };
    Style st_i = { tcolor_hex(theme_tok(&app.theme, TK_TEXT)), COL_DEFAULT, 0 };

    uint32_t bg = tcolor_hex(theme_tok(&app.theme, TK_TOOL_SUCCESS_BG));
    screen_fill_rect(s, 0, topo, cols, visto + 1, (Style){ COL_DEFAULT, bg, 0 });
    st_t.bg = bg; st_s.bg = bg; st_i.bg = bg;

    screen_text(s, 2, topo, "Extensões  —  Enter liga/desliga, Esc fecha", strlen("Extensões  —  Enter liga/desliga, Esc fecha"), st_t, cols - 4);
    if (n == 0) {
        screen_text(s, 2, topo + 1, "(nenhuma extensão — use /extensions <pedido>)", strlen("(nenhuma extensão — use /extensions <pedido>)"), st_s, cols - 4);
        return;
    }
    int off = app.ext_sel > 6 ? app.ext_sel - 6 : 0;
    for (int i = 0; i < visto && off + i < n; i++) {
        int idx = off + i;
        char nome[64], kind[16];
        int en = 1;
        if (!ext_get(idx, nome, sizeof nome, kind, sizeof kind, &en)) continue;
        char linha[192];
        snprintf(linha, sizeof linha, "%s [%s] %s%s", idx == app.ext_sel ? ">" : " ",
                 en ? "on " : "off", nome, str_eq(kind, "tool") ? " (tool)" :
                 str_eq(kind, "skill") ? " (skill)" : str_eq(kind, "command") ? " (comando)" : " (mcp)");
        screen_text(s, 2, topo + 1 + i, linha, strlen(linha),
                    idx == app.ext_sel ? st_t : st_i, cols - 4);
    }
}

static void handle_command(const char *cmd);

/* ================================================================== */
/* Fila de mensagens: o usuario escreve enquanto o agente trabalha e    */
/* as mensagens sao lidas no intervalo de thinking/tools                */
/* ================================================================== */
typedef struct {
    Str    text;
    char  *imgs[8];
    int    n_imgs;
    size_t block;      /* bloco do usuario ja' criado no transcript */
} MsgFila;

#define MAX_FILA 16
static MsgFila g_fila[MAX_FILA];
static int     g_fila_n = 0;

static void fila_push(const char *text, char **imgs, int n_imgs, size_t block)
{
    if (g_fila_n >= MAX_FILA) { notify("fila cheia (max %d)", MAX_FILA); return; }
    MsgFila *f = &g_fila[g_fila_n++];
    memset(f, 0, sizeof *f);
    str_init(&f->text);
    str_adds(&f->text, text);
    f->block = block;
    for (int i = 0; i < n_imgs && i < 8; i++)
        f->imgs[f->n_imgs++] = str_dup(imgs[i]);
}

static int fila_pop(Str *text, char **imgs, int *n_imgs)
{
    if (g_fila_n == 0) return 0;
    MsgFila *f = &g_fila[0];
    *text = f->text;                       /* transfere a propriedade */
    str_init(&f->text);
    *n_imgs = f->n_imgs;
    for (int i = 0; i < f->n_imgs; i++) { imgs[i] = f->imgs[i]; f->imgs[i] = NULL; }
    f->n_imgs = 0;
    g_fila_n--;
    memmove(&g_fila[0], &g_fila[1], sizeof(MsgFila) * (size_t)g_fila_n);
    return 1;
}

static void fila_clear(void)
{
    for (int i = 0; i < g_fila_n; i++) {
        if (g_fila[i].block < app.n_blocks && app.blocks[g_fila[i].block].kind == B_USER)
            app.blocks[g_fila[i].block].queued = 3;   /* nao enviada */
        str_free(&g_fila[i].text);
        for (int j = 0; j < g_fila[i].n_imgs; j++) free(g_fila[i].imgs[j]);
        g_fila[i].n_imgs = 0;
    }
    g_fila_n = 0;
    app.tr_dirty = 1;
}

/* Pega a ultima mensagem enfileirada e devolve para o editor (Ctrl+Q). */
static int fila_pop_last(Str *text, char **imgs, int *n_imgs, size_t *block)
{
    if (g_fila_n == 0) return 0;
    MsgFila *f = &g_fila[--g_fila_n];
    *text = f->text;                       /* transfere a propriedade */
    str_init(&f->text);
    *n_imgs = f->n_imgs;
    *block = f->block;
    for (int i = 0; i < f->n_imgs; i++) { imgs[i] = f->imgs[i]; f->imgs[i] = NULL; }
    f->n_imgs = 0;
    return 1;
}

/* Intervalo entre thinking/tools: o agente le o que foi enfileirado. */
static void cb_on_interval(void *ud)
{
    (void)ud;
    app.last_activity_ms = now_ms();
    app.stall_warned = 0;
    while (g_fila_n > 0) {
        Str t; str_init(&t);
        char *imgs[8]; int n_imgs = 0;
        size_t blk = g_fila[0].block;
        if (!fila_pop(&t, imgs, &n_imgs)) { str_free(&t); break; }
        /* o bloco ja' ficou no transcript quando foi digitado: agora ele
           deixa de ser "na fila" e vira mensagem normal do usuario */
        if (blk < app.n_blocks && app.blocks[blk].kind == B_USER) app.blocks[blk].queued = 0;
        tools_auto_memorize(t.data);
        agent_push_user_imgs(&app.agent, t.data, imgs, n_imgs);
        for (int i = 0; i < n_imgs; i++) free(imgs[i]);
        str_free(&t);
        app.tr_dirty = 1;
    }
    maybe_render(0);
}

/* Ha' mensagem esperando? (o agente corta o stream para le-la agora) */
static int cb_has_steer(void *ud)
{
    (void)ud;
    return g_fila_n > 0;
}

static int fila_count(void) { return g_fila_n; }

/* Enter com o agente trabalhando: texto comum entra na fila (o submit real
   so roda fora do agent_run). */
static void enqueue_input(void)
{
    if (app.input.len == 0) return;
    Str text; str_init(&text);
    str_add(&text, app.input.data, app.input.len);
    expand_paste(&text);
    str_clear(&app.input);
    app.cursor = 0;

    if (text.data[0] == '/') {
        notify("comando aguardando o fim do turno");
        str_free(&text);
        render();
        return;
    }

    fila_push(text.data, app.imgs, app.n_imgs, app.n_blocks);      /* bloco = o que vem agora */
    Block *qb = block_add(B_USER);
    str_add(&qb->text, text.data, text.len);
    qb->queued = 1;                        /* aparece no chat, apagada */
    app.tr_dirty = 1;
    for (int i = 0; i < app.n_imgs; i++) free(app.imgs[i]);
    app.n_imgs = 0;
    str_free(&text);
    notify("mensagem na fila (%d) \u2014 o agente le no pr\u00f3ximo passo", g_fila_n);
    render();
}

static void submit_input(void)
{
    if (app.input.len == 0) return;
    Str text; str_init(&text);
    str_add(&text, app.input.data, app.input.len);
    expand_paste(&text);

    app.scroll = 0;          /* enviou: volta para o fim da conversa */

    str_clear(&app.input);
    app.cursor = 0;

    if (text.data[0] == '/') {
        handle_command(text.data);
        str_free(&text);
        return;
    }

    tools_auto_memorize(text.data);   /* guarda a mensagem + fatos duraveis */

    if (!app.cfg.api_key) {
        Block *ub2 = block_add(B_USER);
        str_add(&ub2->text, text.data, text.len);
        Block *b = block_add(B_INFO);
        str_adds(&b->text, "Nenhuma chave configurada para este provedor. "
                           "Use /login para escolher um provedor e colar a chave.");
        str_free(&text);
        app.tr_dirty = 1;
        render();
        return;
    }

    Block *ub = block_add(B_USER);
    str_add(&ub->text, text.data, text.len);

    /* ISTO e o que entrega a mensagem ao modelo. Sem isso o agente so via o
       prompt de sistema e respondia "sobre o que voce esta trabalhando?". */
    agent_push_user_imgs(&app.agent, text.data, app.imgs, app.n_imgs);
    for (int i = 0; i < app.n_imgs; i++) free(app.imgs[i]);
    app.n_imgs = 0;

    app.working = 1;
    app.abort_requested = 0;
    str_clear(&app.work_msg);
    str_adds(&app.work_msg, "Working...");
    g_cur_assistant = (size_t)-1;
    g_cur_thinking = (size_t)-1;
    render();

    AgentCB cb;
    memset(&cb, 0, sizeof cb);
    cb.on_text = cb_on_text;
    cb.on_thinking = cb_on_thinking;
    cb.on_turn_start = cb_on_turn_start;
    cb.on_retry = cb_on_retry;
    cb.on_tool_start = cb_on_tool_start;
    cb.on_tool_end = cb_on_tool_end;
    cb.on_interval = cb_on_interval;
    cb.should_abort = cb_should_abort;
    cb.has_steer = cb_has_steer;
    cb.ud = NULL;

    /* Roda turno apos turno: mensagens enfileiradas enquanto o agente
       trabalhava entram aqui (ou no intervalo de thinking/tools). */
    agent_rebuild_prompt(&app.agent);   /* tools/skills criadas no turno anterior */
    for (;;) {
        Str err; str_init(&err);
        app.working = 1;
        app.abort_requested = 0;
        str_clear(&app.work_msg);
        str_adds(&app.work_msg, "Working...");
        g_cur_assistant = (size_t)-1;
        g_cur_thinking = (size_t)-1;
        render();

        size_t n0 = app.n_blocks;
        uint64_t t0 = now_ms();
        long in0 = app.agent.total_in, out0 = app.agent.total_out;
        int ok = agent_run(&app.agent, NULL, &cb, &err);
        app.working = 0;
        if (ok) turn_meta_attach(n0, t0, in0, out0);
        if (!ok && err.len) {
            Block *b = block_add(B_ERROR);
            str_adds(&b->text, err.data);
            str_free(&err);
            break;
        }
        if (!ok) {
            Block *b = block_add(B_INFO);
            str_adds(&b->text, "Operacao interrompida.");
            if (g_fila_n) {
                fila_clear();
                notify("mensagens enfileiradas descartadas");
            }
            str_free(&err);
            break;
        }
        str_free(&err);
        if (g_fila_n == 0) break;

        /* puxa a proxima mensagem da fila e continua */
        Str t; str_init(&t);
        char *imgs[8]; int n_imgs = 0;
        if (!fila_pop(&t, imgs, &n_imgs)) { str_free(&t); break; }
        Block *ub = block_add(B_USER);
        str_add(&ub->text, t.data, t.len);
        agent_push_user_imgs(&app.agent, t.data, imgs, n_imgs);
        for (int i = 0; i < n_imgs; i++) free(imgs[i]);
        str_free(&t);
        app.tr_dirty = 1;
    }

    app.working = 0;
    app.tr_dirty = 1;
    render();
}

/* O modelo atual aceita imagem na entrada? (modalities do models.dev) */
static void refresh_model_caps(void)
{
    app.cfg.supports_image = 0;
    catalog_ensure_full();
    if (!app.catalog_loaded || !app.cfg.provider || !app.cfg.model) return;
    const MdModel *m = catalog_model(&app.catalog, app.cfg.provider, app.cfg.model);
    if (m && m->image_in) app.cfg.supports_image = 1;
}

/* Resolve a chave do provedor atual: ambiente > auth.json. */
static void refresh_api_key(void)
{
    free(app.cfg.api_key);
    app.cfg.api_key = NULL;

    const char *env = catalog_env_var(&app.catalog, app.cfg.provider);
    if (env && *env) {
        char *v = env_get(env);
        if (v && *v) { app.cfg.api_key = v; return; }
        free(v);
    }
    char *stored = auth_get_key(app.cfg.provider);
    if (stored && *stored) { app.cfg.api_key = stored; return; }
    free(stored);
}

/* Abre o seletor de modelos (1), a lista de provedores do /login (2), o
   campo de chave (3), os niveis de thinking (4) ou o /logout (5). */
static void open_picker(int kind)
{
    if ((kind == 1 || kind == 2 || kind == 5) && !catalog_ensure()) return;
    app.picker = kind;
    app.menu_sel = 0;
    app.menu_scroll = 0;
    app.menu_suppressed = 0;
    str_clear(&app.picker_filter);
    app.tr_dirty = 1;
    render();
}

/* Aplica o item selecionado no menu/seletor. */
static void menu_apply(void)
{
    /* -------- mode 3: salvando a chave digitada -------- */
    if (app.picker == 3) {
        const char *key = app.picker_filter.data ? app.picker_filter.data : "";
        if (!*key) {
            app.picker = 0;
            str_clear(&app.picker_filter);
            set_status("cancelado");
            return;
        }
        const char *prov = app.login_provider ? app.login_provider : app.cfg.provider;
        auth_set_key(prov, key);

        /* o provedor configurado passa a ser o ativo */
        free(app.cfg.provider);
        app.cfg.provider = str_dup(prov);
        const char *ep = catalog_endpoint(&app.catalog, app.cfg.provider);
        if (ep && *ep) { free(app.cfg.base_url); app.cfg.base_url = str_dup(ep); }
        if (!catalog_model(&app.catalog, app.cfg.provider, app.cfg.model)) {
            const MdModel *d = catalog_default_model(&app.catalog, app.cfg.provider);
            if (d) { free(app.cfg.model); app.cfg.model = str_dup(d->id); }
        }
        config_save(&app.cfg);
        refresh_api_key();
        refresh_model_caps();
        app.picker = 0;
        str_clear(&app.picker_filter);
        app.tr_dirty = 1;
        notify("chave salva para %s · modelo: %s", app.cfg.provider, app.cfg.model);
        return;
    }

    if (g_items_n == 0 || app.menu_sel < 0 || (size_t)app.menu_sel >= g_items_n) return;
    Item *it = &g_items[app.menu_sel];

    if (app.picker == 5 && it->provider) {
        /* desconectar: so remove o que esta salvo no auth.json */
        const char *env = catalog_env_var(&app.catalog, it->provider->id);
        char *v = (env && *env) ? env_get(env) : NULL;
        int de_env = v && *v;
        free(v);
        if (de_env) {
            notify("%s usa a chave de %s (ambiente); nao ha o que remover",
                       it->provider->id, env);
        } else {
            auth_remove_key(it->provider->id);
            if (app.cfg.provider && str_eq(app.cfg.provider, it->provider->id)) refresh_api_key();
            notify("desconectado de %s", it->provider->id);
        }
        app.picker = 0;
        str_clear(&app.picker_filter);
        return;
    }

    if (app.picker == 4 && it->value) {
        free(app.cfg.thinking_level);
        app.cfg.thinking_level = str_dup(it->value);
        config_save(&app.cfg);
        app.picker = 0;
        str_clear(&app.picker_filter);
        notify("thinking: %s", it->value);
        return;
    }

    if (app.picker == 6 && it->model) {
        /* escolha do modelo dos SUBAGENTES (nao troca o modelo do chat) */
        free(app.cfg.subagent_model);
        app.cfg.subagent_model = str_dup(it->model->id);
        config_save(&app.cfg);
        snprintf(g_subagent_escolhido, sizeof g_subagent_escolhido, "%s", it->model->id);
        app.picker = 0;
        str_clear(&app.picker_filter);
        app.tr_dirty = 1;
        return;
    }

    if (app.picker == 1 && it->model) {
        free(app.cfg.model);
        app.cfg.model = str_dup(it->model->id);
        /* escolher um modelo de outro provedor troca o provedor tambem */
        if (it->provider) {
            free(app.cfg.provider);
            app.cfg.provider = str_dup(it->provider->id);
            const char *ep = catalog_endpoint(&app.catalog, it->provider->id);
            if (ep && *ep) { free(app.cfg.base_url); app.cfg.base_url = str_dup(ep); }
        }
        config_save(&app.cfg);
        refresh_api_key();
        refresh_model_caps();
        app.picker = 0;
        str_clear(&app.picker_filter);
        app.tr_dirty = 1;
        notify("modelo: %s · %s", app.cfg.provider ? app.cfg.provider : "?",
                   app.cfg.model ? app.cfg.model : "?");
        return;
    }

    if (app.picker == 2 && it->provider) {
        /* escolheu o provedor: agora pede a chave */
        free(app.login_provider);
        app.login_provider = str_dup(it->provider->id);
        app.picker = 3;
        str_clear(&app.picker_filter);
        app.menu_sel = 0;
        app.menu_scroll = 0;
        return;
    }

    if (app.picker == 0 && it->name) {
        Str cmd; str_init(&cmd);
        str_addc(&cmd, '/');
        str_adds(&cmd, it->name);
        str_clear(&app.input);
        app.cursor = 0;
        app.menu_suppressed = 0;
        handle_command(cmd.data);
        str_free(&cmd);
    }
}

/* Completa o comando selecionado na linha de entrada. */
static void menu_complete(void)
{
    if (g_items_n == 0 || app.menu_sel < 0 || (size_t)app.menu_sel >= g_items_n) return;
    Item *it = &g_items[app.menu_sel];
    if (!it->name) return;
    str_clear(&app.input);
    str_addc(&app.input, '/');
    str_adds(&app.input, it->name);
    str_addc(&app.input, ' ');
    app.cursor = app.input.len;
    app.menu_suppressed = 1;
}

static void handle_command(const char *raw)
{
    char cmd[64] = { 0 };
    const char *rest = "";
    {
        size_t i = 0;
        while (raw[i] && raw[i] != ' ' && i < sizeof cmd - 1) { cmd[i] = raw[i]; i++; }
        if (raw[i] == ' ') rest = raw + i + 1;
    }

    if (str_eq(cmd, "/quit") || str_eq(cmd, "/exit") || str_eq(cmd, "/q")) {
        app.quit = 1;
        return;
    }

    if (str_eq(cmd, "/help")) {
        Block *b = block_add(B_INFO);
        str_adds(&b->text, "Comandos (digite / para ver a lista):\n\n");
        for (int i = 0; i < N_COMMANDS; i++) {
            char line[192];
            char sig[48];
            snprintf(sig, sizeof sig, "/%s %s", COMMANDS[i].name,
                     COMMANDS[i].takes_arg && COMMANDS[i].hint ? COMMANDS[i].hint : "");
            snprintf(line, sizeof line, "  %-34s %s\n", sig, cmd_desc(&COMMANDS[i]));
            str_adds(&b->text, line);
        }
        str_adds(&b->text,
            "\nAtalhos: Enter envia, Shift+Enter quebra linha. Arraste o mouse para\n"
            "selecionar (fundo branco, letra preta) e Ctrl+C copia; um clique limpa\n"
            "a seleção. Sem seleção, Ctrl+C interrompe a geração ou limpa o editor\n"
            "(nunca fecha o app; Ctrl+D sai). Digite enquanto ele trabalha: a\n"
            "mensagem aparece no chat marcada \"na fila\" e o agente corta a resposta\n"
            "no meio para le-la (Ctrl+Q devolve).\n"
            "Editor: Ctrl+U/K (linha), Ctrl+W (palavra), Ctrl+Y (colar), Ctrl+Z (undo).\n"
            "Up/Down, PgUp/PgDn e a roda do mouse rolam a conversa; Esc limpa a\n"
            "seleção e volta para o fim. Tab completa o comando, Esc fecha o menu.\n");
        app.tr_dirty = 1;
        render();
        return;
    }

    /* /projects <pasta>: muda o cwd do Delta (e lembra para a proxima vez) */
    if (str_eq(cmd, "/projects") || str_eq(cmd, "/project") || str_eq(cmd, "/cd")) {
        while (*rest == ' ') rest++;
        if (!*rest) {
            Block *b = block_add(B_INFO);
            str_adds(&b->text, "Projeto atual: ");
            str_adds(&b->text, app.agent.cwd);
            str_adds(&b->text, "\nUso: /projects <pasta>   (ex.: /projects C:\\dev\\app  ou  /projects ~/dev/app)\nA pasta fica salva: nas proximas execucoes o Delta ja comeca nela.\n");
            str_adds(&b->text, "Skills, mcps, memory, extensions e o sistema ficam em ~/.deltaCore.\n");
            app.tr_dirty = 1;
            render();
            return;
        }
        char *raw = str_dup(rest);
        size_t rl = strlen(raw);
        while (rl && (raw[rl - 1] == ' ' || raw[rl - 1] == '\r' || raw[rl - 1] == '\n')) raw[--rl] = 0;
        /* aceita caminho entre aspas (arrastar do Explorer cola assim) */
        if (rl >= 2 && ((raw[0] == '"' && raw[rl - 1] == '"') || (raw[0] == '\'' && raw[rl - 1] == '\''))) {
            memmove(raw, raw + 1, rl - 2);
            raw[rl - 2] = 0;
        }

        char *path = tools_resolve(app.agent.cwd, raw);
        free(raw);
        if (!path || !path_is_dir(path)) {
            notify("pasta nao encontrada: %s", path ? path : rest);
            free(path);
            render();
            return;
        }
        agent_set_cwd(&app.agent, path);
        free(app.cfg.project_dir);
        app.cfg.project_dir = str_dup(path);
        config_save(&app.cfg);
        Block *b = block_add(B_INFO);
        str_addf(&b->text, "projeto: %s", path);
        str_adds(&b->text, "\n(salvo na config; a sessao atual continua, as proximas ja abrem aqui)\n");
        app.tr_dirty = 1;
        notify("projeto: %s", path);
        free(path);
        render();
        return;
    }

    /* /subagentmodel: escolhe o modelo dos subagentes (o Delta tambem
       pergunta sozinho antes de criar um, se ainda nao houver escolha) */
    if (str_eq(cmd, "/subagentmodel") || str_eq(cmd, "/subagent")) {
        open_picker(6);
        return;
    }

    /* /undo [arquivo]: restaura o conteudo anterior a ultima edicao */
    if (str_eq(cmd, "/undo")) {
        const char *alvo = rest && *rest ? rest : NULL;
        char *p = NULL;
        if (tools_undo_restore(alvo, &p)) {
            notify("restaurado: %s", p ? p : "?");
            free(p);
            app.tr_dirty = 1;
        } else {
            notify("nada para desfazer");
        }
        render();
        return;
    }

    /* /deltagroup: link de convite do grupo do Delta no Telegram */
    if (str_eq(cmd, "/deltagroup") || str_eq(cmd, "/group") || str_eq(cmd, "/telegram")) {
        Block *b = block_add(B_INFO);
        str_adds(&b->text, "Grupo do Delta no Telegram\n\n");
        str_addf(&b->text, "  %s\n\n", DELTA_TELEGRAM);
        str_adds(&b->text,
                 "  O convite \u00e9 p\u00fablico e est\u00e1 no clipboard: \u00e9 onde o pessoal tira d\u00favidas, "
                 "avisa das vers\u00f5es novas e mostra as automa\u00e7\u00f5es que anda fazendo com o Delta. "
                 "Entrar no grupo n\u00e3o muda o que o Delta faz nem libera nada: modelo e rede continuam "
                 "sendo os seus (provedor + chave). Ctrl+clique no link acima para abrir.\n");
        if (!clipboard_write_text(DELTA_TELEGRAM))
            str_adds(&b->text, "  (n\u00e3o consegui copiar automaticamente: selecione o link acima)\n");
        app.tr_dirty = 1;
        render();
        return;
    }

    /* /update [--check]: consulta os releases do GitHub e troca o binario */
    if (str_eq(cmd, "/update") || str_eq(cmd, "/atualizar")) {
        Block *b = block_add(B_INFO);
        str_adds(&b->text, "procurando versoes novas no GitHub...");
        app.tr_dirty = 1;
        render();

        UpdateInfo u;
        int ok = update_check(&u, 0);
        str_clear(&b->text);
        if (!ok) {
            str_addf(&b->text, "nao consegui consultar os releases: %s",
                     u.err[0] ? u.err : "sem conexao");
            if (u.available)
                str_addf(&b->text, "\n(pelo cache, a versao %s esta disponivel: %s)\n",
                         u.latest, u.page);
            else
                str_addc(&b->text, '\n');
        } else if (!u.available) {
            str_addf(&b->text, "voce ja esta na versao mais nova (%s).", u.current);
        } else if (str_istarts(rest, "--check")) {
            str_addf(&b->text, "nova versao %s (a sua: %s)\n%s\n", u.latest, u.current, u.page);
        } else {
            Str msg; str_init(&msg);
            str_addf(&b->text, "nova versao %s (a sua: %s)\n", u.latest, u.current);
            int trocou = update_apply(&u, &msg);
            if (msg.len) str_addf(&b->text, "%s\n", msg.data);
            if (!trocou && u.page[0]) str_addf(&b->text, "pagina do release: %s\n", u.page);
            str_free(&msg);
        }
        app.tr_dirty = 1;
        render();
        return;
    }

    /* /terminal <comando>: roda no shell do sistema e mostra a saida aqui
       dentro, num bloco igual ao das ferramentas. */
    if (str_eq(cmd, "/terminal") || str_eq(cmd, "/sh") || str_eq(cmd, "/exec")) {
        while (*rest == ' ') rest++;
        if (!*rest) {
            notify("uso: /terminal <comando>   (ex.: /terminal git status)");
            render();
            return;
        }
        size_t bi = app.n_blocks;
        Block *tb = block_add(B_TOOL);
        tb->tool_name = str_dup("terminal");
        tb->tool_args = str_dup(rest);        /* comando cru (renderiza como $ cmd) */
        tb->tool_state = 0;
        tb->tool_t0 = now_ms();
        str_clear(&app.work_msg);
        str_addf(&app.work_msg, "$ %s", rest);
        app.working = 1;
        app.tr_dirty = 1;
        render();

        int code = 0;
        int ok = tools_exec(app.agent.cwd, rest, 120, &app.blocks[bi].tool_output, &code);
        tb = &app.blocks[bi];
        if (!ok) {
            str_clear(&tb->tool_output);
            str_adds(&tb->tool_output, "Erro: falha ao iniciar o processo.\n");
            tb->tool_state = 2;
        } else {
            tb->tool_state = code == 0 ? 1 : 2;
        }
        tb->tool_secs = (double)(now_ms() - tb->tool_t0) / 1000.0;
        if (code != 0 && tb->tool_state != 2)
            str_addf(&tb->tool_output, "\n[saiu com codigo %d]\n", code);
        app.working = 0;
        app.tr_dirty = 1;
        notify("$ %s \u2014 saiu com %d em %.1fs", rest, code, tb->tool_secs);
        render();
        return;
    }

    if (str_eq(cmd, "/clear")) {
        blocks_clear();
        msglist_free(&app.agent.msgs);
        msglist_init(&app.agent.msgs);
        render();
        return;
    }

    if (str_eq(cmd, "/new")) {
        blocks_clear();
        msglist_free(&app.agent.msgs);
        msglist_init(&app.agent.msgs);
        session_start(app.agent.cwd);
        notify("nova sessao %s", session_id());
        render();
        return;
    }

    if (str_eq(cmd, "/reload")) {
        char note[256] = { 0 };
        Config novo;
        config_load(&novo, note, sizeof note);
        config_free(&app.cfg);
        app.cfg = novo;
        Theme th;
        load_theme_named(app.theme_name ? app.theme_name : "mono", &th);
        theme_free(&app.theme);
        app.theme = th;
        app.tr_dirty = 1;
        notify("recarregado: %s", note);
        render();
        return;
    }

    if (str_eq(cmd, "/models") || str_eq(cmd, "/model")) {
        /* Recarrega o catalogo a cada /models (modelo novo pode ter saido no
           meio da sessao) e guarda SO' os provedores que o usuario conectou:
           o resto do models.dev (233 provedores) fica no cache para o /login. */
        {
            int forcado = getenv("DELTA_NO_CATALOG_REFRESH") ? 0 : 1;
            Block *b = block_add(B_INFO);
            long antes = app.catalog_loaded ? modelos_conectados() : 0;
            if (!forcado && app.catalog_loaded) {
                /* ambiente de teste/sem rede: usa o cache, mas filtra igual */
                catalog_keep(&app.catalog, keep_provider_conectado, NULL);
                app.catalog_filtrado = 1;
                long agora = modelos_conectados();
                str_addf(&b->text, "catalogo do cache \u00b7 %d provedor%s conectado%s, %ld modelos\n",
                         catalog_provedores_conectados(),
                         catalog_provedores_conectados() == 1 ? "" : "es",
                         catalog_provedores_conectados() == 1 ? "" : "s", agora);
            } else {
                str_adds(&b->text, "atualizando o catalogo do models.dev...");
                app.tr_dirty = 1;
                render();
                Str e; str_init(&e);
                int ok = catalog_load(&app.catalog, 1, &e);
                str_clear(&b->text);
                if (ok) {
                    app.catalog_loaded = 1;
                    size_t total_models = catalog_models_total(&app.catalog);
                    int conectados = catalog_keep(&app.catalog, keep_provider_conectado, NULL);
                    app.catalog_filtrado = 1;
                    long agora = modelos_conectados();
                    str_addf(&b->text, "catalogo atualizado · %d provedor%s conectado%s, %ld modelos",
                             conectados, conectados == 1 ? "" : "es",
                             conectados == 1 ? "" : "s", agora);
                    if (agora > 0 && (size_t)agora != total_models)
                        str_addf(&b->text, " (o models.dev publica %zu no total)", total_models);
                    if (antes > 0 && agora != antes)
                        str_addf(&b->text, " · %+ld desde o inicio da sessao", agora - antes);
                    str_addc(&b->text, 10);
                } else {
                    str_addf(&b->text, "nao consegui atualizar (%s)",
                             e.len ? e.data : "sem conexao");
                    if (catalog_load_cached(&app.catalog)) {
                        app.catalog_loaded = 1;
                        catalog_keep(&app.catalog, keep_provider_conectado, NULL);
                        app.catalog_filtrado = 1;
                        str_adds(&b->text, " · usando o cache local");
                    }
                    str_addc(&b->text, 10);
                }
                str_free(&e);
            }
            app.tr_dirty = 1;
            render();
            if (!app.catalog_loaded) return;
        }
        if (!*rest) {
            if (!catalog_ensure()) return;
            if (catalog_provedores_conectados() == 0) {
                /* catalogo filtrado sem ninguem: recarrega a lista cheia antes
                   de dizer que nao ha' provedor conectado */
                if (app.catalog_filtrado) {
                    catalog_load_cached(&app.catalog);
                    app.catalog_filtrado = 0;
                }
                notify("nenhum provedor conectado · use /login para ligar um");
                render();
                return;
            }
            open_picker(1);      /* modelos dos provedores conectados */
            return;
        }
        /* aceita "provedor/modelo" ou so o modelo do provedor atual */
        const char *slash = strchr(rest, '/');
        int aplicado = 0;
        if (slash) catalog_ensure_full();       /* pode ser de provedor nao conectado */
        if (slash && app.catalog_loaded) {
            char *prov = str_ndup(rest, (size_t)(slash - rest));
            if (catalog_provider(&app.catalog, prov)) {
                free(app.cfg.provider);
                app.cfg.provider = prov;
                const char *ep = catalog_endpoint(&app.catalog, app.cfg.provider);
                if (ep && *ep) { free(app.cfg.base_url); app.cfg.base_url = str_dup(ep); }
                free(app.cfg.model);
                app.cfg.model = str_dup(slash + 1);
                aplicado = 1;
            } else {
                free(prov);
            }
        }
        if (!aplicado) {
            if (app.catalog_loaded && app.cfg.provider &&
                !catalog_model(&app.catalog, app.cfg.provider, rest)) {
                notify("'%s' nao existe em %s (use /model para listar)", rest, app.cfg.provider);
                render();
                return;
            }
            free(app.cfg.model);
            app.cfg.model = str_dup(rest);
        }
        config_save(&app.cfg);
        refresh_api_key();
        refresh_model_caps();
        app.tr_dirty = 1;
        notify("modelo: %s/%s", app.cfg.provider ? app.cfg.provider : "?", app.cfg.model);
        render();
        return;
    }

    if (str_eq(cmd, "/login")) {
        if (*rest) {
            /* /login <provedor> vai direto para o campo da chave */
            if (!catalog_ensure()) return;
            const MdProvider *p = catalog_provider(&app.catalog, rest);
            if (!p) {
                for (size_t i = 0; i < app.catalog.n_providers; i++)
                    if (str_ieq(app.catalog.providers[i].name, rest)) { p = &app.catalog.providers[i]; break; }
            }
            if (!p) {
                notify("provedor '%s' nao encontrado (use /login para listar)", rest);
                render();
                return;
            }
            free(app.login_provider);
            app.login_provider = str_dup(p->id);
            app.picker = 3;
            str_clear(&app.picker_filter);
            render();
            return;
        }
        open_picker(2);
        return;
    }

    if (str_eq(cmd, "/logout")) {
        if (*rest) {
            if (auth_has_key(rest)) {
                auth_remove_key(rest);
                if (app.cfg.provider && str_eq(app.cfg.provider, rest)) refresh_api_key();
                notify("desconectado de %s", rest);
            } else {
                notify("%s nao tinha chave salva", rest);
            }
            render();
            return;
        }
        /* sem argumento: lista os provedores conectados para escolher */
        if (!catalog_ensure()) return;
        if (catalog_provedores_conectados() == 0) {
            notify("nenhum provedor conectado");
            render();
            return;
        }
        open_picker(5);
        return;
    }

    if (str_eq(cmd, "/provider")) {
        notify("use /login para escolher o provedor");
        render();
        return;
    }

    if (str_eq(cmd, "/baseurl")) {
        notify("o endpoint vem do catalogo; use config.json (baseUrl) para trocar");
        render();
        return;
    }

    if (str_eq(cmd, "/thinking")) {
        catalog_ensure_full();
        if (*rest) {
            for (int i = 0; i < N_THINKING; i++) {
                if (str_ieq(rest, THINKING_LEVELS[i].id)) {
                    free(app.cfg.thinking_level);
                    app.cfg.thinking_level = str_dup(THINKING_LEVELS[i].id);
                    config_save(&app.cfg);
                    notify("thinking: %s", THINKING_LEVELS[i].id);
                    render();
                    return;
                }
            }
            notify("nivel invalido (use /thinking para ver a lista)");
            render();
            return;
        }

        /* o seletor e do modelo selecionado: se ele nao anuncia raciocinio
           no models.dev, nao ha o que escolher.
           Aqui so o cache local e consultado (sem baixar nada). */
        if (!app.catalog_loaded && catalog_load_cached(&app.catalog)) app.catalog_loaded = 1;
        if (app.catalog_loaded && app.cfg.provider && app.cfg.model) {
            const MdModel *m = catalog_model(&app.catalog, app.cfg.provider, app.cfg.model);
            if (m && !m->reasoning) {
                free(app.cfg.thinking_level);
                app.cfg.thinking_level = str_dup("off");
                config_save(&app.cfg);
                notify("%s nao anuncia raciocinio no models.dev · thinking off", app.cfg.model);
                render();
                return;
            }
        }
        open_picker(4);
        return;
    }

    /* /verbosity: so' a prosa da resposta. O raciocinio (thinking) e o trabalho
       (tools, codigo, testes) nao mudam — menos texto nunca vira menos trabalho. */
    if (str_eq(cmd, "/verbosity")) {
        const char *atual = app.cfg.verbosity && *app.cfg.verbosity ? app.cfg.verbosity : "normal";
        if (*rest) {
            for (int i = 0; i < N_VERBOSITY; i++) {
                if (str_ieq(rest, VERBOSITY_LEVELS[i].id)) {
                    free(app.cfg.verbosity);
                    app.cfg.verbosity = str_dup(VERBOSITY_LEVELS[i].id);
                    config_save(&app.cfg);
                    agent_rebuild_prompt(&app.agent);   /* o prompt muda na hora */
                    notify("verbosity: %s", VERBOSITY_LEVELS[i].id);
                    render();
                    return;
                }
            }
            notify("nivel invalido (low|normal|high) · atual: %s", atual);
            render();
            return;
        }
        Block *b = block_add(B_INFO);
        str_adds(&b->text, "Verbosidade: " );
        str_adds(&b->text, atual);
        str_adds(&b->text, "  (o thinking e o trabalho nao mudam; so' a prosa)\n\n");
        for (int i = 0; i < N_VERBOSITY; i++)
            str_addf(&b->text, "  %-7s %s%s\n", VERBOSITY_LEVELS[i].id, VERBOSITY_LEVELS[i].desc,
                     str_eq(atual, VERBOSITY_LEVELS[i].id) ? "  <-" : "");
        str_adds(&b->text, "\n/verbosity low|normal|high\n");
        app.tr_dirty = 1;
        render();
        return;
    }

    if (str_eq(cmd, "/translator") || str_eq(cmd, "/language")) {
        if (!*rest) {
            Block *b = block_add(B_INFO);
            str_addf(&b->text, "%s:\n", tr("lang.title"));
            for (int i = 0; i < i18n_count(); i++)
                str_addf(&b->text, "  %-6s %s%s\n", i18n_code(i), i18n_name(i),
                         i == i18n_get() ? "  <-" : "");
            str_addf(&b->text, "\n/translator <codigo>  (ex.: /translator en)\n");
            app.tr_dirty = 1;
            render();
            return;
        }
        int lang = i18n_from_code(rest);
        if (lang < 0) {
            notify(tr("lang.unknown"), rest);
            render();
            return;
        }
        i18n_set(lang);
        free(app.cfg.language);
        app.cfg.language = str_dup(i18n_code(lang));
        config_save(&app.cfg);
        app.tr_dirty = 1;
        notify(tr("lang.set"), i18n_name(lang));
        render();
        return;
    }

    if (str_eq(cmd, "/extensions")) {
        if (*rest) {
            ext_criar_com_modelo(rest);
        } else {
            app.ext_mode = 1;
            app.ext_sel = 0;
            render();
        }
        return;
    }

    if (str_eq(cmd, "/skills")) {
        Block *b = block_add(B_INFO);
        char *idx = tools_skills_index(app.agent.cwd, 4000);
        if (idx && *idx) {
            str_adds(&b->text, "Skills (skills/, ~/.deltaCore/skills, ~/.agents/skills):\n");
            str_adds(&b->text, idx);
            str_adds(&b->text, "\nLeia uma com a tool skills (op=read).\n");
        } else {
            str_adds(&b->text, "Nenhuma skill encontrada (crie uma com a tool skills, op=create).");
        }
        free(idx);
        app.tr_dirty = 1;
        render();
        return;
    }

    if (str_eq(cmd, "/session")) {
        Block *b = block_add(B_INFO);
        str_addf(&b->text, "sessao   : %s\ncwd      : %s\nmensagens: %zu\n",
                 session_id(), app.agent.cwd, app.agent.msgs.n);
        app.tr_dirty = 1;
        render();
        return;
    }

    if (str_eq(cmd, "/status")) {
        Block *b = block_add(B_INFO);
        long ctx = 0;
        if (app.catalog_loaded) {
            const MdModel *m = catalog_model(&app.catalog, app.cfg.provider, app.cfg.model);
            if (m) ctx = m->ctx;
        }
        char chave[200];
        {
            const char *env = catalog_env_var(&app.catalog, app.cfg.provider);
            char *de_env = (env && *env) ? env_get(env) : NULL;
            if (de_env && *de_env)
                snprintf(chave, sizeof chave, "(ambiente: %s)", env);
            else if (auth_has_key(app.cfg.provider))
                snprintf(chave, sizeof chave, "(salva em ~/.deltaCore/auth.json)");
            else if (env && *env)
                snprintf(chave, sizeof chave, "(ausente - defina %s ou use /login)", env);
            else
                snprintf(chave, sizeof chave, "(ausente - use /login)");
            free(de_env);
        }
        str_addf(&b->text,
                 "provedor : %s\nmodelo   : %s\nendpoint : %s\nchave    : %s\nthinking : %s\nverbos.  : %s\ntema     : %s\n"
                 "contexto : %s%ld\nimagem   : %s\ncatalogo : %s\ncwd      : %s\nsessao   : %s\n",
                 app.cfg.provider ? app.cfg.provider : "?",
                 app.cfg.model ? app.cfg.model : "?",
                 app.cfg.base_url ? app.cfg.base_url : "?",
                 chave,
                 app.cfg.thinking_level ? app.cfg.thinking_level : "off",
                 app.cfg.verbosity && *app.cfg.verbosity ? app.cfg.verbosity : "normal",
                 app.theme_name ? app.theme_name : "mono",
                 ctx > 0 ? "" : "(desconhecido) ",
                 ctx,
                 app.cfg.supports_image ? "sim (Ctrl+V cola imagem)" : "nao",
                 app.catalog_loaded ? "models.dev carregado" : "nao carregado",
                 app.agent.cwd, session_id());
        app.tr_dirty = 1;
        render();
        return;
    }

    {
        char prompt[2048];
        char nome[96];
        snprintf(nome, sizeof nome, "%s", cmd + 1);
        if (ext_command_prompt(nome, prompt, sizeof prompt)) {
            str_clear(&app.input);
            str_adds(&app.input, prompt);
            app.cursor = app.input.len;
            submit_input();
            return;
        }
    }
    notify("comando desconhecido: %s (digite / para ver a lista)", cmd);
    render();
}

/* ================================================================== */
/* Editor                                                             */
/* ================================================================== */
static void input_insert(uint32_t cp)
{
    char buf[4];
    int n = utf8_encode(cp, buf);
    str_reserve(&app.input, (size_t)n);
    memmove(app.input.data + app.cursor + n, app.input.data + app.cursor, app.input.len - app.cursor);
    memcpy(app.input.data + app.cursor, buf, (size_t)n);
    app.input.len += (size_t)n;
    app.input.data[app.input.len] = 0;
    app.cursor += (size_t)n;
}

/* Insere texto UTF-8 na posicao do cursor. */
static void input_insert_text(const char *t, size_t n)
{
    str_reserve(&app.input, n);
    memmove(app.input.data + app.cursor + n, app.input.data + app.cursor,
            app.input.len - app.cursor);
    memcpy(app.input.data + app.cursor, t, n);
    app.input.len += n;
    app.input.data[app.input.len] = 0;
    app.cursor += n;
}

static void input_backspace(void)
{
    if (app.cursor == 0) return;
    /* se o cursor esta logo apos um {paste N caracteres}, apaga tudo de uma vez */
    if (app.input.data && app.cursor <= app.input.len && app.input.data[app.cursor - 1] == '}') {
        size_t ini = app.cursor - 1;
        while (ini > 0 && app.input.data[ini - 1] != '{') ini--;
        if (ini > 0 && memcmp(app.input.data + ini - 1, "{paste ", 7) == 0) {
            size_t start = ini - 1;
            memmove(app.input.data + start, app.input.data + app.cursor,
                    app.input.len - app.cursor);
            app.input.len -= (app.cursor - start);
            app.input.data[app.input.len] = 0;
            app.cursor = start;
            str_clear(&app.paste_text);
            return;
        }
    }
    size_t prev = app.cursor - 1;
    while (prev > 0 && ((unsigned char)app.input.data[prev] & 0xC0) == 0x80) prev--;
    size_t n = app.cursor - prev;
    memmove(app.input.data + prev, app.input.data + app.cursor, app.input.len - app.cursor);
    app.input.len -= n;
    app.input.data[app.input.len] = 0;
    app.cursor = prev;
}

static void input_delete(void)
{
    if (app.cursor >= app.input.len) return;
    size_t next = app.cursor + 1;
    while (next < app.input.len && ((unsigned char)app.input.data[next] & 0xC0) == 0x80) next++;
    size_t n = next - app.cursor;
    memmove(app.input.data + app.cursor, app.input.data + next, app.input.len - next);
    app.input.len -= n;
    app.input.data[app.input.len] = 0;
}

static void input_left(void)
{
    if (app.cursor == 0) return;
    size_t p = app.cursor - 1;
    while (p > 0 && ((unsigned char)app.input.data[p] & 0xC0) == 0x80) p--;
    app.cursor = p;
}

static void input_right(void)
{
    if (app.cursor >= app.input.len) return;
    size_t p = app.cursor + 1;
    while (p < app.input.len && ((unsigned char)app.input.data[p] & 0xC0) == 0x80) p++;
    app.cursor = p;
}

/* ================================================================== */
/* Kill ring e undo do editor                                          */
/* ================================================================== */
static Str  g_kill;              /* ultimo texto cortado (Ctrl+Y cola) */
static int  g_kill_ready = 0;

#define UNDO_MAX 64
static Str    g_undo[UNDO_MAX];
static size_t g_undo_cur[UNDO_MAX];
static int    g_undo_n;

/* Guarda (input, cursor) na pilha de undo. Chamar antes de editar. */
static void undo_push(void)
{
    if (g_undo_n == UNDO_MAX) {
        str_free(&g_undo[0]);
        memmove(&g_undo[0], &g_undo[1], sizeof(Str) * (UNDO_MAX - 1));
        memmove(&g_undo_cur[0], &g_undo_cur[1], sizeof(size_t) * (UNDO_MAX - 1));
        g_undo_n--;
    }
    Str s; str_init(&s);
    if (app.input.len) str_add(&s, app.input.data, app.input.len);
    g_undo[g_undo_n] = s;
    g_undo_cur[g_undo_n] = app.cursor;
    g_undo_n++;
}

static void undo_pop(void)
{
    if (g_undo_n == 0) return;
    g_undo_n--;
    str_clear(&app.input);
    if (g_undo[g_undo_n].len) str_add(&app.input, g_undo[g_undo_n].data, g_undo[g_undo_n].len);
    app.cursor = g_undo_cur[g_undo_n] <= app.input.len ? g_undo_cur[g_undo_n] : app.input.len;
    str_free(&g_undo[g_undo_n]);
}

static void kill_set(const char *s, size_t n)
{
    str_free(&g_kill);
    str_init(&g_kill);
    if (n) str_add(&g_kill, s, n);
    g_kill_ready = 1;
}

/* Remove [from,to) do editor, guardando o trecho no kill ring. */
static void input_kill(size_t from, size_t to)
{
    if (to <= from) return;
    if (from > app.input.len) from = app.input.len;
    if (to > app.input.len) to = app.input.len;
    undo_push();
    kill_set(app.input.data + from, to - from);
    memmove(app.input.data + from, app.input.data + to, app.input.len - to);
    app.input.len -= (to - from);
    app.input.data[app.input.len] = 0;
    app.cursor = from;
}

/* Inicio/fim da linha atual (multi-linha: o '
' e a fronteira). */
static size_t input_line_start(void)
{
    size_t p = app.cursor <= app.input.len ? app.cursor : app.input.len;
    while (p > 0 && app.input.data[p - 1] != '\n') p--;
    return p;
}

static size_t input_line_end(void)
{
    size_t p = app.cursor <= app.input.len ? app.cursor : app.input.len;
    while (p < app.input.len && app.input.data[p] != '\n') p++;
    return p;
}

/* Ctrl+U: apaga da linha ate o cursor. */
static void input_delete_to_line_start(void)
{
    input_kill(input_line_start(), app.cursor);
}

/* Ctrl+W: apaga a palavra anterior (espacos primeiro, depois a palavra). */
static void input_delete_word_backward(void)
{
    size_t p = app.cursor <= app.input.len ? app.cursor : app.input.len;
    if (p == 0) return;
    while (p > 0) {
        size_t prev = p - 1;
        while (prev > 0 && ((unsigned char)app.input.data[prev] & 0xC0) == 0x80) prev--;
        char c = app.input.data[prev];
        if (c == ' ' || c == '\n' || c == '\t') { p = prev; continue; }
        break;
    }
    while (p > 0) {
        size_t prev = p - 1;
        while (prev > 0 && ((unsigned char)app.input.data[prev] & 0xC0) == 0x80) prev--;
        char c = app.input.data[prev];
        if (c == ' ' || c == '\n' || c == '\t') break;
        p = prev;
    }
    input_kill(p, app.cursor);
}

/* Ctrl+Y: cola o ultimo trecho apagado. */
static void input_yank(void)
{
    if (!g_kill_ready || g_kill.len == 0) return;
    undo_push();
    input_insert_text(g_kill.data, g_kill.len);
}

/* ================================================================== */
/* Selecao no transcript + copia                                       */
/* ================================================================== */
/* Arrastar com o botao esquerdo seleciona; Ctrl+C copia (nada vai para o
   clipboard ao soltar); um clique sem arrasto limpa a selecao. A selecao
   e' pintada com fundo branco e letra preta, independente do tema. */
#define SEL_BG 0xFFFFFFu
#define SEL_FG 0x000000u

static void sel_clear(void)
{
    app.sel_active = 0;
}

static void sel_normalize(int *y0, int *x0, int *y1, int *x1)
{
    if (app.sel_ay > app.sel_hy ||
        (app.sel_ay == app.sel_hy && app.sel_ax > app.sel_hx)) {
        *y0 = app.sel_hy; *x0 = app.sel_hx; *y1 = app.sel_ay; *x1 = app.sel_ax;
    } else {
        *y0 = app.sel_ay; *x0 = app.sel_ax; *y1 = app.sel_hy; *x1 = app.sel_hx;
    }
}

static int sel_has_text(void)
{
    int y0, x0, y1, x1;
    sel_normalize(&y0, &x0, &y1, &x1);
    return y0 != y1 || x0 != x1;
}

/* Ultima linha (exclusiva) do bloco i no transcript. */
static int block_row_end(size_t i)
{
    int inicio = app.blocks[i].tr_y;
    if (i + 1 < app.n_blocks) {
        int fim = app.blocks[i + 1].tr_y;
        if (fim > inicio) return fim;
    }
    return app.tr_end_y > inicio ? app.tr_end_y : inicio + 1;
}

/* Texto "de verdade" de um bloco: sem moldura, sem wrap visual, sem o
   rodape de tokens. E' o que o usuario espera ao copiar uma mensagem. */
static void block_plain_text(const Block *b, Str *out)
{
    if (b->kind == B_TOOL) {
        if (b->tool_name && *b->tool_name) {
            str_adds(out, b->tool_name);
            if (b->tool_args && *b->tool_args) {
                str_addc(out, ' ');
                str_adds(out, b->tool_args);
            }
            if (b->tool_output.len) str_addc(out, '\n');
        }
        if (b->tool_output.len) str_add(out, b->tool_output.data, b->tool_output.len);
        return;
    }
    if (b->text.len) str_add(out, b->text.data, b->text.len);
}

static int eh_sem_conteudo(uint32_t cp)
{
    /* espaco, celula nunca escrita ou desenho de caixa (╭─│╰ etc.) */
    return cp == 0 || cp == ' ' || cp == '\t' || (cp >= 0x2500 && cp <= 0x257F);
}

static void sel_push_line(Str *out, const char *s, size_t n)
{
    if (n == 0) {
        if (out->len && out->data[out->len - 1] != '\n') str_addc(out, '\n');
        return;
    }
    if (out->len && out->data[out->len - 1] != '\n') str_addc(out, '\n');
    str_add(out, s, n);
    str_addc(out, '\n');
}

/* Trecho cru da grade (usado so' quando a selecao pega meia mensagem):
   tira a moldura e os espacos das pontas de cada linha. */
static void sel_grid_range(Str *out, int ya, int yb, int xa, int xb)
{
    int rows = app.tr.rows, cols = app.tr.cols;
    if (ya < 0) ya = 0;
    if (yb >= rows) yb = rows - 1;
    if (cols <= 0) return;
    for (int y = ya; y <= yb; y++) {
        int xs = (y == ya) ? xa : 0;
        int xe = (y == yb) ? xb : cols - 1;
        if (xs < 0) xs = 0;
        if (xe > cols - 1) xe = cols - 1;
        int first = -1, last = -1;
        for (int x = xs; x <= xe; x++) {
            uint32_t cp = app.tr.cp[(size_t)y * (size_t)cols + (size_t)x];
            if (!eh_sem_conteudo(cp)) {
                if (first < 0) first = x;
                last = x;
            }
        }
        if (first < 0) { sel_push_line(out, "", 0); continue; }
        Str line; str_init(&line);
        for (int x = first; x <= last; x++) {
            uint32_t cp = app.tr.cp[(size_t)y * (size_t)cols + (size_t)x];
            if (cp == 0) cp = ' ';
            char buf[4];
            int n = utf8_encode(cp, buf);
            str_add(&line, buf, (size_t)n);
        }
        sel_push_line(out, line.data, line.len);
        str_free(&line);
    }
}

/* Texto da selecao. Regra: mensagem inteira sai do modelo de blocos (texto
   original, sem quebras do wrap nem moldura de caixa); o que estiver cortado
   no meio sai da grade, limpo. */
static void selection_text(Str *out)
{
    int y0, x0, y1, x1;
    sel_normalize(&y0, &x0, &y1, &x1);
    if (app.tr.rows <= 0) return;
    if (y0 < 0) y0 = 0;
    if (y1 >= app.tr.rows) y1 = app.tr.rows - 1;
    if (y0 > y1) return;
    const int cols = app.tr.cols;

    size_t i = 0;
    int y = y0;
    while (y <= y1) {
        while (i < app.n_blocks && block_row_end(i) <= y) i++;
        if (i >= app.n_blocks) {          /* fim: cabecalho/linhas soltas */
            sel_grid_range(out, y, y1, (y == y0) ? x0 : 0, x1);
            break;
        }
        int bs = app.blocks[i].tr_y;
        int be = block_row_end(i);
        if (bs > y) {                     /* linhas antes do bloco */
            int ate = (bs - 1 > y1) ? y1 : bs - 1;
            sel_grid_range(out, y, ate, (y == y0) ? x0 : 0, (ate == y1) ? x1 : cols - 1);
            y = ate + 1;
            continue;
        }
        int cobre_tudo = (y <= bs) && (y1 >= be - 1) &&
                         (y > y0 || x0 == 0) &&
                         (y1 > be - 1 || x1 >= cols - 1);
        if (cobre_tudo) {
            Str t; str_init(&t);
            block_plain_text(&app.blocks[i], &t);
            if (t.len) {
                if (out->len && out->data[out->len - 1] != '\n') str_addc(out, '\n');
                str_add(out, t.data, t.len);
                if (out->data[out->len - 1] != '\n') str_addc(out, '\n');
            }
            str_free(&t);
            y = be;
            i++;
            continue;
        }
        int ate = (be - 1 > y1) ? y1 : be - 1;
        if (ate < y) { i++; continue; }
        sel_grid_range(out, y, ate, (y == y0) ? x0 : 0, (ate == y1) ? x1 : cols - 1);
        y = ate + 1;
    }

    /* sem linhas em branco nas pontas */
    while (out->len && (out->data[out->len - 1] == '\n' || out->data[out->len - 1] == ' '))
        str_truncate(out, out->len - 1);
    size_t ini = 0;
    while (ini < out->len && out->data[ini] == '\n') ini++;
    if (ini) {
        memmove(out->data, out->data + ini, out->len - ini);
        out->len -= ini;
        out->data[out->len] = 0;
    }
}

/* Pinta a selecao: fundo branco, letra preta (independe do tema). */
static void render_selection(void)
{
    if (!app.sel_active || !sel_has_text()) return;
    int y0, x0, y1, x1;
    sel_normalize(&y0, &x0, &y1, &x1);
    int cols = app.screen.cols;
    for (int sy = 0; sy < app.tr_view; sy++) {
        int ty = app.tr_start + sy;
        if (ty < y0 || ty > y1 || ty >= app.tr.rows) continue;
        int xs = (ty == y0) ? x0 : 0;
        int xe = (ty == y1) ? x1 : cols - 1;
        if (xs > xe) continue;
        if (xe > cols - 1) xe = cols - 1;
        for (int x = xs; x <= xe; x++) {
            size_t i = (size_t)sy * (size_t)cols + (size_t)x;
            app.screen.fg[i] = SEL_FG;
            app.screen.bg[i] = SEL_BG;
            app.screen.at[i] &= (uint8_t)~ATTR_INVERSE;
            app.screen.at[i] &= (uint8_t)~ATTR_DIM;
        }
    }
}

/* Ctrl+C com selecao: vai para o clipboard e avisa na borda do editor. */
static void copy_selection(void)
{
    Str t; str_init(&t);
    selection_text(&t);
    if (!t.len) {
        notify_status("nada selecionado");
        str_free(&t);
        return;
    }
    if (clipboard_write_text(t.data))
        notify_status("selecao copiada (%d caracteres)", (int)utf8_len(t.data));
    else
        notify_status("nao deu para copiar (clipboard ocupado?)");
    str_free(&t);
}

/* Roda do mouse. Um degrau por evento, igual com o agente trabalhando ou
   parado (antes a versao do "working" andava 1 linha e invertia o sentido). */
static void scroll_wheel(int shift)
{
    app.scroll += (shift > 0) ? 3 : -3;
    if (app.scroll < 0) app.scroll = 0;
}

/* ================================================================== */
/* Modo nao interativo                                                 */
/* ================================================================== */
static void print_text(void *ud, const char *s, size_t n)
{
    (void)ud;
    if (n) fwrite(s, 1, n, stdout);
    fflush(stdout);
}

static void print_thinking(void *ud, const char *s, size_t n)
{
    (void)ud;
    (void)s;
    (void)n;
}

static void print_tool_start(void *ud, const char *name, const char *args)
{
    (void)ud;
    fprintf(stderr, "\n[%s %s]\n", name, args);
    fflush(stderr);
}

static int run_print_mode(const char *prompt)
{
    /* "-p @arquivo" le o prompt do arquivo (usado pelo subagente: sem
       brigar com aspas/quebras de linha na linha de comando) */
    char *lido = NULL;
    if (prompt && prompt[0] == '@') {
        size_t n = 0;
        lido = file_read(prompt + 1, &n);
        if (lido) prompt = lido;
        else fprintf(stderr, "delta: nao consegui ler '%s'\n", prompt + 1);
    }
    Str err; str_init(&err);
    AgentCB cb;
    memset(&cb, 0, sizeof cb);
    cb.on_text = print_text;
    cb.on_thinking = print_thinking;
    cb.on_tool_start = print_tool_start;
    int ok = agent_run(&app.agent, prompt, &cb, &err);
    free(lido);
    if (!ok) {
        fprintf(stderr, "pi: %s\n", err.len ? err.data : "erro");
        str_free(&err);
        return 1;
    }
    printf("\n");
    str_free(&err);
    return 0;
}

/* ================================================================== */
/* Pre-visualizacao (testes visuais sem terminal interativo)           */
/* ================================================================== */
static void dump_screen_text(const Screen *s)
{
    for (int y = 0; y < s->rows; y++) {
        Str line; str_init(&line);
        uint32_t last_fg = 0xFFFFFFFE, last_bg = 0xFFFFFFFF;
        for (int x = 0; x < s->cols; x++) {
            size_t idx = (size_t)y * s->cols + x;
            uint32_t cp = s->cp[idx];
            if (cp == 0) continue;
            if (s->fg[idx] != last_fg || s->bg[idx] != last_bg) {
                Str esc; str_init(&esc);
                str_adds(&esc, "\x1b[0");
                if (s->fg[idx] != COL_DEFAULT)
                    str_addf(&esc, ";38;2;%u;%u;%u",
                             (s->fg[idx] >> 16) & 0xFF, (s->fg[idx] >> 8) & 0xFF, s->fg[idx] & 0xFF);
                if (getenv("DELTA_PREVIEW_BG") && s->bg[idx] != COL_DEFAULT)
                    str_addf(&esc, ";48;2;%u;%u;%u",
                             (s->bg[idx] >> 16) & 0xFF, (s->bg[idx] >> 8) & 0xFF, s->bg[idx] & 0xFF);
                str_addc(&esc, 'm');
                str_add(&line, esc.data, esc.len);
                str_free(&esc);
                last_fg = s->fg[idx];
                last_bg = s->bg[idx];
            }
            str_addcp(&line, cp);
        }
        printf("%s\x1b[0m\n", line.data ? line.data : "");
        str_free(&line);
    }
}

static int run_preview(int cols, int rows)
{
    app.screen.cols = 0;
    screen_init(&app.screen, cols, rows);
    screen_init(&app.tr, cols, 8);
    screen_set_grow(&app.tr, 1);

    agent_init(&app.agent, &app.cfg, app.agent.cwd ? app.agent.cwd : ".");

    {
        Block *b = block_add(B_USER);
        str_adds(&b->text, "Refatore src/util.c para usar buffers dinamicos e me explique o que mudou.");
    }
    {
        Block *b = block_add(B_ASSISTANT);
        str_adds(&b->text,
            "## Plano\n\n"
            "Vou trocar as alocacoes fixas por um `Str` com crescimento geometrico, e entao \n"
            "**atualizar** os chamadores. Pontos principais:\n\n"
            "- Crescimento com `realloc` dobrando a capacidade\n"
            "- Terminacao sempre garantida com `\\0`\n"
            "- Sem `strcpy` inseguro no caminho quente\n\n"
            "```c\nvoid str_add(Str *s, const char *bytes, size_t n) {\n    str_reserve(s, n);\n    memcpy(s->data + s->len, bytes, n);\n    s->len += n;\n    s->data[s->len] = 0;\n}\n```\n\n"
            "> O custo amortizado por byte continua O(1).\n\n"
            "Referencia: [documentacao](https://example.com/docs).\n");
    }
    {
        Block *b = block_add(B_TOOL);
        b->tool_name = str_dup("edit");
        b->tool_args = str_dup("{\"path\":\"src/util.c\",\"edits\":[]}");
        b->tool_state = 1;
        b->tool_secs = 0.02;
        str_adds(&b->tool_output,
                 "Editado src/util.c (2 alteracoes)\n"
                 "  static void str_reserve(Str *s, size_t extra)\n"
                 "-     size_t cap = s->cap ? s->cap : 64;\n"
                 "+     size_t cap = s->cap ? s->cap * 2 : 64;\n"
                 "  +     while (cap < need) cap *= 2;\n");
    }
    {
        Block *b = block_add(B_TOOL);
        b->tool_name = str_dup("bash");
        b->tool_args = str_dup("{\"command\":\"make test\"}");
        b->tool_state = 1;
        b->tool_secs = 0.31;
        str_adds(&b->tool_output, "gcc -std=c11 -O2 -Isrc ...\n36 verificacoes, 0 falhas\n");
    }

    render_transcript(cols, 0);

    int footer_h = 2;
    int editor_h = 3;
    int tr_view = rows - footer_h - editor_h;
    if (tr_view < 1) tr_view = 1;
    int total = app.tr.rows;
    int start = total > tr_view ? total - tr_view : 0;
    if (getenv("DELTA_PREVIEW_DEBUG"))
        fprintf(stderr, "[dbg] blocks=%zu tr.rows=%d tr.cap=%d tr.cols=%d total=%d view=%d start=%d\n",
                app.n_blocks, app.tr.rows, app.tr.cap_rows, app.tr.cols, total, tr_view, start);
    screen_blit(&app.screen, &app.tr, start, 0, tr_view);

    str_clear(&app.input);
    str_adds(&app.input, "Agora adicione testes para o caso de capacidade zero");
    app.cursor = app.input.len;
    app.working = 0;
    ui_editor(&app.screen, rows - footer_h - editor_h, cols, &app.theme,
              app.input.data, app.input.len, app.cursor, 0, "", 0, 0, 8, NULL, 0);

    {
        char pwd[512];
        snprintf(pwd, sizeof pwd, "~/Projects/demo");
        ui_footer(&app.screen, rows - footer_h, cols, &app.theme, pwd,
                  "up 12.4k  down 3.1k  6.2%/200k (auto)",
                  "(anthropic) claude-sonnet-4-5 • thinking low", NULL);
    }

    dump_screen_text(&app.screen);

    blocks_clear();
    screen_free(&app.screen);
    screen_free(&app.tr);
    return 0;
}

/* ================================================================== */
/* Tratamento de eventos (compartilhado entre a TUI e o modo replay)   */
/* ================================================================== */
static void filter_insert(uint32_t cp)
{
    char buf[4];
    int n = utf8_encode(cp, buf);
    str_add(&app.picker_filter, buf, (size_t)n);
}

static void filter_backspace(void)
{
    if (app.picker_filter.len == 0) return;
    size_t i = app.picker_filter.len - 1;
    while (i > 0 && ((unsigned char)app.picker_filter.data[i] & 0xC0) == 0x80) i--;
    str_truncate(&app.picker_filter, i);
}

/* Cola texto no campo do seletor (filtro ou chave de API). Sem isso, o
   Ctrl+V do /login caia no editor de prompt e a chave nunca entrava. */
static void picker_insert_text(const char *t)
{
    if (!t || !*t) return;
    if (app.picker == 3) {
        /* chave de API e' uma linha so': nada de espacos, \r ou \n */
        size_t len = strlen(t);
        for (const char *p = t; *p; ) {
            uint32_t cp = 0;
            int n = utf8_decode(p, len - (size_t)(p - t), &cp);
            if (n <= 0) break;
            if (cp != ' ' && cp != '\n' && cp != '\r' && cp != '\t') filter_insert(cp);
            p += n;
        }
    } else {
        for (const char *p = t; *p; ) {
            uint32_t cp = 0;
            int n = utf8_decode(p, strlen(p), &cp);
            if (n <= 0) break;
            filter_insert(cp);
            p += n;
        }
    }
    app.menu_sel = 0;
    app.menu_scroll = 0;
}

/* Cola do clipboard no campo do seletor, se houver texto. 1 = colou. */
static int picker_paste_from_clipboard(void)
{
    if (!app.picker) return 0;
    Clip c;
    char *dir = paste_dir();
    int ok = 0;
    if (clipboard_read(&c, dir) && c.kind == CLIP_TEXT && c.text) {
        picker_insert_text(c.text);
        ok = 1;
    }
    clip_free(&c);
    free(dir);
    return ok;
}

static void handle_event(const Event *ev)
{
    /* painel de extensoes tem prioridade nas teclas */
    if (app.ext_mode) {
        int n = ext_count();
        if (ev->type == EV_UP) { if (app.ext_sel > 0) app.ext_sel--; render(); return; }
        if (ev->type == EV_DOWN) { if (app.ext_sel < n - 1) app.ext_sel++; render(); return; }
        if (ev->type == EV_ESC || (ev->type == EV_CTRL && ev->cp == 'c')) {
            app.ext_mode = 0; render(); return;
        }
        if (ev->type == EV_ENTER) {
            char nome[64], kind[16];
            int en = 1;
            if (ext_get(app.ext_sel, nome, sizeof nome, kind, sizeof kind, &en)) {
                ext_set_enabled(nome, !en);
                ext_reload();
                notify("%s: %s", en ? "desligada" : "ligada", nome);
            }
            render();
            return;
        }
        return;
    }

    int n_items = (int)g_items_n;
    if (ev->type != EV_CTRL_KEY && ev->type != EV_RESIZE) app.ctrl_key_ms = 0;

    /* -------- mouse: arrasto seleciona; clique limpa; roda rola a conversa -------- */
    if (ev->type == EV_DRAG) {
        if (app.menu_visible || app.picker) return;
        if (ev->my < 0 || ev->my >= app.tr_view) return;
        int ty = app.tr_start + ev->my;
        if (ty >= app.tr.rows || app.tr.cols <= 0) return;
        int tx = ev->mx < 0 ? 0 : (ev->mx >= app.tr.cols ? app.tr.cols - 1 : ev->mx);
        if (!app.sel_active) {
            app.sel_active = 1;
            app.sel_ax = app.sel_hx = tx;
            app.sel_ay = app.sel_hy = ty;
            render();
            return;
        }
        if (tx != app.sel_hx || ty != app.sel_hy) {
            app.sel_hx = tx;
            app.sel_hy = ty;
            render();
        }
        return;
    }
    if (ev->type == EV_RELEASE) return;   /* soltar nao copia: o Ctrl+C copia */
    if (ev->type == EV_WHEEL) {
        int n = (int)g_items_n;
        if ((app.menu_visible || app.picker) && n > 0) {
            app.menu_sel += (ev->shift > 0) ? -1 : 1;
            if (app.menu_sel < 0) app.menu_sel = 0;
            if (app.menu_sel >= n) app.menu_sel = n - 1;
        } else {
            scroll_wheel(ev->shift);   /* roda: cima = mais antigo */
        }
        render();
        return;
    }
    if (ev->type == EV_CLICK) {
        int n = (int)g_items_n;
        if ((app.menu_visible || app.picker) && n > 0 &&
            ev->my >= app.menu_y0 && ev->my < app.menu_y0 + app.menu_rows) {
            int idx = app.menu_scroll + (ev->my - app.menu_y0);
            if (idx >= 0 && idx < n) {
                if (idx == app.menu_sel) menu_apply();   /* clicar de novo confirma */
                else app.menu_sel = idx;
            }
            render();
            return;
        }
        /* clique no transcript: limpa a selecao (nao seleciona mensagem) */
        if (!app.menu_visible && !app.picker && app.sel_active) {
            sel_clear();
            render();
        }
        return;
    }

    /* -------- Esc/Ctrl+C sempre cancelam o seletor (mesmo sem resultados) -------- */
    if (app.picker && (ev->type == EV_ESC || (ev->type == EV_CTRL && ev->cp == 'c'))) {
        app.picker = 0;
        str_clear(&app.picker_filter);
        set_status("cancelado");
        render();
        return;
    }

    /* -------- menu de comandos / seletor: consome a navegacao -------- */
    if (n_items > 0 && (app.menu_visible || app.picker)) {
        switch (ev->type) {
            case EV_UP:
                app.menu_sel = app.menu_sel > 0 ? app.menu_sel - 1 : n_items - 1;
                render();
                return;
            case EV_DOWN:
                app.menu_sel = app.menu_sel + 1 < n_items ? app.menu_sel + 1 : 0;
                render();
                return;
            case EV_PGUP:
                app.menu_sel -= 8;
                if (app.menu_sel < 0) app.menu_sel = 0;
                render();
                return;
            case EV_PGDN:
                app.menu_sel += 8;
                if (app.menu_sel >= n_items) app.menu_sel = n_items - 1;
                render();
                return;
            case EV_ENTER:
                menu_apply();
                render();
                return;
            case EV_TAB:
                if (app.picker) menu_apply();
                else menu_complete();
                render();
                return;
            case EV_ESC:
                app.menu_suppressed = 1;
                render();
                return;
            case EV_CTRL:
                break;
            default:
                break;
        }
    }

    /* -------- digitando a chave do /login -------- */
    if (app.picker == 3) {
        switch (ev->type) {
            case EV_ENTER:
                menu_apply();
                render();
                return;
            case EV_ESC:
                app.picker = 0;
                str_clear(&app.picker_filter);
                set_status("cancelado");
                render();
                return;
            case EV_CHAR:
                filter_insert(ev->cp);
                render();
                return;
            case EV_BACKSPACE:
            case EV_DELETE:
                filter_backspace();
                render();
                return;
            case EV_CTRL:
                if (ev->cp == 'c') {
                    app.picker = 0;
                    str_clear(&app.picker_filter);
                    set_status("cancelado");
                    render();
                    return;
                }
                /* Ctrl+V colando a chave de API: le o clipboard e insere no
                   campo (o console nem sempre cola texto sozinho aqui) */
                if (ev->cp == 'v' && now_ms() - app.last_clip_ms >= 300) {
                    app.last_clip_ms = now_ms();
                    if (picker_paste_from_clipboard())
                        notify("chave colada do clipboard");
                    else
                        notify("clipboard sem texto");
                    render();
                    return;
                }
                break;
            default:
                break;
        }
    }

    /* -------- no seletor, o texto digitado e o filtro -------- */
    if (app.picker) {
        switch (ev->type) {
            case EV_CHAR:
                filter_insert(ev->cp);
                app.menu_sel = 0;
                app.menu_scroll = 0;
                render();
                return;
            case EV_BACKSPACE:
            case EV_DELETE:
                filter_backspace();
                app.menu_sel = 0;
                app.menu_scroll = 0;
                render();
                return;
            case EV_CTRL:
                /* Ctrl+V no seletor (models/login/logout): cola no filtro */
                if (ev->cp == 'v' && now_ms() - app.last_clip_ms >= 300) {
                    app.last_clip_ms = now_ms();
                    if (picker_paste_from_clipboard())
                        notify("colado do clipboard");
                    else
                        notify("clipboard sem texto");
                    render();
                    return;
                }
                break;
            default:
                break;
        }
    }

    switch (ev->type) {
            case EV_RESIZE:
                render();
                break;
            case EV_CHAR:
                input_insert(ev->cp);
                app.menu_suppressed = 0;
                app.menu_sel = 0;
                app.menu_scroll = 0;
                render();
                break;
            case EV_ENTER:
                if (ev->shift) {
                    input_insert('\n');
                    render();
                } else if (app.working) {
                    enqueue_input();      /* vai para a fila; /stop interrompe */
                } else {
                    submit_input();
                }
                break;
            case EV_BACKSPACE:
                input_backspace();
                app.menu_suppressed = 0;
                app.menu_sel = 0;
                render();
                break;
            case EV_DELETE:    input_delete(); render(); break;
            case EV_LEFT:      input_left(); render(); break;
            case EV_RIGHT:     input_right(); render(); break;
            case EV_HOME:      app.cursor = 0; render(); break;
            case EV_END:
                if (app.scroll > 0) app.scroll = 0;
                else app.cursor = app.input.len;
                render();
                break;
            case EV_UP:        app.scroll += 1; render(); break;
            case EV_DOWN:      app.scroll -= 1; if (app.scroll < 0) app.scroll = 0; render(); break;
            case EV_PGUP:      app.scroll += 5; render(); break;
            case EV_PGDN:      app.scroll -= 5; if (app.scroll < 0) app.scroll = 0; render(); break;
            case EV_TAB:
                input_insert(' '); input_insert(' '); input_insert(' '); input_insert(' ');
                render();
                break;
            case EV_ESC:
                /* cascata: selecao, rolagem, e so' no fim o texto do editor
                   (antes o ESC fazia tudo de uma vez e apagava o que estava
                   sendo digitado) */
                if (app.sel_active) { sel_clear(); render(); break; }
                if (app.scroll > 0) { app.scroll = 0; render(); break; }
                if (app.input.len) { str_clear(&app.input); app.cursor = 0; }
                app.menu_suppressed = 0;
                render();
                break;
            case EV_CTRL_KEY:
                /* Ctrl sozinho: pode ser um Ctrl+V cujo texto o console ja colou,
                   ou uma colagem de imagem/arquivo (que nao gera evento). */
                app.ctrl_key_ms = now_ms();
                render();
                break;
            case EV_CTRL:
                if (ev->cp == 'v') {
                    /* o fallback por GetAsyncKeyState pode ter pegado o
                       mesmo Ctrl+V antes; cola so uma vez */
                    if (now_ms() - app.last_clip_ms < 300) break;
                    app.last_clip_ms = now_ms();
                    Clip c;
                    char *dir = paste_dir();
                    if (clipboard_read(&c, dir)) inserir_colagem(&c);
                    else notify("clipboard sem texto, imagem ou arquivo");
                    clip_free(&c);
                    free(dir);
                    render();
                } else if (ev->cp == 'c') {
                    /* Ctrl+C nunca fecha o app: com selecao no transcript,
                       copia; sem selecao, interrompe a geracao ou limpa o
                       editor. Sair e' /quit ou Ctrl+D. */
                    if (app.sel_active && sel_has_text()) {
                        copy_selection();
                        render();
                        break;
                    }
                    if (app.working) {
                        app.abort_requested = 1;
                        notify("interrompendo...");
                    } else if (app.input.len) {
                        str_clear(&app.input);
                        app.cursor = 0;
                    } else {
                        notify("nada rodando \u00b7 use /quit ou Ctrl+D para sair");
                    }
                    render();
                } else if (ev->cp == 'd') {
                    if (app.input.len == 0) app.quit = 1;
                    render();
                } else if (ev->cp == 'l') {
                    app.tr_dirty = 1;
                    render();
                } else if (ev->cp == 'a') { app.cursor = 0; render(); }
                else if (ev->cp == 'e') { app.cursor = app.input.len; render(); }
                else if (ev->cp == 'u') { input_delete_to_line_start(); render(); }
                else if (ev->cp == 'k') { input_kill(app.cursor, input_line_end()); render(); }
                else if (ev->cp == 'w') { input_delete_word_backward(); render(); }
                else if (ev->cp == 'y') { input_yank(); render(); }
                else if (ev->cp == 'z') { undo_pop(); render(); }
                else if (ev->cp == 'q') {
                    /* devolve a ultima mensagem da fila para o editor */
                    if (g_fila_n > 0) {
                        Str t; str_init(&t);
                        char *imgs[8]; int n_imgs = 0;
                        size_t blk = 0;
                        if (fila_pop_last(&t, imgs, &n_imgs, &blk)) {
                            if (blk < app.n_blocks && app.blocks[blk].kind == B_USER)
                                app.blocks[blk].queued = 2;   /* devolvida ao editor */
                            app.tr_dirty = 1;
                            str_free(&app.paste_text);
                            str_clear(&app.input);
                            if (t.len) str_add(&app.input, t.data, t.len);
                            app.cursor = app.input.len;
                            for (int i = 0; i < n_imgs; i++) {
                                if (app.n_imgs < 8) app.imgs[app.n_imgs++] = imgs[i];
                                else free(imgs[i]);
                            }
                            notify("mensagem devolvida ao editor (%d na fila)", g_fila_n);
                        }
                        str_free(&t);
                    } else {
                        notify("a fila esta vazia");
                    }
                    render();
                }
                break;
            default:
                break;
    }
}

/* ================================================================== */
/* Colagem                                                             */
/* ================================================================== */
/* Onde as imagens coladas sao gravadas. */
static char *paste_dir(void)
{
    char *home = home_get();
    char *dir = path_join(delta_home(), "paste");
    free(home);
    dir_make(dir);
    return dir;
}
/* Um lote de eventos = colagem: vira texto de uma vez, sem submeter. */
/* Texto de colagem pronto (a rajada inteira, ja com \n das quebras). */
static void handle_paste_text(const char *t)
{
    if (!t || !*t) return;

    /* o console pode ter entregado a mesma colagem que o Ctrl+V ja inseriu */
    if (app.ultimo_colado.len && strlen(t) == app.ultimo_colado.len &&
        memcmp(t, app.ultimo_colado.data, app.ultimo_colado.len) == 0 &&
        now_ms() - app.ultimo_colado_ms < 400) return;

    /* colando dentro do /login ou do seletor: vai para o campo dele, nao
       para o editor de prompt (era isso que impedia colar a chave) */
    if (app.picker) {
        picker_insert_text(t);
        render();
        return;
    }

    Clip c;
    clip_init(&c);
    c.kind = CLIP_TEXT;
    c.text = (char *)t;              /* empresta; o Clip nao libera text aqui */
    inserir_colagem(&c);
    c.text = NULL;
    clip_free(&c);
    app.menu_sel = 0;
}

static void handle_paste_batch(const Event *evs, int n)
{
    Str t; str_init(&t);
    for (int i = 0; i < n; i++) {
        if (evs[i].type == EV_CHAR) str_addcp(&t, evs[i].cp);
        else if (evs[i].type == EV_ENTER) str_addc(&t, '\n');
        else if (evs[i].type == EV_TAB) str_adds(&t, "    ");
    }
    handle_paste_text(t.data);
    str_free(&t);
    render();
}

/* Soma um lote de eventos ao texto de colagem acumulado. Colagens grandes
   chegam em varios lotes de ate 512 eventos; se juntar tudo antes de virar
   {paste N caracteres}, o fim do texto nao se perde e o ENTER final nao
   dispara envio sozinho. Eventos que nao sao colagem (mouse, teclas) sao
   processados normalmente; o texto pendente entra antes, na ordem. */
static void paste_stream(const Event *evs, int n, Str *acc)
{
    int nch = 0;
    for (int i = 0; i < n; i++)
        if (evs[i].type == EV_CHAR || evs[i].type == EV_ENTER || evs[i].type == EV_TAB) nch++;
    if (nch == 0) { for (int i = 0; i < n; i++) handle_event(&evs[i]); return; }
    for (int i = 0; i < n; i++) {
        const Event *e = &evs[i];
        if (e->type == EV_CHAR)  { str_addcp(acc, e->cp); continue; }
        if (e->type == EV_ENTER) { str_addc(acc, '\n'); continue; }
        if (e->type == EV_TAB)   { str_adds(acc, "    "); continue; }
        if (acc->len) { handle_paste_text(acc->data); str_clear(acc); }
        handle_event(e);
    }
}

/* Insere o que veio do clipboard: texto vira sempre {paste N caracteres}. */
static void inserir_colagem(const Clip *c)
{
    /* A mesma colagem pode chegar por dois caminhos (o console colando o texto
       e o Ctrl+V lendo o clipboard, ou os dois fallbacks de imagem). Se o
       conteudo e' o mesmo de agora ha' pouco, entra uma vez so'. */
    if (c->kind == CLIP_TEXT && c->text) {
        if (app.ultimo_colado.len && strlen(c->text) == app.ultimo_colado.len &&
            memcmp(c->text, app.ultimo_colado.data, app.ultimo_colado.len) == 0 &&
            now_ms() - app.ultimo_colado_ms < 400) return;
        size_t chars = utf8_len(c->text);
        char ph[64];
        snprintf(ph, sizeof ph, "{paste %zu caracteres}", chars);
        str_free(&app.paste_text);
        str_init(&app.paste_text);
        str_adds(&app.paste_text, c->text);
        str_free(&app.ultimo_colado);
        str_init(&app.ultimo_colado);
        str_adds(&app.ultimo_colado, c->text);
        app.ultimo_colado_ms = now_ms();
        input_insert_text(ph, strlen(ph));
    } else if (c->kind == CLIP_IMAGE && c->path) {
        if (app.ultimo_colado_img.len && str_eq(c->path, app.ultimo_colado_img.data) &&
            now_ms() - app.ultimo_colado_img_ms < 400) return;
        char limpo[600];
        snprintf(limpo, sizeof limpo, "%s", c->path);
#ifdef _WIN32
        for (char *q = limpo; *q; q++) if (*q == '/') *q = '\\';
#endif
        str_free(&app.ultimo_colado_img);
    str_free(&app.plan);
        str_init(&app.ultimo_colado_img);
    str_init(&app.plan);
        str_adds(&app.ultimo_colado_img, c->path);
        app.ultimo_colado_img_ms = now_ms();
        if (app.n_imgs < 8) app.imgs[app.n_imgs++] = str_dup(limpo);
        input_insert_text(limpo, strlen(limpo));
        input_insert_text(" ", 1);
        if (!app.cfg.supports_image)
            notify("%s nao aceita imagem: so o caminho vai no texto (use /models para um com visao)",
                   app.cfg.model ? app.cfg.model : "o modelo");
    } else if (c->kind == CLIP_FILES && c->path) {
        char buf[1400];
        snprintf(buf, sizeof buf, "[%s: %s] ", c->is_video ? "video" : "arquivo", c->path);
        input_insert_text(buf, strlen(buf));
    }
    app.menu_suppressed = 0;
}

/* Troca o placeholder pelo texto colado na hora de enviar. */
static void expand_paste(Str *text)
{
    if (!app.paste_text.data || app.paste_text.len == 0) return;
    const char *p = strstr(text->data ? text->data : "", "{paste ");
    if (!p) return;
    const char *fim = strchr(p, '}');
    if (!fim) return;
    size_t antes = (size_t)(p - text->data);
    size_t depois = text->len - (size_t)(fim - text->data) - 1;
    Str novo; str_init(&novo);
    str_add(&novo, text->data, antes);
    str_add(&novo, app.paste_text.data, app.paste_text.len);
    str_add(&novo, fim + 1, depois);
    str_free(text);
    *text = novo;
    str_clear(&app.paste_text);
}

/* ================================================================== */
/* Modo replay: roda a aplicacao real sem terminal e despeja a tela    */
/* ================================================================== */
static void dump_screen_plain(const Screen *s, FILE *f)
{
    for (int y = 0; y < s->rows; y++) {
        Str line; str_init(&line);
        for (int x = 0; x < s->cols; x++) {
            uint32_t cp = s->cp[(size_t)y * s->cols + x];
            if (cp == 0) continue;
            str_addcp(&line, cp);
        }
        while (line.len && line.data[line.len - 1] == ' ') line.data[--line.len] = 0;
        fprintf(f, "|%s|\n", line.data ? line.data : "");
        str_free(&line);
    }
}

static int key_from_name(const char *name, Event *ev)
{
    memset(ev, 0, sizeof(*ev));
    if (str_ieq(name, "enter") || str_ieq(name, "return")) { ev->type = EV_ENTER; return 1; }
    if (str_ieq(name, "shift-enter")) { ev->type = EV_ENTER; ev->shift = 1; return 1; }
    if (str_ieq(name, "tab")) { ev->type = EV_TAB; return 1; }
    if (str_ieq(name, "esc") || str_ieq(name, "escape")) { ev->type = EV_ESC; return 1; }
    if (str_ieq(name, "backspace")) { ev->type = EV_BACKSPACE; return 1; }
    if (str_ieq(name, "delete")) { ev->type = EV_DELETE; return 1; }
    if (str_ieq(name, "left")) { ev->type = EV_LEFT; return 1; }
    if (str_ieq(name, "right")) { ev->type = EV_RIGHT; return 1; }
    if (str_ieq(name, "up")) { ev->type = EV_UP; return 1; }
    if (str_ieq(name, "down")) { ev->type = EV_DOWN; return 1; }
    if (str_ieq(name, "home")) { ev->type = EV_HOME; return 1; }
    if (str_ieq(name, "end")) { ev->type = EV_END; return 1; }
    if (str_ieq(name, "pageup")) { ev->type = EV_PGUP; return 1; }
    if (str_ieq(name, "pagedown")) { ev->type = EV_PGDN; return 1; }
    if (str_ieq(name, "resize")) { ev->type = EV_RESIZE; return 1; }
    if (str_ieq(name, "wheel-up"))   { ev->type = EV_WHEEL; ev->shift = 1; return 1; }
    if (str_ieq(name, "wheel-down")) { ev->type = EV_WHEEL; ev->shift = -1; return 1; }
    if (str_ieq(name, "ctrl-c")) { ev->type = EV_CTRL; ev->cp = 'c'; return 1; }
    if (str_ieq(name, "ctrl-d")) { ev->type = EV_CTRL; ev->cp = 'd'; return 1; }
    if (str_ieq(name, "ctrl-l")) { ev->type = EV_CTRL; ev->cp = 'l'; return 1; }
    if (str_ieq(name, "ctrl-u")) { ev->type = EV_CTRL; ev->cp = 'u'; return 1; }
    if (str_ieq(name, "ctrl-v")) { ev->type = EV_CTRL; ev->cp = 'v'; return 1; }
    if (str_ieq(name, "ctrl-k")) { ev->type = EV_CTRL; ev->cp = 'k'; return 1; }
    if (str_ieq(name, "ctrl-w")) { ev->type = EV_CTRL; ev->cp = 'w'; return 1; }
    if (str_ieq(name, "ctrl-y")) { ev->type = EV_CTRL; ev->cp = 'y'; return 1; }
    if (str_ieq(name, "ctrl-z")) { ev->type = EV_CTRL; ev->cp = 'z'; return 1; }
    if (str_ieq(name, "ctrl-x")) { ev->type = EV_CTRL; ev->cp = 'x'; return 1; }
    if (str_ieq(name, "ctrl-q")) { ev->type = EV_CTRL; ev->cp = 'q'; return 1; }
    return 0;
}

static int run_replay(const char *path, int cols, int rows, int force_offline)
{
    if (force_offline) {
        free(app.cfg.api_key);
        app.cfg.api_key = NULL;
    }
    term_set_size(cols, rows);
    screen_init(&app.screen, cols, rows);
    screen_init(&app.tr, cols, 8);
    screen_set_grow(&app.tr, 1);
    app.tr_dirty = 1;
    render();

    size_t len = 0;
    char *text = file_read(path, &len);
    if (!text) {
        fprintf(stderr, "delta: nao consegui ler o roteiro '%s'\n", path);
        return 1;
    }

    int step = 0;
    size_t i = 0;
    while (i < len && !app.quit) {
        size_t eol = i;
        while (eol < len && text[eol] != '\n') eol++;
        size_t ll = eol - i;
        while (ll > 0 && text[i + ll - 1] == '\r') ll--;
        char *line = str_ndup(text + i, ll);
        i = eol + 1;

        char *space = strchr(line, ' ');
        char *arg = NULL;
        if (space) { *space = 0; arg = space + 1; }

        if (!*line || line[0] == '#') { free(line); continue; }
        step++;

        if (str_eq(line, "type")) {
            if (!arg) arg = (char *)"";
            for (size_t k = 0; arg[k]; ) {
                uint32_t cp = 0;
                int n = utf8_decode(arg + k, strlen(arg) - k, &cp);
                if (n <= 0) break;
                Event ev;
                memset(&ev, 0, sizeof ev);
                ev.type = EV_CHAR;
                ev.cp = cp;
                handle_event(&ev);
                k += (size_t)n;
            }
        } else if (str_eq(line, "paste")) {
            /* simula colagem: um lote de eventos de uma vez */
            if (!arg) arg = (char *)"";
            size_t total = 0;
            for (size_t k = 0; arg[k]; ) {
                uint32_t cp = 0;
                int c = utf8_decode(arg + k, strlen(arg) - k, &cp);
                if (c <= 0) break;
                total++;
                k += (size_t)c;
            }
            Event *lote = (Event *)calloc(total + 1, sizeof(Event));
            size_t m = 0;
            for (size_t k = 0; arg[k]; ) {
                uint32_t cp = 0;
                int c = utf8_decode(arg + k, strlen(arg) - k, &cp);
                if (c <= 0) break;
                lote[m].type = (cp == ' ') ? EV_CHAR : EV_CHAR;
                lote[m].cp = cp;
                m++;
                k += (size_t)c;
            }
            if (m) handle_paste_batch(lote, (int)m);
            free(lote);
        } else if (str_eq(line, "key")) {
            Event ev;
            if (arg && key_from_name(arg, &ev)) handle_event(&ev);
            else fprintf(stderr, "replay: tecla desconhecida '%s'\n", arg ? arg : "");
        } else if (str_eq(line, "working")) {
            /* working 1|0 - simula o agente ocupado (testa a fila) */
            app.working = (arg && arg[0] == '1') ? 1 : 0;
            if (app.working) str_adds(&app.work_msg, "Working...");
            render();
        } else if (str_eq(line, "drain")) {
            /* intervalo entre steps: o agente le a fila (cb_on_interval) */
            cb_on_interval(NULL);
            render();
        } else if (str_eq(line, "idle")) {
            /* idle <ms> - espera sem mandar input nenhum (a TUI tem que
               continuar viva depois de ficar parada) */
            int ms = arg ? atoi(arg) : 1000;
            if (ms <= 0) ms = 1000;
            msleep(ms);
        } else if (str_eq(line, "benchmd")) {
            /* benchmd <n> <bytes> - custo do layout de markdown isolado */
            int rep = 50, bytes = 4096;
            if (arg) sscanf(arg, "%d %d", &rep, &bytes);
            if (rep <= 0) rep = 50;
            if (bytes < 64) bytes = 64;
            Str t; str_init(&t);
            str_adds(&t, "# Titulo\n\n");
            while ((int)t.len < bytes) str_adds(&t, "palavra ");
            str_adds(&t, "\n\n- **um**\n- _dois_\n");
            Style base = { COL_DEFAULT, COL_DEFAULT, 0 };
            uint64_t b0 = now_ms();
            for (int k = 0; k < rep; k++) {
                Doc d; md_layout(&app.theme, t.data, t.len, 106, base, &d);
                doc_free(&d);
            }
            uint64_t dt = now_ms() - b0;
            printf("benchmd: %d x %zu bytes em %llu ms (%.2f ms/vez)\n",
                   rep, t.len, (unsigned long long)dt, (double)dt / (double)rep);
            str_free(&t);
        } else if (str_eq(line, "steer-at")) {
            /* steer-at <n> <texto> - simula digitar/Enter no pedaco n do stream */
            int n = 0, off = 0;
            if (arg && sscanf(arg, "%d %n", &n, &off) >= 1 && n > 0) {
                g_replay_steer_at = n;
                g_replay_chunks = 0;
                snprintf(g_replay_steer_text, sizeof g_replay_steer_text, "%s", arg + off);
            } else {
                fprintf(stderr, "replay: steer-at <pedaco> <texto>\n");
            }
        } else if (str_eq(line, "bencht")) {
            /* bencht <n> - so' o transcript (isola do resto do frame) */
            int rep = arg ? atoi(arg) : 100;
            if (rep <= 0) rep = 100;
            size_t from = app.n_blocks > 1 ? app.n_blocks - 2 : 0;
            uint64_t b0 = now_ms();
            for (int k = 0; k < rep; k++) { app.tr_fast = 1; render_transcript(app.tr_width ? app.tr_width : 100, from); }
            uint64_t dt = now_ms() - b0;
            printf("bencht: %d frames em %llu ms (%.2f ms/frame)\n",
                   rep, (unsigned long long)dt, (double)dt / (double)rep);
        } else if (str_eq(line, "bench")) {
            /* bench <n> [full] - mede o custo de um frame */
            int rep = arg ? atoi(arg) : 100;
            int full = strstr(arg ? arg : "", "full") != NULL;
            if (rep <= 0) rep = 100;
            uint64_t b0 = now_ms();
            for (int k = 0; k < rep; k++) {
                if (full) { app.tr_dirty = 1; app.tr_done = 0; }
                else { app.tr_fast = 1; app.tr_fast_from = app.n_blocks > 1 ? app.n_blocks - 2 : 0; }
                render();
            }
            uint64_t dt = now_ms() - b0;
            printf("bench %s: %d frames em %llu ms (%.2f ms/frame, %zu blocos)\n",
                   full ? "FULL" : "fast", rep, (unsigned long long)dt,
                   (double)dt / (double)rep, app.n_blocks);
        } else if (str_eq(line, "mouse")) {
            /* mouse <x> <y> click|drag|release - simula o mouse */
            int mx = 0, my = 0;
            char what[16] = "click";
            if (arg && sscanf(arg, "%d %d %15s", &mx, &my, what) >= 2) {
                Event ev;
                memset(&ev, 0, sizeof ev);
                ev.mx = mx;
                ev.my = my;
                if (str_eq(what, "drag"))       ev.type = EV_DRAG;
                else if (str_eq(what, "release")) ev.type = EV_RELEASE;
                else                            ev.type = EV_CLICK;
                handle_event(&ev);
            } else {
                fprintf(stderr, "replay: mouse <x> <y> [click|drag|release]\n");
            }
        } else if (str_eq(line, "sel")) {
            /* sel - estado da selecao, cores da primeira celula e o texto que
               o Ctrl+C copiaria (gancho dos testes, sem tocar no clipboard) */
            render();
            if (!app.sel_active) {
                printf("sel: inativa\n");
            } else {
                int y0, x0, y1, x1;
                sel_normalize(&y0, &x0, &y1, &x1);
                int sy = y0 - app.tr_start;
                uint32_t fg = 0, bg = 0;
                if (sy >= 0 && sy < app.tr_view && x0 >= 0 && x0 < app.screen.cols) {
                    size_t k = (size_t)sy * (size_t)app.screen.cols + (size_t)x0;
                    fg = app.screen.fg[k];
                    bg = app.screen.bg[k];
                }
                Str t; str_init(&t);
                selection_text(&t);
                printf("sel: %d,%d -> %d,%d fg=%06X bg=%06X texto=%zu inicio=\"%.60s\"\n",
                       y0, x0, y1, x1, (unsigned)fg, (unsigned)bg, t.len,
                       t.data ? t.data : "");
                str_free(&t);
            }
        } else if (str_eq(line, "dump")) {
            printf("--- passo %d: %s ---\n", step, arg ? arg : "");
            render();
            dump_screen_plain(&app.screen, stdout);
            printf("\n");
        } else if (str_eq(line, "update-check")) {
            /* teste: mesmo caminho do aviso de versao nova da inicializacao */
            update_boot_notice();
            render();
        } else if (str_eq(line, "status-off")) {
            app.status_until = 0;
            render();
        } else if (str_eq(line, "size")) {
            /* muda o tamanho e forca um resize (testa a remontagem do transcript) */
            int c = 0, r = 0;
            if (arg && sscanf(arg, "%d %d", &c, &r) == 2 && c > 0 && r > 0) {
                term_set_size(c, r);
                screen_resize(&app.screen, c, r);
                Event ev;
                memset(&ev, 0, sizeof ev);
                ev.type = EV_RESIZE;
                handle_event(&ev);
            }
        } else if (str_eq(line, "echo")) {
            printf("### %s\n", arg ? arg : "");
        } else if (str_eq(line, "input")) {
            printf("### entrada = \"%s\" (cursor %zu)%s\n",
                   app.input.data ? app.input.data : "", app.cursor,
                   app.tr_dirty ? " [transcript sujo]" : "");
        }
        free(line);
    }

    free(text);
    return 0;
}

/* Roda uma ferramenta direto (sem modelo): delta --tool fs '{"op":"list"}' */
static int run_tool_cli(const char *name, const char *json, const char *dir)
{
    delta_bootstrap();
    const Tool *t = tool_find(name);
    if (!t) {
        fprintf(stderr, "delta: ferramenta desconhecida '%s' (veja --tools)\n", name);
        return 2;
    }
    JValue *args = json_parse_cstr(json);
    if (!args || args->kind != J_OBJ) {
        fprintf(stderr, "delta: argumentos precisam ser um objeto JSON\n");
        if (args) json_free(args);
        return 2;
    }
    char *cwd;
    if (dir) {
        char *base = cwd_get();
        cwd = tools_resolve(base, dir);
        free(base);
    } else {
        cwd = cwd_get();
    }
    ToolResult r;
    tool_result_init(&r);
    int ok = t->run((Tool *)t, args, cwd, &r);
    if (r.output.data) fwrite(r.output.data, 1, r.output.len, stdout);
    int rc = (ok && !r.is_error) ? 0 : 1;
    tool_result_free(&r);
    json_free(args);
    free(cwd);
    return rc;
}

/* Roda um roteiro de ferramentas (uma por linha: "nome {json}") */
static int run_tool_script(const char *path, const char *dir)
{
    size_t len = 0;
    char *txt = file_read(path, &len);
    if (!txt) { fprintf(stderr, "delta: nao consegui ler '%s'\n", path); return 2; }
    int rc = 0;
    size_t i = 0;
    while (i < len) {
        size_t eol = i;
        while (eol < len && txt[eol] != '\n') eol++;
        size_t ll = eol - i;
        while (ll > 0 && (txt[i + ll - 1] == '\r' || txt[i + ll - 1] == ' ')) ll--;
        if (ll > 0 && txt[i] != '#') {
            char *linha = str_ndup(txt + i, ll);
            char *sp = strchr(linha, ' ');
            if (sp) {
                *sp = 0;
                char *json = sp + 1;
                while (*json == ' ') json++;
                printf("--- %s %s\n", linha, json);
                int r = run_tool_cli(linha, json, dir);
                if (r > rc) rc = r;
            }
            free(linha);
        }
        i = eol + 1;
    }
    free(txt);
    return rc;
}

/* ================================================================== */
/* main                                                               */
/* ================================================================== */
static void usage(void)
{
    printf(
        "Delta Agent " VERSION " - coding agent leve (C11, sem dependencias)\n"
        "\n"
        "Uso:\n"
        "  delta                     TUI interativa (sempre um chat novo)\n"
        "  delta continue            retoma a ultima sessao desta pasta\n"
        "  delta -p \"<prompt>\"        modo impressao (nao interativo)\n"
        "  delta --model <id>        modelo\n"
        "  delta --provider <nome>   anthropic | openai\n"
        "  delta --theme <nome>      dark | light | arquivo.json\n"
        "  delta --cwd <dir>         diretorio de trabalho\n"
        "  delta --core <dir>        pasta de dados (padrao: <usuario>/.deltaCore)\n"
        "  delta --setup             instalador com tela (abre no navegador)\n"
        "  delta --install <dir>     instala sem tela (dados em <dir>)\n"
        "  delta update [--check]    instala a versao mais nova do GitHub\n"
        "  delta --tools             lista as ferramentas\n"
        "  delta --tool <nome> <json>  roda uma ferramenta direto (teste)\n"
        "  delta --version           versao\n"
        "\n"
        "Ambiente: ANTHROPIC_API_KEY, OPENAI_API_KEY, DELTA_MODEL, DELTA_PROVIDER\n"
        "Config:   ~/.deltaCore/config.json   (skills, mcps, memory, extensions, system)\n"
        "Projeto:  /projects <pasta> dentro da TUI (ou delta --cwd <dir>)\n");
}

/* Windows: true quando o processo foi aberto com duplo clique (o console tem
   so' ele). Rodando de um terminal, o shell tambem esta' no console. */
static int started_from_explorer(void)
{
#ifdef _WIN32
    DWORD pids[8];
    DWORD n = GetConsoleProcessList(pids, 8);
    return n <= 1;
#else
    return 0;
#endif
}

int main(int argc, char **argv)
{
    memset(&app, 0, sizeof app);
    update_init(argv[0]);        /* argv[0] para achar o proprio exe (POSIX sem /proc) */
    str_init(&app.input);
    str_init(&app.work_msg);
    str_init(&app.status);
    str_init(&app.picker_filter);
    str_init(&app.paste_text);
    str_init(&app.ultimo_colado);
    str_init(&app.ultimo_colado_img);
    catalog_init(&app.catalog);

    const char *prompt = NULL;
    const char *opt_model = NULL, *opt_provider = NULL, *opt_theme = NULL, *opt_cwd = NULL;
    const char *opt_baseurl = NULL, *opt_apikey = NULL;
    int use_altscreen = 1;
    const char *opt_replay = NULL;
    int replay_cols = 110, replay_rows = 32;
    int preview_mode = 0, preview_cols = 100, preview_rows = 34;
    int opt_new = 0;                 /* --new: forca sessao nova (padrao agora) */
    int opt_continue = 0;            /* continue/--continue/--resume: retoma a ultima da pasta */
    int opt_no_session = 0;          /* --no-session: nao grava sessao (subagentes) */

    /* --cwd antes de --tool/--tool-script: o roteiro roda no diretorio pedido */
    for (int k = 1; k + 1 < argc; k++)
        if (str_eq(argv[k], "--cwd")) opt_cwd = argv[k + 1];

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (str_eq(a, "-p") || str_eq(a, "--print")) {
            if (i + 1 < argc) prompt = argv[++i];
        } else if (str_eq(a, "--model") && i + 1 < argc) {
            opt_model = argv[++i];
        } else if (str_eq(a, "--provider") && i + 1 < argc) {
            opt_provider = argv[++i];
        } else if (str_eq(a, "--theme") && i + 1 < argc) {
            opt_theme = argv[++i];
        } else if (str_eq(a, "--cwd") && i + 1 < argc) {
            opt_cwd = argv[++i];
        } else if (str_eq(a, "--core") && i + 1 < argc) {
            delta_set_core(argv[++i]);      /* raiz dos dados (skills/mcps/...) */
        } else if (str_eq(a, "--baseurl") && i + 1 < argc) {
            opt_baseurl = argv[++i];
        } else if (str_eq(a, "--api-key") && i + 1 < argc) {
            opt_apikey = argv[++i];
        } else if (str_eq(a, "--new")) {
            opt_new = 1;
        } else if (str_eq(a, "continue") || str_eq(a, "--continue") ||
                   str_eq(a, "--resume") || str_eq(a, "resume")) {
            opt_continue = 1;
        } else if (str_eq(a, "--no-session")) {
            opt_no_session = 1;
        } else if (str_eq(a, "--no-altscreen")) {
            use_altscreen = 0;
        } else if (str_eq(a, "--replay") && i + 1 < argc) {
            opt_replay = argv[++i];
            if (i + 2 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') {
                replay_cols = atoi(argv[++i]);
                replay_rows = atoi(argv[++i]);
            }
        } else if (str_eq(a, "--tool-script") && i + 1 < argc) {
            return run_tool_script(argv[++i], opt_cwd);
        } else if (str_eq(a, "--tool") && i + 2 < argc) {
            const char *tnome = argv[++i];
            const char *tjson = argv[++i];
            return run_tool_cli(tnome, tjson, opt_cwd);
        } else if (str_eq(a, "--setup")) {
            return setup_run();          /* popup HTML; sem navegador, tela nativa */
        } else if (str_eq(a, "--install") && i + 1 < argc) {
            return setup_install_cli(argv[++i]);
        } else if (str_eq(a, "--version") || str_eq(a, "-v")) {
            printf("Delta Agent " VERSION "\n");
            return 0;
        } else if (str_eq(a, "--tools")) {
            for (const Tool *t = tools_all(); t->name; t++)
                printf("%-8s %s\n", t->name, t->description);
            return 0;
        } else if (str_eq(a, "--preview")) {
            int c = 100, r = 34;
            if (i + 2 < argc) { c = atoi(argv[i + 1]); r = atoi(argv[i + 2]); i += 2; }
            preview_mode = 1;
            preview_cols = c;
            preview_rows = r;
        } else if (str_eq(a, "--help") || str_eq(a, "-h")) {
            usage();
            return 0;
        } else if (str_eq(a, "update")) {
            return update_cli(argc - i, argv + i);   /* aceita --check */
        } else if (str_eq(a, "wiki") || str_eq(a, "docs")) {
            /* a documentacao agora vive so' no site (nao abre mais navegador
               nem servidor local) */
            printf("Documentacao: https://descz.github.io/delta/\n");
            return 0;
        } else if (a[0] != '-') {
            prompt = a;
        }
    }

    /* duplo clique no .exe (sem argumentos, console so com ele): tela nativa
       do instalador. Rode `delta --setup` a qualquer momento para reabrir. */
    if (argc == 1 && started_from_explorer())
        return setup_run();

    char note[256] = { 0 };
    delta_bootstrap();                 /* ~/.deltaCore: skills, mcps, system, ... */
    config_load(&app.cfg, note, sizeof note);
    /* modelos dos subagentes: na TUI pergunta; em -p/--tool usa o padrao salvo */
    tools_set_subagent_ask(ask_subagent_model, NULL);

    /* catalogo local (sem rede): da os nomes das envs, endpoints e limites */
    if (catalog_load_cached(&app.catalog)) app.catalog_loaded = 1;
    if (opt_model) { free(app.cfg.model); app.cfg.model = str_dup(opt_model); }
    if (opt_apikey) { free(app.cfg.api_key); app.cfg.api_key = str_dup(opt_apikey); }
    if (opt_provider) {
        free(app.cfg.provider);
        app.cfg.provider = str_dup(opt_provider);
        free(app.cfg.base_url);
        app.cfg.base_url = str_dup(catalog_endpoint(&app.catalog, opt_provider));
    }
    if (opt_baseurl) { free(app.cfg.base_url); app.cfg.base_url = str_dup(opt_baseurl); }

    /* provedor conhecido + modelo que nao existe nele => usa o padrao do provedor */
    if (app.catalog_loaded && app.cfg.provider &&
        !catalog_model(&app.catalog, app.cfg.provider, app.cfg.model)) {
        const MdModel *d = catalog_default_model(&app.catalog, app.cfg.provider);
        if (d) { free(app.cfg.model); app.cfg.model = str_dup(d->id); }
    }
    if (!opt_apikey) refresh_api_key();
    refresh_model_caps();

    /* tema */
    {
        const char *want = opt_theme ? opt_theme : "mono";
        load_theme_named(want, &app.theme);
        app.theme_name = str_dup(want);
    }

    char *cwd = opt_cwd ? str_dup(opt_cwd) : cwd_get();
    /* /projects: a pasta escolhida vale tambem nas proximas execucoes */
    if (!opt_cwd && app.cfg.project_dir && *app.cfg.project_dir && path_is_dir(app.cfg.project_dir)) {
        free(cwd);
        cwd = str_dup(app.cfg.project_dir);
    }
    {
        int lg = i18n_from_code(app.cfg.language);
        if (lg >= 0) i18n_set(lg);
    }
    agent_init(&app.agent, &app.cfg, cwd);
    plan_refresh();               /* painel do plano (agent op=plan) */
    /* `delta` sempre comeca um chat novo. `delta continue` retoma a ultima
       sessao desta pasta. Subagentes rodam com --no-session para nao sujar
       a lista de sessoes da pasta do pai. */
    {
        int retomou = 0;
        if (opt_continue && !opt_new && !opt_no_session)
            retomou = session_resume_last(cwd, &app.agent.msgs);
        if (retomou > 0) {
            blocks_from_msgs();
            Block *b = block_add(B_INFO);
            str_addf(&b->text, "sessao retomada · %d mensagens · continue de onde parou "
                               "(/new começa outra)", retomou);
            app.tr_dirty = 1;
        } else if (!opt_no_session) {
            session_start(cwd);
        }
    }
    free(cwd);

    if (opt_replay) {
        int rc = run_replay(opt_replay, replay_cols, replay_rows, opt_apikey == NULL);
        blocks_clear();
        screen_free(&app.screen);
        screen_free(&app.tr);
        theme_free(&app.theme);
        agent_free(&app.agent);
        config_free(&app.cfg);
        return rc;
    }

    if (preview_mode) {
        int rc = run_preview(preview_cols, preview_rows);
        agent_free(&app.agent);
        config_free(&app.cfg);
        theme_free(&app.theme);
        return rc;
    }

    if (prompt) {
        int rc = run_print_mode(prompt);
        agent_free(&app.agent);
        config_free(&app.cfg);
        return rc;
    }

    if (!term_is_tty()) {
        fprintf(stderr, "delta: a TUI precisa de um terminal interativo.\n"
                        "Use -p \"<prompt>\" para o modo nao interativo.\n");
        agent_free(&app.agent);
        config_free(&app.cfg);
        return 1;
    }

    if (!term_init()) {
        fprintf(stderr, "delta: nao foi possivel inicializar o terminal.\n");
        return 1;
    }
    if (use_altscreen) term_alt_enter();
    {
        char title[128];
        snprintf(title, sizeof title, "Delta Agent - %s", app.cfg.model ? app.cfg.model : "");
        term_set_title(title);
    }
    tools_set_tick(tools_tick_ui, NULL);
    g_tui_ativa = 1;                            /* agora pode perguntar (picker) */
    provider_set_wait_tick(provider_tick_ui, NULL);
    if (app.cfg.api_key) notify("pronto (%s/%s)", app.cfg.provider, app.cfg.model);
    else notify("sem API key: use /login <chave>");

    {
        int cols = 0, rows = 0;
        term_size(&cols, &rows);
        screen_init(&app.screen, cols, rows);
    }

    plan_refresh();
    app.tr_dirty = 1;
    render();

    while (!app.quit) {
        Event evs[512];
        int nev = term_read_batch(evs, 512, 250);
        /* colagem = rajada de CARACTERES. Um ENTER solto (mesmo que venha
           junto com um NUMLOCK do terminal) nao e colagem: tem que enviar. */
        int n_chars = 0;
        for (int k = 0; k < nev; k++) if (evs[k].type == EV_CHAR) n_chars++;
        if (n_chars >= 2) {
            /* junta todos os lotes da rajada antes de virar {paste N caracteres}:
               colagem grande nao perde o fim e o ENTER final nao envia sozinho */
            Str acc; str_init(&acc);
            paste_stream(evs, nev, &acc);
            for (;;) {
                Event mais[512];
                int nm = term_read_batch(mais, 512, 120);
                if (nm == 0) break;
                paste_stream(mais, nm, &acc);
            }
            if (acc.len) handle_paste_text(acc.data);
            str_free(&acc);
            render();
            continue;
        }
        if (nev == 0) {
            if (!g_update_checked) {
                g_update_checked = 1;
                update_boot_notice();      /* aviso de versao nova (cache de 24 h) */
            }
            /* Ctrl pressionado e nenhum caractere veio: o console nao colou
               texto, entao pode ser imagem/video no clipboard. */
            if (app.ctrl_key_ms) {
                uint64_t dt = now_ms() - app.ctrl_key_ms;
                if (dt > 100 && dt < 2000) {
                    app.ctrl_key_ms = 0;
                    Clip c;
                    char *dir = paste_dir();
                    if (clipboard_read(&c, dir) && c.kind != CLIP_TEXT) {
                        app.last_clip_ms = now_ms();   /* nao deixa o proximo fallback repetir */
                        inserir_colagem(&c);
                        render();
                    }
                    clip_free(&c);
                    free(dir);
                    app.ctrl_key_ms = 0;
                }
            }
            /* rede de seguranca para o Windows Terminal (e afins): ele consome
               o chord Ctrl+V inteiro quando ha imagem/video no clipboard e nao
               entrega evento nenhum. Se o Ctrl esta pressionado e o V acabou
               de ser apertado no teclado real, cola por conta propria.
               Texto fica de fora: o proprio terminal ja cola texto. */
            if (term_poll_paste_hotkey()) {
                if (now_ms() - app.last_clip_ms >= 300) {
                    app.last_clip_ms = now_ms();
                    Clip c;
                    char *dir = paste_dir();
                    if (clipboard_read(&c, dir) && c.kind != CLIP_TEXT) {
                        inserir_colagem(&c);
                        render();
                    }
                    clip_free(&c);
                    free(dir);
                }
            }
            if (app.working) {
                render();          /* spinner (do relogio) + leitura do teclado */
            } else if (app.status.len && now_ms() >= app.status_until) {
                str_clear(&app.status);
                render();
            }
            continue;
        }
        for (int k = 0; k < nev; k++) handle_event(&evs[k]);
    }

    if (use_altscreen) term_alt_leave();
    term_shutdown();
    blocks_clear();
    screen_free(&app.screen);
    screen_free(&app.tr);
    theme_free(&app.theme);
    str_free(&app.input);
    str_free(&app.work_msg);
    str_free(&app.status);
    free(app.theme_name);
    str_free(&app.picker_filter);
    str_free(&app.paste_text);
    str_free(&app.ultimo_colado);
    str_free(&app.ultimo_colado_img);
    for (int i = 0; i < app.n_imgs; i++) free(app.imgs[i]);
    free(app.login_provider);
    free(g_items);
    catalog_free(&app.catalog);
    agent_free(&app.agent);
    config_free(&app.cfg);
    return 0;
}
