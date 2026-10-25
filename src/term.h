/* term.h - terminal: modo cru, eventos e apresentacao da grade */
#ifndef DELTA_TERM_H
#define DELTA_TERM_H

#include "ui.h"

typedef enum {
    EV_NONE = 0,
    EV_CHAR,        /* cp preenchido */
    EV_ENTER,
    EV_TAB,
    EV_BACKSPACE,
    EV_DELETE,
    EV_LEFT, EV_RIGHT, EV_UP, EV_DOWN,
    EV_HOME, EV_END, EV_PGUP, EV_PGDN,
    EV_ESC,
    EV_CTRL,        /* cp = letra minuscula de controle */
    EV_CTRL_KEY,    /* a tecla Ctrl sozinha (o console cola o texto por conta) */
    EV_RESIZE,
    EV_WHEEL,       /* shift = +1 (cima) / -1 (baixo) */
    EV_CLICK,       /* mx, my = celula clicada */
    EV_DRAG,        /* botao esquerdo apertado se movendo (selecao) */
    EV_RELEASE,     /* botao esquerdo solto (fim da selecao) */
    EV_EOF,
} EvType;

typedef struct {
    EvType   type;
    uint32_t cp;
    int      ctrl, alt, shift;
    int      mx, my;    /* posicao do mouse (EV_CLICK) */
} Event;

int  term_init(void);          /* 1 = ok */
void term_shutdown(void);
void term_size(int *cols, int *rows);
void term_alt_enter(void);
void term_alt_leave(void);
void term_mouse_enable(int on);
/* Espera por um evento (timeout_ms < 0 = infinito). 1 = evento, 0 = timeout */
int  term_read(Event *ev, int timeout_ms);
/* Le de uma vez tudo o que estiver pronto (usado para detectar colagem). */
int  term_read_batch(Event *evs, int max, int timeout_ms);

/* 1 se o usuario acabou de apertar Ctrl+V no teclado real (fallback para
   terminais que consomem o chord e nao entregam evento nenhum, tipo o
   Windows Terminal com imagem/video no clipboard). */
int  term_poll_paste_hotkey(void);
/* Apresenta a grade, escrevendo apenas as linhas que mudaram. */
void term_present(Screen *s);
void term_set_title(const char *title);
void term_set_size(int cols, int rows);
/* Consulta se ha entrada pendente. */
int  term_has_input(void);
int  term_is_tty(void);

#endif /* DELTA_TERM_H */
