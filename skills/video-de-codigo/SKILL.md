---
name: video-de-codigo
description: Gerar videos animados com codigo C (estilo motion graphics do X.com) — o agente escreve o gerador de frames, compila, roda e codifica o MP4 com o Delta. Use quando o usuario pedir um "video animado", "motion graphic", "animacao programatica" ou video explicativo.
---

# Video com código

Videos nascem de CÓDIGO aqui: um gerador em C desenha os frames, o Delta
codifica o MP4 (codec do sistema, sem dependência). Nada de gravar tela.

## O pipeline (sempre este)

1. **BRIEF VISUAL primeiro** (não pule!): descreva em 3-5 linhas — a ideia da
   animação, a paleta (2-3 cores exatas em hex), o ritmo (o que entra quando),
   as dimensões (1280x720 paisagem ou 1080x1920 vertical p/ celular) e a
   duração (10-30s costuma bastar). Sem brief claro o vídeo vira salada.
2. **Escreva `gera.c`**: um programa C com o mini-canvas (abaixo) que desenha
   cada frame em função do tempo `t` (0 a 1) e grava os BMPs.
3. **Compile e rode**: `gcc -O2 gera.c -o gera && ./gera` → `frames/0000.bmp ...`
4. **Codifique**: `delta --render-video frames/ video.mp4 30`
5. **Revise**: extraia de volta (`delta --video-frames video.mp4 frames-check/`)
   e confira 2-3 frames; ajuste o gera.c e repita se o movimento/ritmo não
   bateu com o brief.

## O mini-canvas (cole no topo do gera.c)

```c
/* canvas.h mini — RGB 24bpp, origem no canto sup. esquerdo */
static unsigned char *img; static int CW, CH;
static void canvas_init(int w, int h) {
    CW = w; CH = h;
    img = malloc((size_t)CW * CH * 3);
}
static void px(int x, int y, int r, int g, int b) {
    if (x < 0 || y < 0 || x >= CW || y >= CH) return;
    unsigned char *p = img + ((size_t)y * CW + x) * 3;
    p[0] = r; p[1] = g; p[2] = b;
}
static void fundo(int r, int g, int b) {
    for (int i = 0; i < CW * CH; i++) { img[i*3] = r; img[i*3+1] = g; img[i*3+2] = b; }
}
static void circulo(int cx, int cy, int r, int fr, int fg, int fb) {
    for (int y = cy - r; y <= cy + r; y++)
        for (int x = cx - r; x <= cx + r; x++)
            if ((x-cx)*(x-cx) + (y-cy)*(y-cy) <= r*r) px(x, y, fr, fg, fb);
}
static void retangulo(int x0, int y0, int w, int h, int r, int g, int b) {
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++) px(x, y, r, g, b);
}
static void linha(int x0, int y0, int x1, int y1, int r, int g, int b) {
    int dx = abs(x1-x0), dy = abs(y1-y0), n = dx > dy ? dx : dy;
    for (int i = 0; i <= n; i++) {
        int x = x0 + (x1-x0) * i / (n ? n : 1);
        int y = y0 + (y1-y0) * i / (n ? n : 1);
        px(x, y, r, g, b);
    }
}
static float ease(float t) { return t < .5f ? 2*t*t : 1 - (-2*t+2)*(-2*t+2)/2; } /* easeInOut */
static void frame_gravar(const char *dir, int n) {  /* grava BMP bottom-up */
    char path[300]; snprintf(path, sizeof path, "%s/%04d.bmp", dir, n);
    FILE *f = fopen(path, "wb");
    int lin = CW * 3;
    unsigned char *inv = malloc((size_t)lin * CH);
    for (int y = 0; y < CH; y++) memcpy(inv + (size_t)y * lin, img + (size_t)(CH-1-y) * lin, lin);
    unsigned char cab[54] = {'B','M'};
    int sz = 54 + lin * CH;
    memcpy(cab+2, &sz, 4); unsigned zero = 0; memcpy(cab+6, &zero, 4);
    memcpy(cab+10, &(unsigned){54}, 4); memcpy(cab+14, &(unsigned){40}, 4);
    memcpy(cab+18, &CW, 4); memcpy(cab+22, &CH, 4);
    unsigned short um = 1; memcpy(cab+26, &um, 2); memcpy(cab+28, &(unsigned short){24}, 2);
    memcpy(cab+34, &sz, 4);
    fwrite(cab, 1, 54, f); fwrite(inv, 1, (size_t)lin * CH, f);
    fclose(f); free(inv);
}
```

E o `main` do gerador:

```c
int main(void) {
    canvas_init(1280, 720);
    system("mkdir frames 2>nul");           /* linux: mkdir -p frames */
    int TOTAL = 300;                         /* 10s a 30fps */
    for (int f = 0; f < TOTAL; f++) {
        float t = ease((float)f / TOTAL);    /* o tempo da animação */
        /* desenhe a cena no instante t aqui */
        frame_gravar("frames", f);
    }
    return 0;
}
```

## Regras de qualidade (o que separa o vídeo bom do ruim)

- **Uma ideia por vídeo**: um conceito visual claro; 2-3 cores em hex exatas
  definidas no brief; fundo escuro quase sempre escala melhor;
- **Movimento com easing**: NUNCA linear — use `ease()`; entradas suaves
  (fade/escala), saída rápida. Nada "teleporta";
- **Ritmo**: mude a cena/estado a cada ~1.5-3s; o movimento contínuo mantém
  vivo (partículas, órbitas, gradientes animados);
- **Geometria > texto**: desenhar fonte em C é sofrimento — motion graphics
  geométrico (formas, grades, ondas, partículas) é o forte deste pipeline;
- **Dimensões fixas e pares** (320 múltiplo de 16 ideal): 1280x720, 1080x1920.

## Antipadrões

- Começar a codificar sem o brief (o vídeo vira enfeite sem narrativa);
- Frames com dimensões diferentes entre si (o codificador exige iguais);
- Linha de movimento linear e cores saturadas aleatórias ("vídeo de IA");
- Esquecer que 30fps x 30s = 900 frames — o gerador tem que ser rápido
  (sem alocação por frame no loop de desenho).
