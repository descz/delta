/* clip_probe.c - sonda E2E do clipboard_read: roda com o clipboard ja
 * preparado e imprime o que o Delta Agent veria. Uso:
 *   clip_probe.exe [dir-saida]                                  */
#include "clipboard.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : ".";
    Clip c;
    clip_init(&c);
    if (!clipboard_read(&c, dir)) {
        printf("RESULTADO: nada (texto/imagem/arquivo ausentes)\n");
        return 1;
    }
    const char *kinds[] = { "NONE", "TEXT", "IMAGE", "FILES" };
    printf("RESULTADO: kind=%s path=%s video=%d\n", kinds[c.kind],
           c.path ? c.path : "-", c.is_video);
    if (c.kind == CLIP_TEXT) printf("TEXTO: %.80s\n", c.text);
    clip_free(&c);
    return 0;
}
