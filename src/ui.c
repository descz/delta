/* ui.c - grade de celulas, markdown e componentes do Delta. */
#include "ui.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* Marca de cor para texto ANSI vindo de fora. */
#define ST(f_, b_, a_) ((Style){ (f_), (b_), (a_) })

/* ================================================================== */
/* Grade                                                               */
/* ================================================================== */
static void alloc_grid(Screen *s, int cols, int rows)
{
    size_t n = (size_t)cols * (size_t)rows;
    s->cols = cols; s->rows = rows; s->cap_rows = rows;
    s->cp = (uint32_t *)malloc(n * sizeof(uint32_t));
    s->fg = (uint32_t *)malloc(n * sizeof(uint32_t));
    s->bg = (uint32_t *)malloc(n * sizeof(uint32_t));
    s->at = (uint8_t *)malloc(n);
    if (!s->cp || !s->fg || !s->bg || !s->at) { fprintf(stderr, "pi: oom\n"); exit(1); }
}

void screen_ensure_rows(Screen *s, int rows)
{
    if (rows <= s->cap_rows) {
        if (rows > s->rows) {
            /* inicializa as novas linhas visiveis */
            for (int y = s->rows; y < rows; y++)
                for (int x = 0; x < s->cols; x++) {
                    size_t idx = (size_t)y * s->cols + x;
                    s->cp[idx] = ' '; s->fg[idx] = COL_DEFAULT; s->bg[idx] = COL_DEFAULT; s->at[idx] = 0;
                }
            s->rows = rows;
        }
        return;
    }
    int new_cap = s->cap_rows ? s->cap_rows : 64;
    while (new_cap < rows) new_cap *= 2;
    size_t n = (size_t)s->cols * (size_t)new_cap;
    s->cp = (uint32_t *)realloc(s->cp, n * sizeof(uint32_t));
    s->fg = (uint32_t *)realloc(s->fg, n * sizeof(uint32_t));
    s->bg = (uint32_t *)realloc(s->bg, n * sizeof(uint32_t));
    s->at = (uint8_t *)realloc(s->at, n);
    if (!s->cp || !s->fg || !s->bg || !s->at) { fprintf(stderr, "pi: oom\n"); exit(1); }
    for (int y = s->rows; y < new_cap; y++)
        for (int x = 0; x < s->cols; x++) {
            size_t idx = (size_t)y * s->cols + x;
            s->cp[idx] = ' '; s->fg[idx] = COL_DEFAULT; s->bg[idx] = COL_DEFAULT; s->at[idx] = 0;
        }
    s->cap_rows = new_cap;
    if (rows > s->rows) s->rows = rows;
}

void screen_set_grow(Screen *s, int grow) { s->grow = grow; }

void screen_blit(Screen *dst, const Screen *src, int src_y, int dst_y, int h)
{
    for (int i = 0; i < h; i++) {
        int sy = src_y + i;
        int dy = dst_y + i;
        if (dy < 0 || dy >= dst->rows) continue;
        for (int x = 0; x < dst->cols; x++) {
            size_t di = (size_t)dy * dst->cols + x;
            if (sy < 0 || sy >= src->rows || x >= src->cols) {
                dst->cp[di] = ' '; dst->fg[di] = COL_DEFAULT; dst->bg[di] = COL_DEFAULT; dst->at[di] = 0;
                continue;
            }
            size_t si = (size_t)sy * src->cols + x;
            dst->cp[di] = src->cp[si];
            dst->fg[di] = src->fg[si];
            dst->bg[di] = src->bg[si];
            dst->at[di] = src->at[si];
        }
    }
}

void screen_init(Screen *s, int cols, int rows)
{
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    memset(s, 0, sizeof(*s));
    alloc_grid(s, cols, rows);
    screen_clear(s);
}

void screen_free(Screen *s)
{
    free(s->cp); free(s->fg); free(s->bg); free(s->at);
    s->cp = NULL; s->fg = NULL; s->bg = NULL; s->at = NULL;
}

void screen_resize(Screen *s, int cols, int rows)
{
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    if (cols == s->cols && rows <= s->cap_rows) { s->rows = rows; return; }
    screen_free(s);
    alloc_grid(s, cols, rows);
    screen_clear(s);
}

void screen_clear(Screen *s)
{
    size_t n = (size_t)s->cols * (size_t)s->rows;
    for (size_t i = 0; i < n; i++) {
        s->cp[i] = ' ';
        s->fg[i] = COL_DEFAULT;
        s->bg[i] = COL_DEFAULT;
        s->at[i] = 0;
    }
}

void screen_fill_rect(Screen *s, int x, int y, int w, int h, Style st)
{
    if (y < 0 || y >= s->rows) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > s->cols) w = s->cols - x;
    for (int i = 0; i < w; i++) {
        int yy0 = y < 0 ? 0 : y;
        int yy1 = y + h > s->rows ? s->rows : y + h;
        for (int yy = yy0; yy < yy1; yy++) {
            size_t idx = (size_t)yy * s->cols + (x + i);
            s->cp[idx] = ' ';
            s->fg[idx] = st.fg;
            s->bg[idx] = st.bg;
            s->at[idx] = st.a;
        }
    }
}

void screen_put(Screen *s, int x, int y, uint32_t cp, Style st)
{
    if (y < 0) return;
    if (y >= s->rows) {
        if (!s->grow) return;
        screen_ensure_rows(s, y + 1);
    }
    if (x < 0 || x >= s->cols) return;
    size_t idx = (size_t)y * s->cols + x;
    s->cp[idx] = cp;
    s->fg[idx] = st.fg;
    s->bg[idx] = st.bg;
    s->at[idx] = st.a;
}

int screen_text(Screen *s, int x, int y, const char *text, size_t n, Style st, int clip)
{
    if (y < 0) return 0;
    if (y >= s->rows) {
        if (!s->grow) return 0;
        screen_ensure_rows(s, y + 1);
    }
    int col = x;
    size_t i = 0;
    while (i < n && text[i]) {
        if (clip >= 0 && col - x >= clip) break;
        if (col >= s->cols) break;
        uint32_t cp;
        int c = utf8_decode(text + i, n - i, &cp);
        if (c <= 0) break;
        i += (size_t)c;

        /* tab: avanca ate o proximo multiplo de 4 a partir do inicio do texto */
        if (cp == '\t') {
            int next = x + (((col - x) / 4) + 1) * 4;
            while (col < next && (clip < 0 || col - x < clip) && col < s->cols)
                screen_put(s, col++, y, ' ', st);
            continue;
        }
        /* controles (inclui '\n' e '\r') nunca viram celula: antes eles
           sobrescreviam o caractere anterior por serem largura zero */
        if (cp < 0x20 || cp == 0x7F) continue;

        int w = cp_width(cp);
        if (w == 0) {
            /* combinante: aplica na celula anterior */
            if (col - 1 >= 0) screen_put(s, col - 1, y, cp, st);
            continue;
        }
        screen_put(s, col, y, cp, st);
        if (w == 2 && col + 1 < s->cols) {
            screen_put(s, col + 1, y, 0, st);   /* continuacao do caractere wide */
        }
        col += w;
    }
    return col - x;
}

int screen_texts(Screen *s, int x, int y, const char *text, Style st, int clip)
{
    return screen_text(s, x, y, text, text ? strlen(text) : 0, st, clip);
}

void screen_hline(Screen *s, int y, int x, int w, uint32_t cp, Style st)
{
    for (int i = 0; i < w; i++) screen_put(s, x + i, y, cp, st);
}

/* Texto com sequencias SGR simples (usado no rodape). */
static void ansi_text(Screen *s, int x, int y, int max_cols, const char *text, Style base)
{
    Style cur = base;
    int col = x;
    size_t i = 0, n = text ? strlen(text) : 0;
    while (i < n && text[i]) {
        if (text[i] == 0x1B && i + 1 < n && text[i + 1] == '[') {
            size_t j = i + 2;
            Str params; str_init(&params);
            while (j < n && text[j] != 'm') { str_addc(&params, text[j]); j++; }
            if (j < n) j++;
            /* aplica */
            const char *p = params.data ? params.data : "";
            if (str_eq(p, "0")) { cur = base; }
            else if (str_eq(p, "1")) cur.a |= ATTR_BOLD;
            else if (str_eq(p, "2")) cur.a |= ATTR_DIM;
            else if (str_eq(p, "3")) cur.a |= ATTR_ITALIC;
            else if (str_eq(p, "4")) cur.a |= ATTR_UNDERLINE;
            else if (str_eq(p, "22")) cur.a &= (uint8_t)~ATTR_DIM;
            else if (str_eq(p, "39")) cur.fg = base.fg;
            else if (str_eq(p, "49")) cur.bg = base.bg;
            else if (str_starts(p, "38;2;") || str_starts(p, "48;2;")) {
                unsigned r = 0, g = 0, b = 0;
                int fg = (p[1] == '8');
                if (sscanf(p + 5, "%u;%u;%u", &r, &g, &b) == 3) {
                    uint32_t c = (r << 16) | (g << 8) | b;
                    if (fg) cur.fg = c; else cur.bg = c;
                }
            } else if (str_starts(p, "38;5;") || str_starts(p, "48;5;")) {
                int idx = atoi(p + 5);
                if (idx >= 0 && idx < 256) {
                    /* aproxima a paleta 256 */
                    static const uint32_t base16[16] = {
                        0x000000,0x800000,0x008000,0x808000,0x000080,0x800080,0x008080,0xc0c0c0,
                        0x808080,0xff0000,0x00ff00,0xffff00,0x0000ff,0xff00ff,0x00ffff,0xffffff
                    };
                    uint32_t c;
                    if (idx < 16) c = base16[idx];
                    else if (idx < 232) {
                        int v = idx - 16;
                        int r = v / 36, g = (v / 6) % 6, b = v % 6;
                        c = (uint32_t)((r ? r * 40 + 55 : 0) << 16 | (g ? g * 40 + 55 : 0) << 8 | (b ? b * 40 + 55 : 0));
                    } else {
                        int v = 8 + (idx - 232) * 10;
                        c = (uint32_t)((v << 16) | (v << 8) | v);
                    }
                    if (p[1] == '8') cur.fg = c; else cur.bg = c;
                }
            }
            str_free(&params);
            i = j;
            continue;
        }
        if (max_cols >= 0 && col - x >= max_cols) return;
        uint32_t cp;
        int c = utf8_decode(text + i, n - i, &cp);
        if (c <= 0) break;
        i += (size_t)c;
        if (cp < 0x20 || cp == 0x7F) continue;
        int w = cp_width(cp);
        if (w == 0) { if (col > x) screen_put(s, col - 1, y, cp, cur); continue; }
        screen_put(s, col, y, cp, cur);
        col += w;
    }
}

int screen_para(Screen *s, int x, int y, int w, const char *text, size_t n, Style st)
{
    if (w < 1) return 0;
    size_t i = 0;
    int row = 0;
    while (i <= n) {
        /* encontra o fim da linha: '\n' explicito, largura esgotada ou
           o ultimo espaco antes do limite (quebra por palavra) */
        int used = 0;
        size_t start = i;
        size_t quebra = 0;      /* posicao logo apos o ultimo espaco */
        while (i < n) {
            if (text[i] == '\n') break;
            uint32_t cp;
            int c = utf8_decode(text + i, n - i, &cp);
            if (c <= 0) break;
            int cw = cp_width(cp);
            if (cp == '\t') cw = 4 - (used % 4);
            if (used + cw > w) {
                if (quebra > start) i = quebra;   /* volta para o espaco */
                break;
            }
            if (cp == ' ') quebra = i + 1;
            used += cw;
            i += (size_t)c;
        }
        screen_text(s, x, y + row, text + start, i - start, st, w);
        row++;
        if (i >= n) break;
        if (text[i] == '\n') { i++; continue; }        /* quebra explicita */
        if (text[i] == ' ') {
            while (i < n && text[i] == ' ') i++;
        }
    }
    return row;
}

/* ================================================================== */
/* Markdown                                                            */
/* ================================================================== */
/* Cresce em dobro: realloc por caractere era O(n^2) e aparecia como lag em
   mensagens grandes (uma resposta de 4 KB custava ~80 ms por frame). */
static void ul_push(ULine *u, uint32_t cp, Style st)
{
    if (u->n == u->cap) {
        size_t cap = u->cap ? u->cap * 2 : 32;
        u->cp = (uint32_t *)realloc(u->cp, cap * sizeof(uint32_t));
        u->st = (Style *)realloc(u->st, cap * sizeof(Style));
        if (!u->cp || !u->st) { fprintf(stderr, "pi: oom\n"); exit(1); }
        u->cap = cap;
    }
    u->cp[u->n] = cp;
    u->st[u->n] = st;
    u->n++;
}

static void ul_free(ULine *u) { free(u->cp); free(u->st); u->cp = NULL; u->st = NULL; u->n = 0; u->cap = 0; }

/* Compara sem consumir. */
static int at(const char *s, size_t n, size_t i, const char *lit)
{
    size_t l = strlen(lit);
    return i + l <= n && memcmp(s + i, lit, l) == 0;
}

static long find_str(const char *s, size_t n, size_t from, const char *needle)
{
    size_t l = strlen(needle);
    if (l == 0) return -1;
    for (size_t i = from; i + l <= n; i++)
        if (memcmp(s + i, needle, l) == 0) return (long)i;
    return -1;
}

static void il_parse(const char *s, size_t n, Style st, const Theme *t, ULine *out)
{
    size_t i = 0;
    while (i < n) {
        char c = s[i];
        if (c == '\\' && i + 1 < n) { ul_push(out, (uint32_t)(unsigned char)s[i + 1], st); i += 2; continue; }

        if (c == '`') {
            long e = find_str(s, n, i + 1, "`");
            if (e > 0) {
                Style sc = st;
                sc.fg = tcolor_hex(theme_tok(t, TK_MD_CODE));
                sc.a |= 0;
                for (size_t k = (size_t)i + 1; k < (size_t)e; k++) {
                    uint32_t cp; int cc = utf8_decode(s + k, (size_t)e - k, &cp);
                    if (cc <= 0) break;
                    ul_push(out, cp, sc);
                    k += (size_t)cc - 1;
                }
                i = (size_t)e + 1;
                continue;
            }
        }
        if (at(s, n, i, "**") || at(s, n, i, "__")) {
            const char *m = at(s, n, i, "**") ? "**" : "__";
            long e = find_str(s, n, i + 2, m);
            if (e > (long)(i + 2)) {
                Style sb = st; sb.a |= ATTR_BOLD;
                il_parse(s + i + 2, (size_t)e - (i + 2), sb, t, out);
                i = (size_t)e + 2;
                continue;
            }
        }
        if (at(s, n, i, "~~")) {
            long e = find_str(s, n, i + 2, "~~");
            if (e > (long)(i + 2)) {
                Style ss = st; ss.a |= ATTR_STRIKE;
                il_parse(s + i + 2, (size_t)e - (i + 2), ss, t, out);
                i = (size_t)e + 2;
                continue;
            }
        }
        if ((c == '*' || c == '_') && i + 1 < n && s[i + 1] != ' ' && s[i + 1] != c) {
            char m[2] = { c, 0 };
            long e = find_str(s, n, i + 1, m);
            if (e > (long)(i + 1)) {
                Style si = st; si.a |= ATTR_ITALIC;
                il_parse(s + i + 1, (size_t)e - (i + 1), si, t, out);
                i = (size_t)e + 1;
                continue;
            }
        }
        if (c == '[') {
            long close = find_str(s, n, i + 1, "](");
            if (close > 0) {
                long paren = find_str(s, n, (size_t)close + 2, ")");
                if (paren > 0) {
                    Style sl = st; sl.fg = tcolor_hex(theme_tok(t, TK_MD_LINK));
                    il_parse(s + i + 1, (size_t)close - (i + 1), sl, t, out);
                    Style su = st; su.fg = tcolor_hex(theme_tok(t, TK_MD_LINK_URL));
                    ul_push(out, ' ', su);
                    ul_push(out, '(', su);
                    for (size_t k = (size_t)close + 2; k < (size_t)paren; k++) {
                        uint32_t cp; int cc = utf8_decode(s + k, (size_t)paren - k, &cp);
                        if (cc <= 0) break;
                        ul_push(out, cp, su);
                        k += (size_t)cc - 1;
                    }
                    ul_push(out, ')', su);
                    i = (size_t)paren + 1;
                    continue;
                }
            }
        }
        if (c == '<' && i + 1 < n) {
            long e = find_str(s, n, i + 1, ">");
            if (e > (long)i && memchr(s + i, ' ', (size_t)e - i) == NULL && memchr(s + i, ':', (size_t)e - i) != NULL) {
                Style sl = st; sl.fg = tcolor_hex(theme_tok(t, TK_MD_LINK_URL));
                for (size_t k = i; k <= (size_t)e; k++) ul_push(out, (uint32_t)(unsigned char)s[k], sl);
                i = (size_t)e + 1;
                continue;
            }
        }
        uint32_t cp;
        int cc = utf8_decode(s + i, n - i, &cp);
        if (cc <= 0) break;
        ul_push(out, cp, st);
        i += (size_t)cc;
    }
}

static void doc_add(Doc *d, ULine *u)
{
    if (d->n == d->cap) {
        size_t cap = d->cap ? d->cap * 2 : 32;
        d->l = (ULine *)realloc(d->l, cap * sizeof(ULine));
        if (!d->l) { fprintf(stderr, "pi: oom\n"); exit(1); }
        d->cap = cap;
    }
    d->l[d->n] = *u;
    d->n++;
    u->cp = NULL; u->st = NULL; u->n = 0; u->cap = 0;
}

void doc_free(Doc *d)
{
    if (!d) return;
    for (size_t i = 0; i < d->n; i++) ul_free(&d->l[i]);
    free(d->l);
    d->l = NULL; d->n = 0; d->cap = 0;
}

/* Quebra a linha em varias, com indentacao pendurada. */
static void flush_wrapped(Doc *out, ULine *u, int width, int indent, int hang, Style base)
{
    if (u->n == 0) {
        ULine e = { 0 };
        doc_add(out, &e);
        return;
    }
    if (width <= indent) width = indent + 1;

    /* Uma passada so: antes, cada quebra copiava o resto da linha para um
       ULine novo (e re-varria tudo para achar o ultimo espaco), o que dava
       O(n^2) num paragrafo longo - era lag visivel numa resposta grande. */
    size_t i = 0;
    int first = 1;
    do {
        int pre = first ? indent : hang;
        size_t start = i;
        int used = pre;
        size_t last_space = (size_t)-1;
        while (i < u->n) {
            uint32_t cp = u->cp[i];
            if (cp == '\n' || cp == '\r') { i++; continue; }
            int cw = cp_width(cp);
            if (used + cw > width && i > start) break;   /* nao cabe: quebra */
            if (cp == ' ') last_space = i;
            used += cw;
            i++;
        }
        size_t end = i;
        if (i < u->n && last_space != (size_t)-1 && last_space > start + (size_t)pre)
            end = last_space;

        ULine l = { 0 };
        for (int k = 0; k < pre; k++) ul_push(&l, ' ', base);
        for (size_t k = start; k < end; k++)
            if (u->cp[k] != '\n' && u->cp[k] != '\r') ul_push(&l, u->cp[k], u->st[k]);
        while (l.n > (size_t)pre && l.cp[l.n - 1] == ' ') l.n--;   /* espacos a direita */
        doc_add(out, &l);

        if (end < i) i = end;                       /* pula o espaco da quebra */
        while (i < u->n && (u->cp[i] == ' ' || u->cp[i] == '\n' || u->cp[i] == '\r')) i++;
        first = 0;
    } while (i < u->n);
}

/* Adiciona uma linha vazia, evitando vazias consecutivas (como o marked faz). */
static void doc_add_blank(Doc *out)
{
    if (out->n > 0 && out->l[out->n - 1].n == 0) return;
    ULine e = { 0 };
    doc_add(out, &e);
}

/* Encontra o fim de um paragrafo (linha em branco ou bloco especial). */
static int line_is_blank(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) if (s[i] != ' ' && s[i] != '\t' && s[i] != '\r') return 0;
    return 1;
}

void md_layout(const Theme *t, const char *text, size_t n, int width, Style base, Doc *out)
{
    memset(out, 0, sizeof(*out));
    if (!text || width < 1) return;

    Style fg_heading   = base; fg_heading.fg = tcolor_hex(theme_tok(t, TK_MD_HEADING));   fg_heading.a |= ATTR_BOLD;
    Style fg_codeblk   = base; fg_codeblk.fg = tcolor_hex(theme_tok(t, TK_MD_CODE_BLOCK));
    Style fg_fence     = base; fg_fence.fg = tcolor_hex(theme_tok(t, TK_MD_CODE_BLOCK_BORDER));
    Style fg_quote     = base; fg_quote.fg = tcolor_hex(theme_tok(t, TK_MD_QUOTE));
    Style fg_quote_b   = base; fg_quote_b.fg = tcolor_hex(theme_tok(t, TK_MD_QUOTE_BORDER));
    Style fg_bullet    = base; fg_bullet.fg = tcolor_hex(theme_tok(t, TK_MD_LIST_BULLET));
    Style fg_hr        = base; fg_hr.fg = tcolor_hex(theme_tok(t, TK_MD_HR));

    int in_code = 0;
    size_t pos = 0;
    ULine para = { 0 };

    while (pos <= n) {
        /* pega a linha */
        size_t eol = pos;
        while (eol < n && text[eol] != '\n') eol++;
        const char *ls = text + pos;
        size_t ll = eol - pos;
        while (ll > 0 && (ls[ll - 1] == '\r')) ll--;
        int is_last = (eol >= n);

        if (in_code) {
            if (ll >= 3 && ls[0] == '`' && ls[1] == '`' && ls[2] == '`') {
                ULine f = { 0 };
                for (int k = 0; k < width && k < 3; k++) ul_push(&f, (uint32_t)'`', fg_fence);
                doc_add(out, &f);
                in_code = 0;
            } else {
                ULine l = { 0 };
                ul_push(&l, ' ', fg_codeblk);
                ul_push(&l, ' ', fg_codeblk);
                for (size_t k = 0; k < ll; k++) {
                    uint32_t cp; int cc = utf8_decode(ls + k, ll - k, &cp);
                    if (cc <= 0) break;
                    ul_push(&l, cp, fg_codeblk);
                    k += (size_t)cc - 1;
                }
                doc_add(out, &l);
            }
            pos = eol + 1;
            continue;
        }

        if (ll >= 3 && ls[0] == '`' && ls[1] == '`' && ls[2] == '`') {
            if (para.n) { flush_wrapped(out, &para, width, 0, 0, base); para.n = 0; free(para.cp); free(para.st); para.cp=NULL; para.st=NULL; }
            ULine f = { 0 };
            for (int k = 0; k < width && k < 3; k++) ul_push(&f, (uint32_t)'`', fg_fence);
            doc_add(out, &f);
            in_code = 1;
            pos = eol + 1;
            continue;
        }

        /* linha em branco separa paragrafos (e deixa a linha em branco visivel) */
        if (line_is_blank(ls, ll)) {
            if (para.n) { flush_wrapped(out, &para, width, 0, 0, base); ul_free(&para); }
            if (!is_last || out->n == 0) doc_add_blank(out);   /* sem linha extra no fim */
            pos = eol + 1;
            if (is_last) break;
            continue;
        }

        /* heading */
        int hlevel = 0;
        while (hlevel < 6 && (size_t)hlevel < ll && ls[hlevel] == '#') hlevel++;
        if (hlevel > 0 && (size_t)hlevel < ll && ls[hlevel] == ' ') {
            if (para.n) { flush_wrapped(out, &para, width, 0, 0, base); ul_free(&para); }
            ULine h = { 0 };
            il_parse(ls + hlevel + 1, ll - hlevel - 1, fg_heading, t, &h);
            flush_wrapped(out, &h, width, 0, 0, base);
            pos = eol + 1;
            continue;
        }

        /* hr */
        {
            int hr = 0;
            char c0 = ll ? ls[0] : 0;
            if ((c0 == '-' || c0 == '*' || c0 == '_') && ll >= 3) {
                hr = 1;
                for (size_t k = 0; k < ll; k++)
                    if (ls[k] != c0 && ls[k] != ' ') { hr = 0; break; }
            }
            if (hr) {
                if (para.n) { flush_wrapped(out, &para, width, 0, 0, base); ul_free(&para); }
                ULine h = { 0 };
                for (int k = 0; k < width; k++) ul_push(&h, 0x2500, fg_hr);
                doc_add(out, &h);
                pos = eol + 1;
                if (is_last) break;
                continue;
            }
        }

        /* blockquote */
        if (ll >= 1 && ls[0] == '>') {
            if (para.n) { flush_wrapped(out, &para, width, 0, 0, base); ul_free(&para); }
            size_t off = 1;
            while (off < ll && ls[off] == ' ') off++;
            ULine q = { 0 };
            ul_push(&q, 0x2502, fg_quote_b);   /* │ */
            ul_push(&q, ' ', fg_quote_b);
            il_parse(ls + off, ll - off, fg_quote, t, &q);
            flush_wrapped(out, &q, width, 0, 2, base);
            pos = eol + 1;
            if (is_last) break;
            continue;
        }

        /* lista */
        {
            size_t off = 0;
            int is_list = 0, ordered = 0;
            size_t numlen = 0;
            while (off < ll && ls[off] == ' ') off++;
            if (off + 2 <= ll && (ls[off] == '-' || ls[off] == '*' || ls[off] == '+') && ls[off + 1] == ' ') {
                is_list = 1; ordered = 0;
            } else {
                size_t k = off;
                while (k < ll && ls[k] >= '0' && ls[k] <= '9') k++;
                if (k > off && k + 1 < ll && (ls[k] == '.' || ls[k] == ')') && ls[k + 1] == ' ') {
                    is_list = 1; ordered = 1; numlen = k - off;
                }
            }
            if (is_list) {
                if (para.n) { flush_wrapped(out, &para, width, 0, 0, base); ul_free(&para); }
                ULine l = { 0 };
                int ind = (int)off;
                for (int k = 0; k < ind; k++) ul_push(&l, ' ', base);
                if (ordered) {
                    char num[16];
                    snprintf(num, sizeof num, "%zu. ", numlen);
                    for (const char *p = num; *p; p++) ul_push(&l, (uint32_t)(unsigned char)*p, fg_bullet);
                    il_parse(ls + off + numlen + 2, ll - off - numlen - 2, base, t, &l);
                    flush_wrapped(out, &l, width, 0, ind + (int)strlen(num), base);
                } else {
                    ul_push(&l, 0x2022, fg_bullet);   /* • */
                    ul_push(&l, ' ', fg_bullet);
                    il_parse(ls + off + 2, ll - off - 2, base, t, &l);
                    flush_wrapped(out, &l, width, 0, ind + 2, base);
                }
                pos = eol + 1;
                if (is_last) break;
                continue;
            }
        }

        /* paragrafo: acumula ate a proxima linha em branco ou bloco */
        if (para.n) ul_push(&para, ' ', base);
        il_parse(ls, ll, base, t, &para);
        pos = eol + 1;
        if (is_last) { flush_wrapped(out, &para, width, 0, 0, base); ul_free(&para); }
    }
    if (in_code) { /* fence nao fechado */ }
}

int doc_draw(Screen *s, int x, int y, int width, const Doc *d, Style base)
{
    if (!d) return 0;
    for (size_t i = 0; i < d->n; i++) {
        const ULine *l = &d->l[i];
        int col = 0;
        for (size_t k = 0; k < l->n; k++) {
            uint32_t cp = l->cp[k];
            Style st = l->st[k];
            if (cp < 0x20 || cp == 0x7F) continue;
            if (st.bg == COL_DEFAULT) st.bg = base.bg;
            int cw = cp_width(cp);
            if (cp == 0) { col += 1; continue; }
            if (col >= width) break;
            screen_put(s, x + col, y + (int)i, cp, st);
            col += cw;
        }
        /* preenche o resto da linha com o fundo (importante para mensagens do usuario) */
        if (base.bg != COL_DEFAULT) {
            for (; col < width; col++) {
                Style st = base;
                screen_put(s, x + col, y + (int)i, ' ', st);
            }
        }
    }
    return (int)d->n;
}

/* ================================================================== */
/* Componentes                                                         */
/* ================================================================== */
int ui_user_message(Screen *s, int y, int width, const Theme *t, const char *text, size_t n)
{
    Style base;
    base.bg = tcolor_hex(theme_tok(t, TK_USER_MESSAGE_BG));
    base.fg = tcolor_hex(theme_tok(t, TK_USER_MESSAGE_TEXT));
    base.a = 0;

    Doc d;
    md_layout(t, text, n, width - 4, base, &d);
    int h = (int)d.n + 2;

    /* padding vertical + fundo full width */
    for (int i = 0; i < h; i++) {
        for (int c = 0; c < width; c++) screen_put(s, c, y + i, ' ', base);
    }
    doc_draw(s, 2, y + 1, width - 4, &d, base);
    doc_free(&d);
    return h;
}

int ui_assistant_message(Screen *s, int y, int width, const Theme *t, const char *text, size_t n,
                         int streaming)
{
    (void)streaming;
    Style base;
    base.bg = COL_DEFAULT;
    base.fg = tcolor_hex(theme_tok(t, TK_TEXT));
    base.a = 0;

    Doc d;
    md_layout(t, text, n, width - 4, base, &d);
    doc_draw(s, 2, y, width - 4, &d, base);
    int h = (int)d.n;
    if (h == 0) h = 1;
    doc_free(&d);
    return h;
}

int ui_box_begin(Screen *s, int y, int width, uint32_t bg)
{
    Style st = { COL_DEFAULT, bg, 0 };
    for (int c = 0; c < width; c++) screen_put(s, c, y, ' ', st);
    return y + 1;
}

void ui_box_end(Screen *s, int y0, int y1, int width, uint32_t bg)
{
    Style st = { COL_DEFAULT, bg, 0 };
    for (int yy = y0; yy <= y1; yy++)
        for (int c = 0; c < width; c++) screen_put(s, c, yy, ' ', st);
}

int ui_tool_box(Screen *s, int y, int width, const Theme *t, int state,
                const char *title, const char *args_preview,
                const char *output, size_t output_len, int max_output_lines,
                const char *dur)
{
    uint32_t bg = tcolor_hex(theme_tok(t, state == 2 ? TK_TOOL_ERROR_BG : (state == 1 ? TK_TOOL_SUCCESS_BG : TK_TOOL_PENDING_BG)));
    Style st_title = { tcolor_hex(theme_tok(t, TK_TOOL_TITLE)), bg, ATTR_BOLD };
    Style st_args  = { tcolor_hex(theme_tok(t, TK_TOOL_OUTPUT)), bg, 0 };
    Style st_out   = { tcolor_hex(theme_tok(t, TK_TOOL_OUTPUT)), bg, 0 };

    /* O bloco tem o proprio respiro: 1 linha de fundo em cima do titulo e 1
       embaixo do output (antes o titulo encostava no topo e sobrava um vazio
       duplo na base, porque a quebra de linha final do output era contada
       como uma linha a mais). */
    int y0 = y;
    int hdr = y0 + 1;

    size_t corpo = output ? output_len : 0;
    while (corpo > 0 && (output[corpo - 1] == '\n' || output[corpo - 1] == '\r' || output[corpo - 1] == '\0'))
        corpo--;

    int body_lines = 0;
    if (corpo > 0) {
        for (size_t i = 0; i < corpo; i++) if (output[i] == '\n') body_lines++;
        body_lines++;
        if (max_output_lines > 0 && body_lines > max_output_lines) body_lines = max_output_lines;
    }

    int h = 3 + body_lines;      /* respiro + titulo + output + respiro */
    ui_box_end(s, y0, y0 + h - 1, width, bg);

    /* titulo + args (mesmo recuo das outras caixas: 2 colunas), com a
       duracao encostada a direita dentro da propria caixa */
    int durw = (dur && *dur) ? (int)strlen(dur) : 0;
    if (durw && width < 24) durw = 0;        /* sem espaco: omite o tempo */
    int dir = width - 2;                     /* borda direita (2 colunas de recuo) */
    int titulo_ate = width - 4 - (durw ? durw + 2 : 0);
    if (titulo_ate < 4) titulo_ate = 4;

    int col = 2;
    col += screen_text(s, col, hdr, title, strlen(title), st_title, titulo_ate - 2);
    if (args_preview && *args_preview && col < titulo_ate - 1) {
        col += screen_text(s, col, hdr, " ", 1, st_args, titulo_ate - 1 - col);
        screen_text(s, col, hdr, args_preview, strlen(args_preview), st_args, titulo_ate - 1 - col);
    }
    if (durw) {
        Style st_dur = { tcolor_hex(theme_tok(t, TK_DIM)), bg, 0 };
        screen_text(s, dir - durw, hdr, dur, strlen(dur), st_dur, durw);
    }

    /* output: comeca na linha seguinte ao titulo */
    if (corpo > 0 && body_lines > 0) {
        int written = 0;
        size_t i = 0;
        while (i < corpo && written < body_lines) {
            size_t eol = i;
            while (eol < corpo && output[eol] != '\n') eol++;
            size_t ll = eol - i;
            while (ll > 0 && output[i + ll - 1] == '\r') ll--;
            screen_text(s, 2, hdr + 1 + written, output + i, ll, st_out, width - 4);
            written++;
            if (eol >= corpo) break;
            i = eol + 1;
        }
    }
    return h;
}

/* ================================================================== */
/* Editor                                                              */
/* ================================================================== */
static const char *SPIN[10] = { "\u280b", "\u2819", "\u2839", "\u2838", "\u283c",
                                "\u2834", "\u2826", "\u2827", "\u2807", "\u280f" };

/* Quebra um trecho em linhas visuais de `w` colunas. Devolve o numero de
   linhas e, se `cursor` cair no trecho, a linha/coluna do cursor. */
static int input_layout(const char *text, size_t len, int w, size_t cursor,
                        size_t *cur_row, size_t *cur_col, size_t **starts, size_t **ends)
{
    if (w < 1) w = 1;
    size_t *st = (size_t *)malloc((len + 2) * sizeof(size_t));
    size_t *en = (size_t *)malloc((len + 2) * sizeof(size_t));
    int rows = 0;
    size_t i = 0;
    while (i <= len) {
        size_t start = i;
        int used = 0;
        while (i < len && text[i] != '\n') {
            uint32_t cp;
            int c = utf8_decode(text + i, len - i, &cp);
            if (c <= 0) break;
            int cw = cp_width(cp);
            if (used + cw > w) break;          /* nao cabe: continua embaixo */
            used += cw;
            i += (size_t)c;
        }
        if (cursor >= start && cursor <= i) {
            if (cur_row) *cur_row = (size_t)rows;
            if (cur_col) *cur_col = (size_t)strn_width(text + start, cursor - start);
        }
        st[rows] = start;
        en[rows] = i;
        rows++;
        if (i >= len) break;
        if (text[i] == '\n') { i++; continue; }
    }
    if (rows == 0) { st[0] = 0; en[0] = 0; rows = 1; }
    *starts = st;
    *ends = en;
    return rows;
}

int ui_input_rows(const char *text, size_t len, int width)
{
    size_t *st = NULL, *en = NULL;
    int rows = input_layout(text, len, width - 4, 0, NULL, NULL, &st, &en);
    free(st);
    free(en);
    return rows;
}

/* Desenha um trecho, em negrito no placeholder de colagem. */
static void draw_input(Screen *s, int x, int y, const char *t, size_t n, int maxw, Style base,
                       const char **negrito, int n_negrito)
{
    Style forte = base;
    forte.a |= ATTR_BOLD;
    int col = 0;
    size_t i = 0;
    int dentro = 0;
    while (i < n) {
        uint32_t cp;
        int c = utf8_decode(t + i, n - i, &cp);
        if (c <= 0) break;
        if (!dentro && i + 7 <= n && memcmp(t + i, "{paste ", 7) == 0) dentro = 1;
        /* caminhos destacados (imagens coladas) saem em negrito */
        if (!dentro) {
            for (int k = 0; k < n_negrito; k++) {
                size_t l = strlen(negrito[k]);
                if (l && i + l <= n && memcmp(t + i, negrito[k], l) == 0) { dentro = 2; break; }
            }
        }
        int cw = cp_width(cp);
        if (col + cw > maxw) break;
        screen_put(s, x + col, y, cp, dentro ? forte : base);
        col += cw;
        i += (size_t)c;
        if (dentro == 1 && cp == '}') dentro = 0;
        if (dentro == 2) {
            for (int k = 0; k < n_negrito; k++) {
                size_t l = strlen(negrito[k]);
                if (l && i >= l && memcmp(t + i - l, negrito[k], l) == 0) { dentro = 0; break; }
            }
        }
    }
}

int ui_editor(Screen *s, int y, int width, const Theme *t,
              const char *text, size_t text_len, size_t cursor,
              int working, const char *work_msg, int spin_frame, int bash_mode,
              int max_rows, const char **negrito, int n_negrito)
{
    uint32_t border_fg = tcolor_hex(theme_tok(t, bash_mode ? TK_BASH_MODE : TK_BORDER_MUTED));
    Style st_border = { border_fg, COL_DEFAULT, 0 };
    Style st_text   = { tcolor_hex(theme_tok(t, TK_TEXT)), COL_DEFAULT, 0 };

    /* --- contorno superior: primeiro a linha, depois o status por cima
       (embedWorkingStatus), senao o hline apaga o indicador --- */
    screen_hline(s, y, 1, width - 2, 0x2500, st_border);
    screen_put(s, 0, y, 0x256D, st_border);            /* ╭ */
    screen_put(s, width - 1, y, 0x256E, st_border);    /* ╮ */
    if (working && width > 12) {
        const char *sp = SPIN[((spin_frame % 10) + 10) % 10];
        int msgw = work_msg ? str_width(work_msg) : 0;
        int statusw = 1 + 1 + msgw;
        if (width >= statusw + 8) {
            Style st_sp = { tcolor_hex(theme_tok(t, TK_ACCENT)), COL_DEFAULT, 0 };
            Style st_msg = { tcolor_hex(theme_tok(t, TK_MUTED)), COL_DEFAULT, 0 };
            int col = 1;
            col += screen_text(s, col, y, "\u2500 ", 4, st_border, -1);
            col += screen_text(s, col, y, sp, strlen(sp), st_sp, -1);
            col += screen_text(s, col, y, " ", 1, st_border, -1);
            col += screen_text(s, col, y, work_msg ? work_msg : "",
                               work_msg ? strlen(work_msg) : 0, st_msg, -1);
            if (col < width - 1) screen_text(s, col, y, " ", 1, st_border, -1);
        }
    }

    /* --- conteudo: barras laterais + quebra automatica --- */
    int row = y + 1;
    int content_w = width - 4;
    if (content_w < 1) content_w = 1;

    size_t *starts = NULL, *ends = NULL;
    size_t cur_row = 0, cur_col = 0;
    int rows = input_layout(text, text_len, content_w, cursor, &cur_row, &cur_col, &starts, &ends);

    int maxr = (max_rows > 0 && rows > max_rows) ? max_rows : rows;
    size_t first = 0;
    if (rows > maxr && (int)cur_row >= maxr) {
        first = cur_row - (size_t)maxr + 1;
        if (first + (size_t)maxr > (size_t)rows) first = (size_t)rows - (size_t)maxr;
    }

    for (int k = 0; k < maxr; k++) {
        size_t idx = first + (size_t)k;
        if (idx >= (size_t)rows) break;
        screen_put(s, 0, row + k, 0x2502, st_border);
        draw_input(s, 2, row + k, text + starts[idx], ends[idx] - starts[idx], content_w, st_text,
                   negrito, n_negrito);
        screen_put(s, width - 1, row + k, 0x2502, st_border);
    }
    free(starts);
    free(ends);

    /* --- cursor --- */
    {
        Style st_cur = { tcolor_hex(theme_tok(t, TK_TEXT)),
                         tcolor_hex(theme_tok(t, TK_SELECTED_BG)), 0 };
        int cx = 2 + (int)cur_col;
        int cy = row + (int)(cur_row - first);
        if (cx > s->cols - 3) cx = s->cols - 3;
        if (cx < 2) cx = 2;
        screen_put(s, cx, cy, ' ', st_cur);
    }
    row += maxr;

    /* --- contorno inferior --- */
    screen_hline(s, row, 1, width - 2, 0x2500, st_border);
    screen_put(s, 0, row, 0x2570, st_border);           /* ╰ */
    screen_put(s, width - 1, row, 0x256F, st_border);   /* ╯ */
    return (row - y) + 1;
}

/* ================================================================== */
/* Rodape                                                              */
/* ================================================================== */
int ui_footer(Screen *s, int y, int width, const Theme *t,
              const char *pwd, const char *stats_left, const char *model_right,
              const char *ext_status)
{
    Style st_dim = { tcolor_hex(theme_tok(t, TK_DIM)), COL_DEFAULT, 0 };

    /* linha 1: pwd */
    screen_text(s, 0, y, pwd, strlen(pwd), st_dim, width);

    /* linha 2: stats (esquerda) + modelo (direita) */
    int y2 = y + 1;
    int lw = str_width(stats_left);
    int rw = str_width(model_right);
    if (lw > width) lw = width;
    if (lw + 2 + rw <= width) {
        ansi_text(s, 0, y2, width, stats_left, st_dim);
        screen_text(s, width - rw, y2, model_right, strlen(model_right), st_dim, rw);
    } else {
        /* nao cabe: trunca o lado direito em vez de sumir com ele */
        int avail = width - lw - 2;
        if (avail >= 12) {
            ansi_text(s, 0, y2, lw, stats_left, st_dim);
            screen_text(s, width - avail, y2, model_right, strlen(model_right), st_dim, avail);
        } else {
            ansi_text(s, 0, y2, width, stats_left, st_dim);
        }
    }

    int lines = 2;
    if (ext_status && *ext_status) {
        screen_text(s, 0, y + 2, ext_status, strlen(ext_status), st_dim, width);
        lines = 3;
    }
    return lines;
}

/* ================================================================== */
/* Borda com rotulo (usada no lugar do status quando um seletor abre)  */
/* ================================================================== */
void ui_border_label(Screen *s, int y, int width, const Theme *t, const char *label)
{
    Style st_border = { tcolor_hex(theme_tok(t, TK_BORDER_MUTED)), COL_DEFAULT, 0 };
    Style st_label  = { tcolor_hex(theme_tok(t, TK_ACCENT)), COL_DEFAULT, ATTR_BOLD };
    screen_hline(s, y, 1, width - 2, 0x2500, st_border);
    int col = 1;
    if (label && *label) {
        col += screen_text(s, col, y, "\u2500 ", 4, st_border, -1);
        col += screen_text(s, col, y, label, strlen(label), st_label, width - col - 3);
        if (col < width - 1) screen_text(s, col, y, " ", 1, st_border, -1);
    }
    screen_put(s, 0, y, 0x256D, st_border);            /* ╭ */
    screen_put(s, width - 1, y, 0x256E, st_border);    /* ╮ */
}

/* ================================================================== */
/* Menu (comandos / modelos / provedores)                              */
/* ================================================================== */
int ui_menu(Screen *s, int y, int width, const Theme *t,
            const MenuItem *items, int n, int sel, int scroll, int max_rows,
            int name_col, const char *counter)
{
    if (n <= 0) return 0;
    if (max_rows < 1) max_rows = 1;
    if (max_rows > n) max_rows = n;
    if (scroll < 0) scroll = 0;
    if (sel < scroll) scroll = sel;
    if (sel >= scroll + max_rows) scroll = sel - max_rows + 1;
    if (scroll > n - max_rows) scroll = n - max_rows;
    if (scroll < 0) scroll = 0;

    uint32_t accent = tcolor_hex(theme_tok(t, TK_ACCENT));
    uint32_t text   = tcolor_hex(theme_tok(t, TK_TEXT));
    uint32_t muted  = tcolor_hex(theme_tok(t, TK_MUTED));
    uint32_t dim    = tcolor_hex(theme_tok(t, TK_DIM));

    /* largura da coluna de nomes */
    if (name_col <= 0) {
        name_col = 12;
        for (int i = 0; i < n; i++) {
            int w = items[i].name ? str_width(items[i].name) : 0;
            if (w > name_col) name_col = w;
        }
        if (name_col > 34) name_col = 34;
        name_col += 2;
    }

    int row = y;
    for (int i = scroll; i < scroll + max_rows; i++) {
        const MenuItem *it = &items[i];
        int selected = (i == sel);

        Style st_arrow = { selected ? accent : COL_DEFAULT, COL_DEFAULT, 0 };
        Style st_name  = { selected ? accent : text, COL_DEFAULT, selected ? ATTR_BOLD : 0 };
        Style st_desc  = { muted, COL_DEFAULT, 0 };

        if (selected) {
            /* marcador como no Delta: "→ " */
            screen_text(s, 0, row, "→ ", 4, st_arrow, 2);
        } else {
            screen_text(s, 0, row, "  ", 2, st_arrow, 2);
        }
        int used = screen_text(s, 2, row, it->name ? it->name : "", it->name ? strlen(it->name) : 0,
                               st_name, name_col - 1);
        int pad = name_col - used;
        if (pad < 1) pad = 1;
        if (2 + used + pad < width && it->desc && *it->desc) {
            screen_text(s, 2 + used + pad, row, it->desc, strlen(it->desc), st_desc,
                        width - (2 + used + pad));
        }
        row++;
    }

    if (counter && *counter) {
        Style st_c = { dim, COL_DEFAULT, 0 };
        screen_text(s, 2, row, counter, strlen(counter), st_c, width - 2);
        row++;
    }
    return row - y;
}

/* ================================================================== */
/* Boas-vindas                                                         */
/* ================================================================== */
int ui_welcome(Screen *s, int y, int width, const Theme *t, const char *model, const char *cwd)
{
    /* Logo do Delta Agent: um Δ de 8x6 pixels em meios blocos (▀ ▄ █),
       tres linhas de altura — a mesma pixel art de assets/ascii-delta.mjs
       (--style=block). Nome, modelo e cwd alinhados a direita do logo. */
    static const char *const LOGO[3] = {
        "   ▄▄",
        " ▄█▀▀█▄",
        "██▄▄▄▄██",
    };
    enum { LOGO_X = 1, LOGO_W = 9, TEXTO_X = 10 };   /* logo ocupa 1..8 */

    uint32_t cor_logo = t->mono ? tcolor_hex(theme_tok(t, TK_TEXT))
                                : tcolor_hex(theme_tok(t, TK_ACCENT));
    uint32_t cor_texto = tcolor_hex(theme_tok(t, TK_TEXT));

    Style st_logo = { cor_logo, COL_DEFAULT, ATTR_BOLD };
    Style st_nome = { cor_texto, COL_DEFAULT, ATTR_BOLD };
    Style st_muted = { tcolor_hex(theme_tok(t, TK_MUTED)), COL_DEFAULT, 0 };
    Style st_dim = { tcolor_hex(theme_tok(t, TK_DIM)), COL_DEFAULT, 0 };

    for (int i = 0; i < 3; i++)
        screen_text(s, LOGO_X, y + i, LOGO[i], strlen(LOGO[i]), st_logo, LOGO_W);

    int clip = width > TEXTO_X ? width - TEXTO_X : 0;
    screen_texts(s, TEXTO_X, y, "Delta Agent", st_nome, clip);
    screen_texts(s, TEXTO_X, y + 1, model ? model : "no-model", st_muted, clip);
    screen_texts(s, TEXTO_X, y + 2, cwd ? cwd : "", st_dim, clip);
    return 3;
}
