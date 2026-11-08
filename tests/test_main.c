/* tests/test_main.c - oraculo de fidelidade: cores, JSON, largura, markdown */
#include "../src/util.h"
#include "../src/json.h"
#include "../src/theme.h"
#include "../src/okcolor.h"
#include "../src/ui.h"
#include "../src/dialeto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int failures = 0;

static int contains(const char *hay, const char *needle)
{
    return hay && needle && strstr(hay, needle) != NULL;
}
static int checks = 0;

static void ok(int cond, const char *what)
{
    checks++;
    if (!cond) { failures++; printf("  FALHOU: %s\n", what); }
}

static void eq_hex(Rgb8 b, const char *expect, const char *what)
{
    char got[8];
    snprintf(got, sizeof got, "#%02x%02x%02x", b.r, b.g, b.b);
    checks++;
    if (!str_ieq(got, expect)) {
        failures++;
        printf("  FALHOU: %s -> %s (esperado %s)\n", what, got, expect);
    }
}

int main(void)
{
    printf("Delta testes\n");

    /* ---------------- okhsl: oraculo gerado com o pi-tui real ---------------- */
    printf("[1] okhsl/oklch -> sRGB (oraculo: pi-tui parseColor/colorToHex)\n");
    eq_hex(okhsl_rgb8(234.0, 0.03, 0.89), "#dee0e1", "text");
    eq_hex(okhsl_rgb8(229.0, 0.06, 0.67), "#9da5a9", "muted");
    eq_hex(okhsl_rgb8(295.0, 0.50, 0.67), "#a798d7", "accent");
    eq_hex(okhsl_rgb8(159.0, 0.59, 0.67), "#68b78d", "success");
    eq_hex(okhsl_rgb8(20.0, 0.72, 0.67), "#ea7f81", "error");
    eq_hex(okhsl_rgb8(83.0, 0.88, 0.67), "#cd9a22", "warning");
    eq_hex(okhsl_rgb8(233.0, 0.41, 0.24), "#213b49", "userMessageBg");
    eq_hex(okhsl_rgb8(158.0, 0.46, 0.25), "#254131", "toolSuccessBg");
    eq_hex(okhsl_rgb8(20.0, 0.99, 0.63), "#fe5462", "thinkingMax");
    eq_hex(oklch_rgb8(0.7, 0.12, 265), "#7a9ce9", "oklch");
    eq_hex(okhsl_rgb8(0, 0, 1.0), "#ffffff", "branco");
    eq_hex(okhsl_rgb8(0, 0, 0.0), "#000000", "preto");

    /* ---------------- JSON ---------------- */
    printf("[2] JSON\n");
    {
        const char *src = "{\"a\":[1,2,{\"b\":\"x\\ny\"}],\"c\":true,\"d\":null,\"e\":-1.5e2}";
        JValue *v = json_parse_cstr(src);
        ok(v != NULL, "parse");
        ok(json_num(json_at(json_get(v, "a"), 0), -1) == 1, "array[0]");
        ok(str_eq(json_str(json_get(json_at(json_get(v, "a"), 2), "b")), "x\ny"), "string com escape");
        ok(json_bool(json_get(v, "c"), 0) == 1, "bool");
        ok(json_get(v, "d") && json_get(v, "d")->kind == J_NULL, "null");
        ok(fabs(json_num(json_get(v, "e"), 0) + 150.0) < 1e-9, "numero exp");
        ok(json_get(v, "naoExiste") == NULL, "ausente");
        json_free(v);
        ok(json_parse_cstr("{bad}") == NULL, "json invalido rejeitado");
    }

    /* ---------------- largura de exibicao ---------------- */
    printf("[3] largura de exibicao\n");
    ok(str_width("abc") == 3, "ascii");
    ok(str_width("─") == 1, "box drawing = 1");
    ok(str_width("日本") == 4, "CJK = 2 cada");
    ok(str_width("\x1b[31mred\x1b[0m") == 3, "ignora ANSI");
    ok(str_width("e\u0301") == 1, "combinante = 0");
    ok(str_clip_cols("日本abc", strlen("日本abc"), 3) == strlen("日"), "clip em coluna");

    /* ---------------- tema ---------------- */
    printf("[4] tema\n");
    {
        Theme t;
        theme_load_builtin_dark(&t);
        Rgb8 text = rgb_to_rgb8(theme_tok(&t, TK_TEXT)->rgb);
        ok(text.r == 0xde && text.g == 0xe0 && text.b == 0xe1, "dark text");
        ok(theme_tok(&t, TK_TOOL_PENDING_BG)->kind != CHEAP_DEFAULT, "bg nao default");
        ok(token_from_name("toolPendingBg") == TK_TOOL_PENDING_BG, "lookup de token");
        ok(token_from_name("naoExiste") == TK_COUNT, "token desconhecido");
        ok(token_slot(TK_TOOL_PENDING_BG) == SLOT_BG, "slot bg");
        ok(token_slot(TK_TEXT) == SLOT_FG, "slot fg");
        theme_free(&t);
    }

    /* ---------------- tema mono (padrao) ---------------- */
    printf("[4b] tema mono\n");
    {
        Theme m;
        theme_load_builtin_mono(&m);
        ok(m.mono == 1, "flag mono");
        Rgb8 tx = rgb_to_rgb8(theme_tok(&m, TK_TEXT)->rgb);
        ok(tx.r == tx.g && tx.g == tx.b, "texto e cinza puro");
        Rgb8 um = rgb_to_rgb8(theme_tok(&m, TK_USER_MESSAGE_BG)->rgb);
        ok(um.r == um.g && um.g == um.b, "fundo da mensagem do usuario e cinza");
        Rgb8 tb = rgb_to_rgb8(theme_tok(&m, TK_TOOL_SUCCESS_BG)->rgb);
        ok(tb.r == tb.g && tb.g == tb.b, "fundo da ferramenta e cinza");
        Rgb8 ac = rgb_to_rgb8(theme_tok(&m, TK_ACCENT)->rgb);
        ok(ac.r == 255 && ac.g == 255 && ac.b == 255, "accent e branco");
        theme_free(&m);
    }

    /* ---------------- tema em arquivo ---------------- */
    printf("[5] tema em arquivo (vars + refs)\n");
    {
        const char *json =
            "{\"name\":\"t\",\"appearance\":\"dark\",\"vars\":{\"base\":\"okhsl(234 3% 89%)\"},"
            "\"colors\":{\"text\":\"base\",\"accent\":\"#ff0000\"}}";
        FILE *f = fopen("build/test-theme.json", "wb");
        if (f) { fwrite(json, 1, strlen(json), f); fclose(f); }
        Theme t;
        ok(theme_load(&t, "build/test-theme.json"), "load");
        Rgb8 a = rgb_to_rgb8(theme_tok(&t, TK_ACCENT)->rgb);
        ok(a.r == 255 && a.g == 0 && a.b == 0, "hex direto");
        Rgb8 tx = rgb_to_rgb8(theme_tok(&t, TK_TEXT)->rgb);
        ok(tx.r == 0xde && tx.g == 0xe0 && tx.b == 0xe1, "ref de var");
        theme_free(&t);
    }

    /* ---------------- markdown ---------------- */
    printf("[6] markdown\n");
    {
        Theme t; theme_load_builtin_dark(&t);
        Style base = { 0, COL_DEFAULT, 0 };

        /* **negrito**, *italico* e `code` inline viram estilo, nao texto cru */
        const char *md = "Chefe, o **cache** agora vem *em streaming* e o `path` mudou.";
        Doc d; md_layout(&t, md, strlen(md), 70, base, &d);
        int tem_bold = 0, tem_ital = 0, asterisco = 0, crase = 0;
        for (size_t i = 0; i < d.n; i++)
            for (size_t k = 0; k < d.l[i].n; k++) {
                if (d.l[i].st[k].a & ATTR_BOLD) tem_bold = 1;
                if (d.l[i].st[k].a & ATTR_ITALIC) tem_ital = 1;
                if (d.l[i].cp[k] == '*') asterisco = 1;
                if (d.l[i].cp[k] == '`') crase = 1;
            }
        ok(d.n >= 1, "markdown gera linhas");
        ok(tem_bold, "**negrito** renderiza com ATTR_BOLD");
        ok(tem_ital, "*italico* renderiza com ATTR_ITALIC");
        ok(!asterisco, "os asteriscos de marcacao nao aparecem");
        ok(!crase, "as crases de inline code nao aparecem");
        doc_free(&d);

        /* bloco cercado: abre, conteudo preservado, fecha */
        const char *bloco = "```c\nstatic int x = 1;\n```\n";
        Doc b; md_layout(&t, bloco, strlen(bloco), 40, base, &b);
        char conteudo[128] = { 0 };
        if (b.n >= 2) {
            size_t w = 0;
            for (size_t k = 0; k < b.l[1].n && w + 1 < sizeof conteudo; k++)
                conteudo[w++] = (char)b.l[1].cp[k];
            conteudo[w] = 0;
        }
        ok(b.n >= 2 && b.l[0].n >= 2 && b.l[0].cp[0] == 0x2502 && b.l[1].cp[0] == 0x2502,
           "bloco de codigo vira citacao (barra na lateral, sem cercas cruas)");
        ok(strstr(conteudo, "static int x = 1;") != NULL, "conteudo do bloco e' preservado");
        doc_free(&b);

        /* nada dentro do bloco vira markdown */
        const char *cru = "```\na ** b * c\n```\n";
        Doc c; md_layout(&t, cru, strlen(cru), 40, base, &c);
        int marcou = 0, cerca = 0;
        for (size_t i = 0; i < c.n; i++)
            for (size_t k = 0; k < c.l[i].n; k++) {
                if (c.l[i].st[k].a & (ATTR_BOLD | ATTR_ITALIC)) marcou = 1;
                if (c.l[i].cp[k] == '`') cerca = 1;
            }
        ok(!marcou, "dentro do bloco nada e' interpretado como markdown");
        ok(!cerca, "os backticks da cerca nao vazam na tela");
        doc_free(&c);
        theme_free(&t);
    }

    /* ---------------- links clicaveis (OSC 8) ---------------- */
    printf("[7] links na grade\n");
    {
        Theme t; theme_load_builtin_dark(&t);
        Style base = { 0, COL_DEFAULT, 0 };
        Screen s; screen_init(&s, 60, 6);
        const char *txt = "grupo: https://t.me/+Neo123456789 aqui";
        screen_para(&s, 0, 0, 60, txt, strlen(txt), base);
        /* a URL tem que ter virado link; o texto em volta, nao */
        int links_marcados = 0, antes = 0;
        for (int x = 0; x < 60; x++) {
            if (s.lk[x]) links_marcados++;
            if (x < 7 && s.lk[x]) antes++;
        }
        ok(links_marcados >= 24, "URL na linha vira link (celulas marcadas)");
        ok(antes == 0, "texto antes da URL nao vira link");
        ok(s.nlinks == 1 && s.links[0] && str_eq(s.links[0], "https://t.me/+Neo123456789"),
           "URL guardada na tabela do screen");
        screen_free(&s);
        theme_free(&t);
    }

    /* ---------------- gravacao atomica (duas instancias no mesmo disco) ---------------- */
    printf("[8] file_write_atomic\n");
    {
        char big[4096];
        for (size_t i = 0; i < sizeof big; i++) big[i] = (char)('a' + (i % 26));
        const char *p = "build/test-atomic.json";
        ok(file_write_atomic(p, big, sizeof big), "grava arquivo novo");
        size_t n = 0;
        char *back = file_read(p, &n);
        ok(back && n == sizeof big && memcmp(back, big, sizeof big) == 0,
           "conteudo e tamanho conferem");
        free(back);

        ok(file_write_atomic(p, "{\"a\":1}\n", 8), "sobrescreve arquivo existente");
        back = file_read(p, &n);
        ok(back && n == 8 && memcmp(back, "{\"a\":1}\n", 8) == 0,
           "sobrescrita troca o conteudo inteiro");
        free(back);

        size_t ntmp = 0;
        char **files = dir_list("build", &ntmp);
        int tmp_sobrando = 0;
        for (size_t i = 0; files && i < ntmp; i++)
            if (strstr(files[i], "test-atomic.json.") && strstr(files[i], ".tmp")) tmp_sobrando++;
        if (files) dir_list_free(files, ntmp);
        ok(tmp_sobrando == 0, "nenhum temporario fica para tras");

        ok(!file_write_atomic("build/pasta-que-nao-existe/x.json", "x", 1),
           "falha sem crash quando a pasta nao existe");
        remove(p);
    }

    /* ---------------- dialeto ---------------- */
    printf("[9] dialeto (opcoes, nota do critico, placar)\n");
    {
        DialetoOpts o;
        const char *brief = NULL;
        Str err; str_init(&err);

        ok(dialeto_opts(&o, "melhorar o parser", &brief, &err), "opts sem flag");
        ok(o.rodadas_min == 0 && o.rodadas_max == 0, "padrao = infinito (sem piso nem teto)");
        ok(o.meta == 0, "padrao = sem meta de encerramento automatico");
        ok(o.copia_critico == 1, "critico roda em copia isolada por padrao");
        ok(str_eq(brief, "melhorar o parser"), "brief sem flag e' o texto todo");

        str_clear(&err);
        ok(dialeto_opts(&o, "--rodadas 12 arrumar o loop", &brief, &err), "opts com --rodadas");
        ok(o.rodadas_min == 12 && o.rodadas_max == 12, "--rodadas fixa piso e teto");
        ok(str_eq(brief, "arrumar o loop"), "brief depois das flags");

        str_clear(&err);
        ok(dialeto_opts(&o, "--max 30 arrumar", &brief, &err), "opts com --max");
        ok(o.rodadas_max == 30, "--max so' poe teto");
        ok(str_eq(brief, "arrumar"), "--max nao engole o objetivo");

        /* --meta, --sem-copia e --critico sairam no /dialeto simplificado:
           agora tem de falhar como flag desconhecida, nao ser ignoradas */
        str_clear(&err);
        ok(!dialeto_opts(&o, "--meta 8.5 x", &brief, &err), "--meta removida falha");
        ok(err.len > 0, "--meta removida explica o erro");

        str_clear(&err);
        ok(!dialeto_opts(&o, "--rodadas 2 x", &brief, &err), "--rodadas abaixo do minimo falha");
        ok(err.len > 0, "erro explicado");
        str_clear(&err);
        ok(!dialeto_opts(&o, "--inventada x", &brief, &err), "flag desconhecida falha");
        str_clear(&err);
        ok(dialeto_opts(&o, "", &brief, &err) && *brief == 0, "vazio: brief vazio (vira uso)");
        str_free(&err);

        char *slug = dialeto_slug("Melhorar o /dialeto! (v2)");
        ok(str_eq(slug, "melhorar-o-dialeto-v2"), "slug limpo");
        free(slug);
        slug = dialeto_slug("");
        ok(str_eq(slug, "projeto"), "slug vazio vira 'projeto'");
        free(slug);
    }
    {
        DialetoNota n;
        const char *ok_txt =
            "Nada de bom no codigo.\n\nBLOQUEIO: main.c:88 - perde dado no flush\n"
            "CRITICO: usuario que usa acento quebra\nPROXIMO: corrigir o flush\nNOTA: 6.5\n";
        ok(dialeto_nota(ok_txt, &n) == 1, "acha NOTA");
        ok(fabs(n.nota - 6.5) < 1e-9, "nota 6.5");
        ok(n.bloqueios == 1 && n.criticos == 1, "conta bloqueio e critico");

        ok(dialeto_nota("NOTA: 9.5\nBLOQUEIO: nenhum\n", &n) == 1, "acha NOTA sem bloqueio");
        ok(n.bloqueios == 0, "'BLOQUEIO: nenhum' nao conta");

        ok(dialeto_nota("**NOTA FINAL:** 8,5 de 10\n", &n) == 1, "nota com marcacao e virgula");
        ok(fabs(n.nota - 8.5) < 1e-9, "8,5 vira 8.5");

        ok(dialeto_nota("sem numero nenhum aqui\n", &n) == 0, "sem NOTA = parse falhou");
        ok(n.nota == 0 && n.bloqueios == 1, "sem nota conta como bloqueio, nao como 10");

        ok(dialeto_nota("NOTA: 99\n", &n) == 1 && n.nota == 10.0, "nota acima de 10 satura");
    }
    {
        DialetoOpts o; const char *brief = "consertar o flush do editor"; Str err;
        str_init(&err);
        dialeto_opts(&o, "--rodadas 3 --critico gpt-x", &brief, &err);
        str_free(&err);
        Dialeto d; dialeto_init(&d, &o);
        d.rodada = 2;

        char *p = dialeto_prompt_dev(&d, brief, "BLOQUEIO: main.c:10 — vaza memoria");
        ok(p && contains(p, "ARTIFICE"), "prompt do artifice se identifica");
        ok(contains(p, "RODADA 2"), "prompt do artifice traz a rodada");
        ok(contains(p, brief), "prompt do artifice traz o objetivo");
        ok(contains(p, "main.c:10"), "critica anterior entra no prompt");
        ok(contains(p, "MUDOU:") && contains(p, "PROVA:"), "prompt do artifice pede formato");
        free(p);

        p = dialeto_prompt_dev(&d, brief, NULL);
        ok(p && contains(p, "ARTIFICE") && !contains(p, "fila:"),
           "rodada 1 sem critica nao inventa critica");
        free(p);

        p = dialeto_prompt_critico(&d, brief, "mudei o flush", "--- a/main.c\n+++ b/main.c",
                                   "C:/tmp/copia", "prefira nomes curtos");
        ok(p && contains(p, "CRITICO"), "prompt do critico se identifica");
        ok(contains(p, "NOTA:") && contains(p, "BLOQUEIO:"), "critico sabe o formato da nota");
        ok(contains(p, "mudei o flush"), "critico recebe o relato do artifice");
        ok(contains(p, "+++ b/main.c"), "critico recebe o diff");
        ok(contains(p, "C:/tmp/copia"), "critico recebe o cwd da copia");
        ok(contains(p, "prefira nomes curtos"), "critico recebe as preferencias da memoria");
        free(p);

        p = dialeto_relatorio(&d, brief, 3);
        ok(p && contains(p, brief), "relatorio traz o objetivo");
        ok(contains(p, "3"), "relatorio traz as rodadas");
        free(p);

        dialeto_free(&d);
    }
    {
        DialetoOpts o; const char *brief = "x"; Str err; str_init(&err);
        dialeto_opts(&o, "--rodadas 50", &brief, &err);
        str_free(&err);

        Dialeto d; dialeto_init(&d, &o);
        DialetoNota n;
        int parou_antes = 0;
        for (int r = 1; r <= 60; r++) {
            memset(&n, 0, sizeof n);
            n.rodada = r; n.nota = 9.5; n.parse_ok = 1;
            dialeto_registrar(&d, &n);
            if (dialeto_parar(&d)) {
                ok(r == 50, "estoura o teto na rodada 50");
                parou_antes = 1;
                break;
            }
        }
        ok(parou_antes, "loop de 50 rodadas encerra sozinho");
        ok(d.melhor > 9.4, "melhor nota guardada");
        ok(d.placar.len > 0, "placar escrito");
        dialeto_free(&d);

        /* piso maior que o teto de meta: rodada 4 nao pode encerrar antes do piso */
        dialeto_opts(&o, "--min 10 --max 40 --meta 9", &brief, &err);
        Dialeto e; dialeto_init(&e, &o);
        memset(&n, 0, sizeof n);
        n.rodada = 1; n.nota = 10; n.parse_ok = 1;
        dialeto_registrar(&e, &n);
        ok(!dialeto_parar(&e), "nao encerra antes do piso de rodadas");
        dialeto_free(&e);
    }

    printf("\n%d verificacoes, %d falhas\n", checks, failures);
    return failures ? 1 : 0;
}
