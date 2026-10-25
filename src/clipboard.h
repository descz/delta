/* clipboard.h - leitura do clipboard (texto, imagem, arquivos/video).
 * Windows: API nativa. Sem dependencias externas. */
#ifndef DELTA_CLIPBOARD_H
#define DELTA_CLIPBOARD_H

#include "util.h"

typedef enum { CLIP_NONE = 0, CLIP_TEXT, CLIP_IMAGE, CLIP_FILES } ClipKind;

typedef struct {
    ClipKind kind;
    char    *text;      /* CLIP_TEXT: UTF-8 */
    char    *path;      /* CLIP_IMAGE/CLIP_FILES: caminho salvo/no disco */
    int      width, height;
    int      is_video;  /* CLIP_FILES que parece video */
} Clip;

void clip_init(Clip *c);
void clip_free(Clip *c);
/* Le o clipboard. Retorna 1 se trouxe algo. `dir` guarda imagens coladas. */
int  clipboard_read(Clip *c, const char *dir);
/* Grava texto UTF-8 no clipboard. Retorna 1 se ok. */
int  clipboard_write_text(const char *utf8);
/* 1 se o Ctrl+V esta disponivel nesta plataforma. */
int  clipboard_available(void);

/* Utilitarios expostos para teste */
int  png_encode(const unsigned char *rgb, int w, int h, int channels, Str *out);

#endif /* DELTA_CLIPBOARD_H */
