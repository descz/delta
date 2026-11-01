/* dialeto.c - modo dialectico (artifice x critico) sem dependencias.
 *
 * Tudo que e' logica pura mora aqui para poder ser testado no binario de
 * testes (que so' linka o CORE). O loop que chama o modelo fica no
 * main.c.
 */
#include "dialeto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#endif

/* ================================================================== */
/* Ciclo de vida                                                       */
/* ================================================================== */
void dialeto_init(Dialeto *d, const DialetoOpts *o)
{
    memset(d, 0, sizeof *d);
    d->o = *o;
    /* `critico` aponta para o buffer do proprio struct: refaz o ponteiro
       depois da copia, senao ele continua apontando para a struct original. */
    if (d->o.critico) d->o.critico = d->o.critico_buf;
    d->motivo = "";
    str_init(&d->placar);
    str_init(&d->pendencias);
}

void dialeto_free(Dialeto *d)
{
    if (!d) return;
    str_free(&d->placar);
    str_free(&d->pendencias);
}

/* ================================================================== */
/* Opcoes                                                              */
/* ================================================================== */
static int parar_em_marca(const char *p)
{
    return p[0] == '-' && p[1] == '-';
}

int dialeto_opts(DialetoOpts *o, const char *args, const char **brief, Str *err)
{
    memset(o, 0, sizeof *o);
    o->rodadas_min = 0;    /* 0 = sem piso */
    o->rodadas_max = 0;    /* 0 = INFINITO: roda ate o usuario parar (Ctrl+C) */
    o->meta = 0;           /* 0 = sem meta de encerramento automatico */
    o->meta_seguidas = 2;
    o->copia_critico = 1;

    const char *p = args ? args : "";
    while (*p == ' ') p++;

    while (parar_em_marca(p)) {
        char flag[32];
        size_t k = 0;
        while (p[k] && p[k] != ' ' && k + 1 < sizeof flag) {
            flag[k] = p[k];
            k++;
        }
        flag[k] = 0;
        p += k;

        /* valor opcional (nao consome a proxima flag). `valor` aponta para
           dentro de `args` — vale enquanto o chamador mantiver a string.
           SO' flags que tomam valor consomem: --sim tabela-larga tem que
           deixar o objetivo no brief (a mensagem de erro ensina exatamente
           essa sintaxe), nao virar valor de flag booleana. */
        int toma_valor = str_ieq(flag, "--rodadas") || str_ieq(flag, "--turnos") ||
                         str_ieq(flag, "--max") || str_ieq(flag, "--min") ||
                         str_ieq(flag, "--meta") || str_ieq(flag, "--minutos") ||
                         str_ieq(flag, "--critico");
        char num[32] = {0};
        const char *valor = NULL;
        int tem_valor = 0;
        if (toma_valor) {
            const char *q = p;
            while (*q == ' ') q++;
            if (*q && !parar_em_marca(q)) {
                size_t j = 0;
                valor = q;
                while (q[j] && q[j] != ' ') {
                    if (j + 1 < sizeof num) num[j] = q[j];
                    j++;
                }
                tem_valor = 1;
                p = q + j;
            }
        }

        if (str_ieq(flag, "--rodadas") || str_ieq(flag, "--turnos")) {
            if (!tem_valor || !num[0]) {
                str_adds(err, "--rodadas precisa de um numero (ex.: --rodadas 50)");
                return 0;
            }
            int n = atoi(num);
            if (n < 4) {
                str_adds(err, "--rodadas: o minimo e' 4 (abaixo disso nao ha dialogo)");
                return 0;
            }
            if (n > 500) {
                str_adds(err, "--rodadas: o teto e' 500");
                return 0;
            }
            o->rodadas_min = n;
            o->rodadas_max = n;
        } else if (str_ieq(flag, "--min")) {
            if (!tem_valor || !num[0]) {
                str_adds(err, "--min precisa de um numero de rodadas");
                return 0;
            }
            o->rodadas_min = atoi(num);
            if (o->rodadas_min < 1) o->rodadas_min = 1;
        } else if (str_ieq(flag, "--max")) {
            if (!tem_valor || !num[0]) {
                str_adds(err, "--max precisa de um numero de rodadas");
                return 0;
            }
            o->rodadas_max = atoi(num);
        } else if (str_ieq(flag, "--meta")) {
            if (!tem_valor || !num[0]) {
                str_adds(err, "--meta precisa de uma nota (ex.: --meta 9)");
                return 0;
            }
            o->meta = atof(num);
            if (o->meta <= 0 || o->meta > 10) o->meta = 9.0;
        } else if (str_ieq(flag, "--minutos")) {
            if (!tem_valor || !num[0]) {
                str_adds(err, "--minutos precisa de um numero (ex.: --minutos 90)");
                return 0;
            }
            long m = atol(num);
            if (m > 0) o->limite_s = m * 60;
        } else if (str_ieq(flag, "--critico")) {
            if (!tem_valor) {
                str_adds(err, "--critico precisa do nome de um modelo");
                return 0;
            }
            /* copia o token: `valor` aponta no meio de `args` e iria ate' o fim */
            snprintf(o->critico_buf, sizeof o->critico_buf, "%s", num);
            o->critico = o->critico_buf;
        } else if (str_ieq(flag, "--sem-copia")) {
            o->copia_critico = 0;
        } else if (str_ieq(flag, "--copia")) {
            o->copia_critico = 1;
        } else if (str_ieq(flag, "--sim") || str_ieq(flag, "--yes")) {
            o->sim = 1;
        } else if (str_eq(flag, "--")) {
            break;
        } else {
            str_addf(err, "flag desconhecida: %s", flag);
            return 0;
        }
        while (*p == ' ') p++;
    }

    if (o->rodadas_max > 0 && o->rodadas_max < o->rodadas_min) o->rodadas_max = o->rodadas_min;
    while (*p == ' ') p++;
    *brief = p;
    return 1;
}

/* ================================================================== */
/* Slug                                                                */
/* ================================================================== */
char *dialeto_slug(const char *texto)
{
    char buf[48];
    size_t n = 0;
    for (const char *p = texto ? texto : ""; *p && n + 1 < 32; p++) {
        unsigned char c = (unsigned char)*p;
        if (c >= 'A' && c <= 'Z') {
            buf[n++] = (char)(c - 'A' + 'a');
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            buf[n++] = (char)c;
        } else if (n && buf[n - 1] != '-') {
            buf[n++] = '-';
        }
    }
    while (n && buf[n - 1] == '-') n--;
    buf[n] = 0;
    return str_dup(n ? buf : "projeto");
}

/* ================================================================== */
/* Prompts                                                             */
/* ================================================================== */
static void add_trecho(Str *s, const char *texto, size_t max)
{
    if (!texto || !*texto) return;
    str_addn(s, texto, max); /* corta em max bytes */
}

char *dialeto_prompt_dev(const Dialeto *d, const char *brief,
                         const char *ultima_critica)
{
    Str s;
    str_init(&s);

    str_addf(&s, "DIALETO · RODADA %d%s — voce e' o ARTIFICE.\n\n",
             d->rodada,
             d->o.rodadas_max > 0 ? "" : " (sem teto — roda ate o usuario parar)");
    (void)d->o.rodadas_max;
    str_adds(&s, "Objetivo do projeto:\n");
    add_trecho(&s, brief, 4000);
    str_adds(&s, "\n\n");

    if (d->placar.len) {
        str_adds(&s, "Placar ate' agora (nota do critico, de 0 a 10):\n");
        add_trecho(&s, d->placar.data, 4000);
        str_adds(&s, "\n");
    }

    if (ultima_critica && *ultima_critica) {
        str_adds(&s, "Critica da rodada anterior — esta e' a sua fila:\n\"\"\"\n");
        add_trecho(&s, ultima_critica, 8000);
        str_adds(&s, "\n\"\"\"\n\n");
    }

    str_adds(&s,
        "O que esta rodada precisa entregar:\n"
        "1. Comece pelos BLOQUEIO da critica acima, um por vez, provando cada um.\n"
        "2. Entrega minima da rodada: UMA melhoria verificavel — bug corrigido com\n"
        "   evidencia, teste novo que falha antes e passa depois, caso de borda\n"
        "   coberto, erro que estourava e agora tem mensagem, ou doc que mentia.\n"
        "3. Rode o build e os testes do projeto ANTES de responder. Afirmacao sem\n"
        "   saida de comando nao vale nada para o critico.\n"
        "4. Nao reescreva o projeto do zero e nao faca refactor cosmetico: o critico\n"
        "   tem o placar e trata churn sem ganho como regressao.\n"
        "5. Nao leia nem escreva em .delta/dialeto/ — e' o placar do dialogo.\n"
        "6. Se a critica estiver errada, diga por que, com arquivo:linha e a saida\n"
        "   que prova. Discordar com prova vale; discordar sem prova nao.\n\n"
        "Responda em no maximo 15 linhas, neste formato:\n"
        "MUDOU: arquivo — o que mudou (uma linha por arquivo)\n"
        "PROVA: o comando que rodou e o resultado\n"
        "ABERTO: o que ficou para a proxima rodada\n");

    if (d->sem_melhora >= 3) {
        str_adds(&s,
            "\nATENCAO: a nota nao sobe ha varias rodadas. Repetir a mesma abordagem\n"
            "nao vai funcionar. Escolha um angulo que voce ainda nao atacou: entrada\n"
            "malformada, limite de memoria, concorrencia, falha de IO, mensagem de\n"
            "erro inutil, teste que nao existe, caso que so' aparece em producao.\n");
    }
    return str_steal(&s);
}

char *dialeto_prompt_critico(const Dialeto *d, const char *brief,
                             const char *relato_dev, const char *diff,
                             const char *cwd)
{
    Str s;
    str_init(&s);

    str_addf(&s, "DIALETO · RODADA %d — voce e' o CRITICO.\n\n", d->rodada);
    str_adds(&s, "Voce julga o trabalho do artifice. Sem cortesia, sem elogio,\n"
                 "sem resumo do codigo. O produto final depende de voce achar o que\n"
                 "esta errado enquanto ainda da' tempo.\n\n");

    str_adds(&s, "Objetivo do projeto:\n");
    add_trecho(&s, brief, 4000);
    str_adds(&s, "\n\n");

    if (cwd && *cwd) {
        str_addf(&s, "Voce esta numa COPIA isolada do projeto: %s\n", cwd);
        str_adds(&s,
            "Voce pode compilar, rodar testes e quebrar coisas a vontade aqui — o\n"
            "projeto de verdade nao e' afetado. Use isso: teste de verdade.\n\n");
    }

    str_adds(&s, "O que o artifice disse ter feito nesta rodada:\n\"\"\"\n");
    add_trecho(&s, relato_dev, 6000);
    str_adds(&s, "\n\"\"\"\n\n");

    if (diff && *diff) {
        str_adds(&s, "Mudancas no projeto (git diff):\n\"\"\"\n");
        add_trecho(&s, diff, 12000);
        str_adds(&s, "\n\"\"\"\n\n");
    }

    if (d->placar.len) {
        str_adds(&s, "Placar das rodadas anteriores:\n");
        add_trecho(&s, d->placar.data, 3000);
        str_adds(&s, "\n");
    }

    str_adds(&s,
        "Como julgar, nesta ordem:\n"
        "1. O que o artifice afirmou e' VERDADE? Rode o comando que ele citou. Se a\n"
        "   saida nao bater com a afirmacao, isso e' bloqueio.\n"
        "2. Defeito real de comportamento: entrada que quebra, estado que corrompe,\n"
        "   perda de dado, mensagem de erro que engana, resultado errado em silencio.\n"
        "3. Buraco de teste que esconde regressao real (nao cobertura por cobertura).\n"
        "4. Doc que mente, codigo morto, dependencia que nao precisava existir.\n"
        "5. So' depois de tudo isso: forma, nome, organizacao.\n\n"
        "Regras:\n"
        "- Todo apontamento precisa de arquivo:linha (ou comando + saida).\n"
        "- Nao invente defeito para parecer rigoroso: apontamento falso queima uma\n"
        "  rodada do artifice e derruba a nota final.\n"
        "- Nao repita apontamento ja corrigido nas rodadas anteriores.\n\n"
        "Termine a resposta EXATAMENTE com estas linhas (o driver as le'):\n"
        "NOTA: <0 a 10, uma casa decimal>\n"
        "BLOQUEIO: <arquivo:linha — defeito que torna o resultado errado>\n"
        "CRITICO: <o que um usuario faria hoje e quebra>\n"
        "PROXIMO: <o proximo passo concreto do artifice>\n\n"
        "Escala da nota: 0-3 quebrado ou nao faz o que promete; 4-5 funciona com\n"
        "falhas graves; 6-7 solido mas incompleto; 8-8.9 bom, com buracos conhecidos;\n"
        "9-10 pronto para uso real. Se nao houver BLOQUEIO, escreva 'BLOQUEIO: nenhum'.\n");
    return str_steal(&s);
}

/* ================================================================== */
/* Nota do critico                                                     */
/* ================================================================== */
static const char *pular_marcacao(const char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '*' || *s == '`' || *s == '#' ||
           *s == '>' || *s == '-' || *s == '|' || *s == '_')
        s++;
    return s;
}

/* Primeiro numero da linha, aceitando virgula decimal ("8,5") e
 * ignorando o que vem depois ("8.5 de 10" -> 8.5). -1 = nao ha numero. */
static double num_da_linha(const char *q)
{
    char limpo[16];
    size_t k = 0;
    while (*q == ' ' || *q == '\t') q++;
    for (; *q && k + 1 < sizeof limpo; q++) {
        if ((*q >= '0' && *q <= '9') || *q == '.') limpo[k++] = *q;
        else if (*q == ',') limpo[k++] = '.';
        else break;
    }
    limpo[k] = 0;
    return k ? atof(limpo) : -1.0;
}

int dialeto_nota(const char *texto, DialetoNota *out)
{
    memset(out, 0, sizeof *out);
    if (!texto) return 0;

    const char *p = texto;
    int achou = 0;
    double nota = 0;

    while (*p) {
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        char linha[512];
        if (len > sizeof linha - 1) len = sizeof linha - 1;
        memcpy(linha, p, len);
        linha[len] = 0;

        const char *t = pular_marcacao(linha);
        if (str_istarts(t, "nota")) {
            const char *q = t + 4;
            /* aceita "NOTA: 9", "NOTA FINAL = 9,5", "**Nota** 9/10" */
            while (*q && !(*q >= '0' && *q <= '9')) q++;
            double v = *q ? num_da_linha(q) : -1.0;
            if (v >= 0) {
                nota = v;
                achou = 1;
            }
        } else if (str_istarts(t, "bloqueio")) {
            const char *q = t + 8;
            while (*q == ' ' || *q == ':') q++;
            if (!str_istarts(q, "nenhum")) out->bloqueios++;
        } else if (str_istarts(t, "crítico") || str_istarts(t, "critico")) {
            const char *q = t + 7;
            while (*q == ' ' || *q == ':') q++;
            if (*q && !str_istarts(q, "nenhum")) out->criticos++;
        }

        if (!e) break;
        p = e + 1;
    }

    if (nota < 0) nota = 0;
    if (nota > 10) nota = 10;
    out->nota = nota;
    out->parse_ok = achou;
    if (!achou) {
        /* critico que nao entrega nota vale como rodada sem ganho, nao como
           rodada perfeita: nota 0 com bloqueio contabilizado. */
        out->bloqueios = out->bloqueios ? out->bloqueios : 1;
    }
    return achou;
}

void dialeto_registrar(Dialeto *d, const DialetoNota *n)
{
    d->rodada = n->rodada;
    if (d->rodada > 1 && n->nota > d->nota + 0.05) d->sem_melhora = 0;
    else if (d->rodada > 1) d->sem_melhora++;

    d->nota = n->nota;
    if (n->nota > d->melhor) d->melhor = n->nota;

    if (n->nota >= d->o.meta && n->bloqueios == 0) d->seguidas_meta++;
    else d->seguidas_meta = 0;

    str_addf(&d->placar, "| %d | %.1f | %d | %d |%s\n", n->rodada, n->nota,
             n->bloqueios, n->criticos, n->parse_ok ? "" : " (sem nota)");
}

int dialeto_parar(Dialeto *d)
{
    /* modo infinito (padrão): so' o usuario manda parar (Ctrl+C) */
    if (d->o.rodadas_max == 0) return 0;
    if (d->rodada >= d->o.rodadas_max) {
        d->motivo = "teto de rodadas";
        return 1;
    }
    if (d->o.meta <= 0) return 0;   /* sem meta: sem encerramento automatico */
    if (d->rodada < d->o.rodadas_min) return 0;
    if (d->seguidas_meta >= d->o.meta_seguidas) {
        d->motivo = "meta atingida e estavel";
        return 1;
    }
    return 0;
}

/* Retoma: relê o PLACAR.md e reproduz dialeto_registrar na ordem — o estado
 * (rodada, melhor nota, seguidas na meta, estagnação) volta exatamente como
 * estava quando o dialeto parou. A crítica pendente vem do último
 * rodada-NN.md (seção "## Critica"). */
int dialeto_retomar(Dialeto *d, const char *ddir, char **ultima_critica)
{
    if (ultima_critica) *ultima_critica = NULL;
    if (!ddir || !*ddir) return 0;

    char *p = path_join(ddir, "PLACAR.md");
    char *txt = p ? file_read(p, NULL) : NULL;
    free(p);
    if (!txt) return 0;

    int feitas = 0;
    size_t placar_ini = d->placar.len;
    for (char *linha = strtok(txt, "\n"); linha; linha = strtok(NULL, "\n")) {
        int rod = 0, bloq = 0, crit = 0;
        double nota = 0;
        if (sscanf(linha, "| %d | %lf | %d | %d |", &rod, &nota, &bloq, &crit) != 4)
            continue;                        /* cabecalho ou lixo: fora */
        DialetoNota n;
        memset(&n, 0, sizeof n);
        n.rodada = rod;
        n.nota = nota;
        n.bloqueios = bloq;
        n.criticos = crit;
        n.parse_ok = strstr(linha, "(sem nota)") == NULL;
        dialeto_registrar(d, &n);
        feitas = rod;
    }
    free(txt);
    if (!feitas) return 0;

    /* ultima critica: o rodada-NN.md de maior numero */
    int melhor_nn = 0;
    char **nomes = NULL;
    size_t nn = 0;
    nomes = dir_list(ddir, &nn);
    if (nomes) {
        for (size_t i = 0; i < nn; i++) {
            int nn2 = 0;
            if (sscanf(nomes[i], "rodada-%d.md", &nn2) == 1 && nn2 > melhor_nn) melhor_nn = nn2;
        }
        dir_list_free(nomes, (size_t)nn);
    }
    if (melhor_nn > 0 && ultima_critica) {
        char nome[64];
        snprintf(nome, sizeof nome, "rodada-%02d.md", melhor_nn);
        char *pr = path_join(ddir, nome);
        char *md = file_read(pr, NULL);
        free(pr);
        if (md) {
            const char *sec = strstr(md, "## Critica");
            if (sec) {
                const char *fim = strstr(sec, "\n");
                if (fim) {
                    const char *ini = fim + 1;
                    while (*ini == '\n') ini++;
                    if (*ini) *ultima_critica = str_dup(ini);
                }
            }
        }
        free(md);
    }
    (void)placar_ini;
    return feitas;
}

char *dialeto_relatorio(const Dialeto *d, const char *brief, int rodadas_feitas)
{
    Str s;
    str_init(&s);
    str_adds(&s, "# Dialeto — relatorio\n\n");
    str_adds(&s, "## Objetivo\n\n");
    add_trecho(&s, brief, 4000);
    str_adds(&s, "\n\n## Resultado\n\n");
    str_addf(&s, "- Rodadas: %d\n- Melhor nota: %.1f\n- Ultima nota: %.1f\n- Encerrou por: %s\n",
             rodadas_feitas, d->melhor, d->nota, d->motivo && *d->motivo ? d->motivo : "manual");
    str_adds(&s, "\n## Placar\n\n| rodada | nota | bloqueios | criticos |\n"
                 "| --- | --- | --- | --- |\n");
    add_trecho(&s, d->placar.data, 8000);
    if (d->pendencias.len) {
        str_adds(&s, "\n## Pendencias da ultima critica\n\n");
        add_trecho(&s, d->pendencias.data, 8000);
        str_adds(&s, "\n");
    }
    return str_steal(&s);
}

/* ================================================================== */
/* Copia isolada                                                       */
/* ================================================================== */
typedef struct {
    size_t arquivos;
    size_t bytes;
    int    cortou;
} CopiaCtx;

#define COPIA_MAX_ARQ  20000
#define COPIA_MAX_BYTE (256u * 1024u * 1024u)
#define COPIA_MAX_F    (16u * 1024u * 1024u)
#define COPIA_MAX_NIVEL 12

static int ignorar_nome(const char *nome)
{
    static const char *const IGN[] = {
        ".git", "build", "bin", "dist", "node_modules", ".delta",
        "out", "target", ".cache", "__pycache__", ".venv", "venv", ".idea",
        ".vscode", "obj", "release", "debug"
    };
    for (size_t i = 0; i < sizeof IGN / sizeof IGN[0]; i++)
        if (str_ieq(nome, IGN[i])) return 1;
    return 0;
}

static void copia_rec(const char *src, const char *dst, int nivel, CopiaCtx *c, Str *err)
{
    if (c->cortou) return;
    if (nivel > COPIA_MAX_NIVEL) { c->cortou = 1; return; }
    if (!dir_make(dst) && !dir_exists(dst)) {
        str_addf(err, "nao consegui criar %s", dst);
        c->cortou = 1;
        return;
    }

    size_t n = 0;
    char **lista = dir_list(src, &n);
    if (!lista) return;

    for (size_t i = 0; i < n && !c->cortou; i++) {
        const char *nome = lista[i];
        if (ignorar_nome(nome)) continue;

        char *ps = path_join(src, nome);
        char *pd = path_join(dst, nome);
        if (path_is_dir(ps)) {
            copia_rec(ps, pd, nivel + 1, c, err);
        } else {
            size_t len = 0;
            char *buf = file_read(ps, &len);
            if (buf) {
                if (len <= COPIA_MAX_F && c->bytes + len <= COPIA_MAX_BYTE &&
                    c->arquivos < COPIA_MAX_ARQ) {
                    file_write(pd, buf, len);
                    c->arquivos++;
                    c->bytes += len;
                } else {
                    c->cortou = 1;
                }
                free(buf);
            }
        }
        free(ps);
        free(pd);
    }
    dir_list_free(lista, n);
}

char *dialeto_copia(const char *src, const char *dst, Str *err)
{
    CopiaCtx c;
    memset(&c, 0, sizeof c);
    copia_rec(src, dst, 0, &c, err);
    if (c.cortou && err && !err->len) {
        str_adds(err, "copia parcial: o projeto e' maior que o limite da copia");
    }
    return c.arquivos ? str_dup(dst) : NULL;
}

void dialeto_copia_remove(const char *dir)
{
    if (!dir || !*dir || !dir_exists(dir)) return;
    size_t n = 0;
    char **lista = dir_list(dir, &n);
    if (lista) {
        for (size_t i = 0; i < n; i++) {
            char *p = path_join(dir, lista[i]);
            if (path_is_dir(p)) dialeto_copia_remove(p);
            else remove(p);
            free(p);
        }
        dir_list_free(lista, n);
    }
#ifdef _WIN32
    RemoveDirectoryA(dir);
#else
    rmdir(dir);
#endif
}

/* ================================================================== */
/* Comando de shell                                                    */
/* ================================================================== */
char *dialeto_cmd(const char *cmd, size_t max)
{
    Str s;
    str_init(&s);
    FILE *f = popen(cmd, "r");
    if (!f) return str_steal(&s);
    char buf[4096];
    size_t total = 0;
    while (total < max) {
        size_t n = fread(buf, 1, sizeof buf, f);
        if (!n) break;
        if (total + n > max) n = max - total;
        str_add(&s, buf, n);
        total += n;
    }
    pclose(f);
    if (total >= max) str_adds(&s, "\n[... saida cortada ...]\n");
    return str_steal(&s);
}
