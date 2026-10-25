/* ui.h - grade de celulas, markdown e componentes visuais do Delta */
#ifndef DELTA_UI_H
#define DELTA_UI_H

#include "util.h"
#include "theme.h"

/* ---------------------------------------------------------------- */
/* Estilo e grade                                                     */
/* ---------------------------------------------------------------- */
#define COL_DEFAULT TCOLOR_DEFAULT   /* cor padrao do terminal */

typedef enum {
    ATTR_BOLD      = 1 << 0,
    ATTR_DIM       = 1 << 1,
    ATTR_ITALIC    = 1 << 2,
    ATTR_UNDERLINE = 1 << 3,
    ATTR_STRIKE    = 1 << 4,
    ATTR_INVERSE   = 1 << 5,
} Attr;

typedef struct {
    uint32_t fg;
    uint32_t bg;
    uint8_t  a;
} Style;

typedef struct {
    int       cols, rows;
    int       cap_rows;   /* linhas alocadas */
    int       grow;       /* cresce quando algo e escrito abaixo da ultima linha */
    uint32_t *cp;
    uint32_t *fg;
    uint32_t *bg;
    uint8_t  *at;
} Screen;

void screen_init(Screen *s, int cols, int rows);
void screen_free(Screen *s);
void screen_resize(Screen *s, int cols, int rows);
void screen_clear(Screen *s);
/* Habilita crescimento automatico (usado para o transcript). */
void screen_set_grow(Screen *s, int grow);
/* Garante que existam pelo menos `rows` linhas. */
void screen_ensure_rows(Screen *s, int rows);
/* Copia h linhas da origem a partir de src_y para o destino em dst_y. */
void screen_blit(Screen *dst, const Screen *src, int src_y, int dst_y, int h);
void screen_fill_rect(Screen *s, int x, int y, int w, int h, Style st);
void screen_put(Screen *s, int x, int y, uint32_t cp, Style st);
/* Desenha texto UTF-8 (sem quebrar). clip<0 = sem limite. Retorna colunas. */
int  screen_text(Screen *s, int x, int y, const char *text, size_t n, Style st, int clip);
int  screen_texts(Screen *s, int x, int y, const char *text, Style st, int clip);
void screen_hline(Screen *s, int y, int x, int w, uint32_t cp, Style st);
/* Desenha texto com quebra de palavra. Retorna linhas usadas. */
int  screen_para(Screen *s, int x, int y, int w, const char *text, size_t n, Style st);

/* ---------------------------------------------------------------- */
/* Bloco de linhas com estilo por caractere (saida do markdown)        */
/* ---------------------------------------------------------------- */
typedef struct { uint32_t *cp; Style *st; size_t n; size_t cap; } ULine;
typedef struct { ULine *l; size_t n; size_t cap; } Doc;

void doc_free(Doc *d);
/* Layout de markdown. bg_for_blank preenche linhas vazias (0 = nao preencher). */
void md_layout(const Theme *t, const char *text, size_t n, int width, Style base, Doc *out);
/* Desenha o doc; retorna a altura. */
int  doc_draw(Screen *s, int x, int y, int width, const Doc *d, Style base);

/* ---------------------------------------------------------------- */
/* Componentes                                                        */
/* ---------------------------------------------------------------- */
/* Caixa com fundo full-width (usada pelos tool executions). */
int ui_box_begin(Screen *s, int y, int width, uint32_t bg);
void ui_box_end(Screen *s, int y0, int y1, int width, uint32_t bg);

/* Mensagem do usuario: fundo userMessageBg, paddingX=1, paddingY=1. */
int ui_user_message(Screen *s, int y, int width, const Theme *t, const char *text, size_t n);

/* Mensagem do assistente: markdown sem fundo, paddingX=1, paddingY=0. */
int ui_assistant_message(Screen *s, int y, int width, const Theme *t, const char *text, size_t n,
                         int streaming);

/* Caixa de ferramenta. state: 0 pendente, 1 sucesso, 2 erro. */
int ui_tool_box(Screen *s, int y, int width, const Theme *t, int state,
                const char *title, const char *args_preview,
                const char *output, size_t output_len, int max_output_lines,
                const char *dur);   /* duracao alinhada a direita no titulo (pode ser NULL) */

/* Quantas linhas visiveis um texto ocupa no editor (quebra automatica). */
int ui_input_rows(const char *text, size_t len, int width);

/* Barra de status embutida na borda do editor. */
int ui_editor(Screen *s, int y, int width, const Theme *t,
              const char *text, size_t text_len, size_t cursor,
              int working, const char *work_msg, int spin_frame, int bash_mode,
              int max_rows, const char **negrito, int n_negrito);

/* Rodape: 2 linhas (pwd + stats / modelo). */
int ui_footer(Screen *s, int y, int width, const Theme *t,
              const char *pwd, const char *stats_left, const char *model_right,
              const char *ext_status);

/* Menu de comandos / seletor de modelos.
 * Desenha ate `max_rows` itens a partir de `scroll`, com o item `sel` destacado,
 * e uma linha final com `counter` (ex.: "(3/24)"). Retorna a altura usada. */
typedef struct {
    const char *name;
    const char *desc;
} MenuItem;

/* Borda do editor com um rotulo no lugar do status. */
void ui_border_label(Screen *s, int y, int width, const Theme *t, const char *label);

/* name_col <= 0 calcula a largura a partir dos itens recebidos. */
int ui_menu(Screen *s, int y, int width, const Theme *t,
            const MenuItem *items, int n, int sel, int scroll, int max_rows,
            int name_col, const char *counter);

/* Cabecalho de boas-vindas (logo + dicas). */
int ui_welcome(Screen *s, int y, int width, const Theme *t, const char *model, const char *cwd);

#endif /* DELTA_UI_H */
