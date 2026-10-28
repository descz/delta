/* update.h - versao nova a partir dos releases do GitHub.
 *
 * `delta update`        consulta, baixa e troca o proprio executavel
 * `delta update --check` so' consulta
 * na TUI: /update e o aviso automatico na inicializacao
 */
#ifndef DELTA_UPDATE_H
#define DELTA_UPDATE_H

#include "util.h"

#define DELTA_REPO         "descz/delta"
#define DELTA_RELEASES_API "https://api.github.com/repos/" DELTA_REPO "/releases/latest"
#define DELTA_RELEASES_PAGE "https://github.com/" DELTA_REPO "/releases/latest"

/* Intervalo minimo entre duas consultas de rede (a TUI usa o cache). */
#define UPDATE_TTL_SECONDS (24 * 3600)

typedef struct {
    int  available;        /* 1 = ha' versao mais nova */
    char current[32];      /* DELTA_VERSION */
    char latest[32];       /* tag sem o "v" */
    char asset[128];       /* nome do arquivo desta plataforma */
    char url[768];         /* URL do asset (vazio = sem binario para a plataforma) */
    char page[768];        /* pagina do release */
    char err[256];         /* motivo da falha (vazio = ok) */
} UpdateInfo;

/* Guarda o argv[0] (POSIX sem /proc, ex.: macOS) e limpa restos da troca
   anterior. Chamado uma vez no inicio de main(). */
void update_init(const char *argv0);

/* Compara versoes no estilo 1.2.3 (o "v" e sufixos como "-beta" sao
   ignorados). 1 = a > b, -1 = a < b, 0 = iguais. */
int  version_cmp(const char *a, const char *b);

/* Consulta o release mais novo. cache_ok=1 aceita a resposta guardada
   (UPDATE_TTL_SECONDS) e evita rede. 1 = resposta utilis em `out`. */
int  update_check(UpdateInfo *out, int cache_ok);

/* So' o cache, sem rede. 1 = conseguiu ler. */
int  update_cached(UpdateInfo *out);

/* Baixa o asset e troca o executavel em execucao. msg = resumo para o
   usuario. 1 = trocado (precisa reiniciar o delta). */
int  update_apply(const UpdateInfo *info, Str *msg);

/* Aviso pronto para a TUI (alocado) ou NULL quando nao ha' novidade.
   allow_network=0 le so' o cache. */
char *update_notice(int allow_network);

/* `delta update [--check]`. Retorna o codigo de saida do processo. */
int  update_cli(int argc, char **argv);

#endif /* DELTA_UPDATE_H */
