/* dialeto.h - modo dialectico: um agente constroi, outro critica, em loop.
 *
 * A ideia: o artifice implementa, o critico adversarial testa e aponta
 * defeito real, o artifice corrige, e assim por diante por N rodadas.
 * O progresso mora no disco (PLACAR.md + relato de cada rodada), nao no
 * contexto do modelo — por isso cada rodada comeca com contexto limpo.
 *
 * Este modulo e' puro (so' util.h): prompt, placar, parser da nota e a
 * copia isolada onde o critico roda os testes. O loop em si vive no
 * main.c, que tem acesso ao agente.
 */
#ifndef DELTA_DIALETO_H
#define DELTA_DIALETO_H

#include <stddef.h>
#include "util.h"

typedef struct {
    int    rodadas_min;   /* piso: nao encerra antes disto (padrao 50)   */
    int    rodadas_max;   /* teto (padrao: igual ao piso)                 */
    double meta;          /* nota que autoriza encerrar no piso (9.0)     */
    int    meta_seguidas; /* rodadas seguidas na meta (padrao 2)          */
    long   limite_s;      /* 0 = sem limite de tempo                      */
    int    copia_critico; /* 1 = critico roda em copia isolada (padrao)   */
    int    sim;           /* o usuario confirmou o custo (--sim)          */
    const char *critico;   /* modelo do critico (--critico); NULL = padrao */
    char   critico_buf[64]; /* dono do texto apontado por `critico`        */
} DialetoOpts;

typedef struct {
    int    rodada;
    double nota;
    int    bloqueios;   /* linhas "BLOQUEIO:"  */
    int    criticos;    /* linhas "CRITICO:"   */
    int    parse_ok;    /* o critico entregou "NOTA:" */
} DialetoNota;

typedef struct {
    DialetoOpts o;
    int    rodada;
    double nota;
    double melhor;
    int    seguidas_meta;
    int    sem_melhora;
    const char *motivo;   /* por que o loop encerrou */
    Str    placar;        /* linhas markdown: | rodada | nota | ...   */
    Str    pendencias;    /* ultima critica, para o artefato da rodada */
} Dialeto;

void  dialeto_init(Dialeto *d, const DialetoOpts *o);
void  dialeto_free(Dialeto *d);

/* Le as flags do comeco de `args` e aponta o brief no primeiro token que
 * nao e' flag. Retorna 0 e preenche *err quando a flag esta errada. */
int   dialeto_opts(DialetoOpts *o, const char *args, const char **brief, Str *err);

/* Nome de pasta seguro, derivado do brief. Alocado. */
char *dialeto_slug(const char *texto);

/* Prompts. Retornam alocado; quem chama libera. */
char *dialeto_prompt_dev(const Dialeto *d, const char *brief,
                         const char *ultima_critica);
char *dialeto_prompt_critico(const Dialeto *d, const char *brief,
                             const char *relato_dev, const char *diff,
                             const char *cwd, const char *preferencias);

/* Extrai NOTA / BLOQUEIO / CRITICO do texto do critico. 1 = achou nota. */
int   dialeto_nota(const char *texto, DialetoNota *out);

/* Atualiza placar, melhor nota, sequencia na meta e estagnacao. */
void  dialeto_registrar(Dialeto *d, const DialetoNota *n);

/* 1 = encerra agora (teto, ou piso cumprido com a meta estavel). */
int   dialeto_parar(Dialeto *d);

/* Relatorio final em markdown. Alocado. */
char *dialeto_relatorio(const Dialeto *d, const char *brief, int rodadas_feitas);

/* Retoma um dialeto interrompido: relê o PLACAR.md do diretório e reconstrói
 * o estado (rodada, notas, sequências) repetindo dialeto_registrar na ordem;
 * devolve em *ultima_critica a crítica da última rodada (alocada ou NULL).
 * Retorna o número de rodadas recuperadas (0 = dialeto novo). */
int   dialeto_retomar(Dialeto *d, const char *ddir, char **ultima_critica);

/* Copia da arvore de trabalho, para o critico rodar build/teste sem
 * tocar no projeto de verdade. Retorna o diretorio (alocado) ou NULL. */
char *dialeto_copia(const char *src, const char *dst, Str *err);
void  dialeto_copia_remove(const char *dir);

/* Roda um comando de shell e devolve ate' `max` bytes da saida. */
char *dialeto_cmd(const char *cmd, size_t max);

#endif /* DELTA_DIALETO_H */
