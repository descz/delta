---
name: video-de-codigo
description: Gerar videos animados de alta qualidade com codigo C em dois estilos — EXPLAINER tipografico (fundo claro, texto gigante, cards, icones, typewriter) e DARK MOTION (fundo escuro, glow, particulas). Brief antes, MP4 pelo Delta. Use quando o usuario pedir "video animado", "motion graphic", "animacao programatica" ou video explicativo.
---

# Vídeo com código

Vídeos nascem de CÓDIGO: um gerador em C desenha os frames, o Delta codifica
o MP4 (codec do sistema — Media Foundation; ffmpeg no POSIX). Nada de gravar tela.

## OS DOIS ESTILOS (estudados em vídeos de referência)

**Estilo A — EXPLAINER tipográfico** (fundo claro): fundo creme/branco,
headlines BOLD gigantes, kicker em small-caps espaçado no topo, highlight de
cor sólida crescendo atrás de uma palavra-chave, cards brancos arredondados
com sombra + ícones flat (desenhe com primitivas: play = círculo+triângulo,
envelope = retângulo+diagonais), labels coloridas dentro dos cards, dot grid
sutil no fundo. Típico: vídeos explicativos, storytelling, comparativos.

**Estilo B — DARK motion** (fundo escuro): fundo quase-preto, partículas/glow
aditivo, rastros (o buffer NUNCA limpa — decai ~0.88 por frame), rampa de cor
por profundidade, anel/órbita de composição, vinheta. Típico: intros, "o que
é X", vídeos de producto.

## O pipeline (sempre este)

1. **BRIEF VISUAL primeiro** (nunca pule): estilo (A ou B), a ideia da animação,
   paleta de 2-3 cores exatas em hex, o ritmo (o que entra quando) em uma
   tabela cena por cena, dimensões (1280x720 ou 1080x1920) e duração (6-20s).
2. **Escreva `gera.c`** com o mini-canvas (abaixo); cada frame desenhado em
   função de `t` (0 a 1), cenas com fade in/out (NUNCA corte seco).
3. **Compile e rode**: `gcc -O2 gera.c -o gera -lm && ./gera` → `frames/0000.bmp ...`
4. **Codifique**: `delta --render-video frames/ video.mp4 30`
5. **Revise de verdade**: `delta --video-frames video.mp4 check/` e OLHE 2-3
   frames; compare com o brief; ajuste e repita. Vídeo bom sai em 2-3 idas.

## As 6 técnicas de qualidade (estilo A e B)

1. **Supersampling 2x**: render em 2560x1440, rebaixa para 720p (anti-aliase);
2. **Acumulação HDR + tonemap** (estilo B): brilho soma em float; grava com
   `x*(2.51x+0.03)/(x*(2.43x+0.59)+0.14)` — glow quente sem branco chapado;
3. **Rastros** (estilo B): NUNCA limpe o buffer — decaia ~0.88 por frame;
4. **CONFIRMAÇÃO de cena** (estilo A): cada elemento ENTRA com ease (deslizando
   de baixo ou escala), cena sai com fade ANTES da próxima entrar (nunca
   sobreponha cenas — o maior defeito que se vê);
5. **Typewriter + highlight** (estilo A): texto aparece letra a letra ou com
   highlight de cor sólida crescendo ATRÁS da palavra-chave;
6. **Vida de fundo** (ambos): dot grid sutil (A), chevrons/setas repetidas com
   fade progressivo (A), elementos decorativos cortando os cantos (A/B),
   fundo borrado ampliado atrás do card-herói (profundidade, B/A).

## O mini-canvas (cole no topo do gera.c)

```c
static unsigned char *img; static int CW, CH;   /* RGB, origem topo-esq */
static void canvas_init(int w, int h) { CW = w; CH = h; img = malloc((size_t)CW*CH*3); }
static void px(int x, int y, int r, int g, int b) {
    if (x < 0 || y < 0 || x >= CW || y >= CH) return;
    unsigned char *p = img + ((size_t)y*CW + x)*3; p[0]=r; p[1]=g; p[2]=b;
}
static void fundo(int r, int g, int b) {
    for (int i = 0; i < CW*CH; i++) { img[i*3]=r; img[i*3+1]=g; img[i*3+2]=b; }
}
static void mistura(int x, int y, int r, int g, int b, float a) {
    if (x < 0 || y < 0 || x >= CW || y >= CH) return;
    unsigned char *p = img + ((size_t)y*CW + x)*3;
    p[0]=p[0]*(1-a)+r*a; p[1]=p[1]*(1-a)+g*a; p[2]=p[2]*(1-a)+b*a;
}
static void retangulo(int x0, int y0, int w, int h, int r, int g, int b) {
    for (int y = y0; y < y0+h; y++) for (int x = x0; x < x0+w; x++) px(x, y, r, g, b);
}
static void rect_arred(int x0, int y0, int w, int h, int rad, int r, int g, int b) {
    for (int y = y0; y < y0+h; y++) for (int x = x0; x < x0+w; x++) {
        int dx = x<x0+rad ? x0+rad-x : (x>=x0+w-rad ? x-(x0+w-1-rad) : 0);
        int dy = y<y0+rad ? y0+rad-y : (y>=y0+h-rad ? y-(y0+h-1-rad) : 0);
        if (dx*dx+dy*dy <= rad*rad) px(x, y, r, g, b);
    }
}
static void sombra(int x0, int y0, int w, int h, int rad, float forca) {
    for (int y = y0-rad; y < y0+h+rad; y++) for (int x = x0-rad; x < x0+w+rad; x++) {
        int dx = x<x0+rad ? x0+rad-x : (x>=x0+w-rad ? x-(x0+w-1-rad) : 0);
        int dy = y<y0+rad ? y0+rad-y : (y>=y0+h-rad ? y-(y0+h-1-rad) : 0);
        float a = (1.0f - sqrt((float)(dx*dx+dy*dy)) / rad) * forca;
        if (a > 0) mistura(x, y, 20, 18, 15, a);
    }
}
static void circulo_preenche(int cx, int cy, int r, int cr, int cg, int cb) {
    for (int y = cy-r; y <= cy+r; y++) for (int x = cx-r; x <= cx+r; x++)
        if ((x-cx)*(x-cx)+(y-cy)*(y-cy) <= r*r) px(x, y, cr, cg, cb);
}
static void linha(int x0, int y0, int x1, int y1, int r, int g, int b) {
    int dx = abs(x1-x0), dy = abs(y1-y0), n = dx > dy ? dx : dy;
    for (int i = 0; i <= n; i++)
        px(x0+(x1-x0)*i/(n?n:1), y0+(y1-y0)*i/(n?n:1), r, g, b);
}
static void dot_grid(int passo, int r, int g, int b, float a) {
    for (int y = passo; y < CH; y += passo)
        for (int x = passo; x < CW; x += passo) mistura(x, y, r, g, b, a);
}
static float ease(float t) { return t < .5f ? 2*t*t : 1 - (-2*t+2)*(-2*t+2)/2; }
static float clamp01(float t) { return t < 0 ? 0 : (t > 1 ? 1 : t); }
```

### Texto (Windows — GDI, fonte do sistema, com anti-aliase de verdade)

```c
/* DIB/DC criados UMA_VEZ (static) — criar a cada chamada esgota o GDI e o
   texto some do nada no meio do vídeo. A máscara = média dos 3 canais
   (ANTIALIASED_QUALITY; ClearType é subpixel e quebra a máscara). */
static HDC mem; static void *bits; static HBITMAP bmp; static HDC wdc;
static int cw_usado;
static void texto(const char *s, int x, int y, int alt, int peso,
                  int r, int g, int b, float alpha) {
    if (cw_usado != CW) {
        wdc = GetDC(NULL); mem = CreateCompatibleDC(wdc);
        BITMAPINFO bi; memset(&bi, 0, sizeof bi);
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = CW; bi.bmiHeader.biHeight = -CH;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
        bmp = CreateDIBSection(wdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        SelectObject(mem, bmp); cw_usado = CW;
    }
    if (!mem || !bits) return;
    for (int i = 0; i < CW*CH; i++)
        ((unsigned char*)bits)[i*4] = 0, ((unsigned char*)bits)[i*4+1] = 0,
        ((unsigned char*)bits)[i*4+2] = 0, ((unsigned char*)bits)[i*4+3] = 255;
    HFONT f = CreateFontW(alt, 0,0,0, peso, FALSE,FALSE,FALSE, DEFAULT_CHARSET,
                          OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                          DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HGDIOBJ vo = SelectObject(mem, f);
    SetBkMode(mem, TRANSPARENT); SetTextColor(mem, RGB(255,255,255));
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    WCHAR *w = malloc((size_t)n*2);
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    TextOutW(mem, x, y, w, n-1);
    free(w);
    for (int yy = 0; yy < CH; yy++) for (int xx = 0; xx < CW; xx++) {
        unsigned char *m = (unsigned char*)bits + ((size_t)yy*CW+xx)*4;
        float k = (m[0]+m[1]+m[2]) / (3*255.0f);
        if (k > 0.01f) mistura(xx, yy, r, g, b, k * alpha);
    }
    SelectObject(mem, vo); DeleteObject(f);
}
```
Fontes boas instaladas no Windows: **Segoe UI** (Black=900 p/ headlines, Bold=700,
Regular), Consolas p/ código. Números do peso: 400 normal, 700 bold, 900 black.

### Estilo B: HDR + rastros + tonemap (adicione ao canvas)

```c
static float *hdr; static int RW, RH;   /* render 2x (supersample) */
static void canvas_init_b(int w, int h) {
    CW = w; CH = h; RW = w*2; RH = h*2;
    hdr = calloc((size_t)RW*RH*3, sizeof(float));
    img = malloc((size_t)CW*CH*3);
}
static void px_add(float x, float y, float r, float g, float b, float f) {
    int xi = (int)x, yi = (int)y;   /* jah em espaco de render (x2 dentro) */
    if (xi < 0 || yi < 0 || xi >= RW || yi >= RH) return;
    float *p = hdr + ((size_t)yi*RW + xi)*3;
    p[0] += r*f; p[1] += g*f; p[2] += b*f;
}
static void glow(float x, float y, float r, float cr, float cg, float cb) {
    x *= 2; y *= 2; r *= 2;
    float r2 = r*r;
    int x0 = (int)(x-r*4), x1 = (int)(x+r*4), y0 = (int)(y-r*4), y1 = (int)(y+r*4);
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 > RW) x1 = RW; if (y1 > RH) y1 = RH;
    for (int yy = y0; yy < y1; yy++) for (int xx = x0; xx < x1; xx++) {
        float d2 = (xx-x)*(xx-x) + (yy-y)*(yy-y);
        float f = 1.0f - d2/(r2*16);
        if (f <= 0) continue;
        float nuc = 1.0f - d2/r2;
        px_add(xx, yy, cr, cg, cb, (f*f)*(nuc > 0 ? 1.0f + nuc*2.0f : 0.0f));
    }
}
static void rastro(float decai) { for (size_t k = 0; k < (size_t)RW*RH*3; k++) hdr[k] *= decai; }
static float tonemap(float x) { return x*(2.51f*x+0.03f)/(x*(2.43f*x+0.59f)+0.14f); }
static void revela(float expo, const char *dir, int n) {
    /* media 2x2 do render -> canvas com tonemap + vinheta -> BMP */
    for (int y = 0; y < CH; y++) for (int x = 0; x < CW; x++) {
        float r = 0, g = 0, b = 0;
        for (int sy = 0; sy < 2; sy++) for (int sx = 0; sx < 2; sx++) {
            float *p = hdr + ((size_t)(y*2+sy)*RW + (x*2+sx))*3;
            r += tonemap(p[0]*expo); g += tonemap(p[1]*expo); b += tonemap(p[2]*expo);
        }
        unsigned char *o = img + ((size_t)y*CW+x)*3;
        o[0] = (unsigned char)(b/4*255); o[1] = (unsigned char)(g/4*255);
        o[2] = (unsigned char)(r/4*255);
        float dx = (x-CW/2.0f)/(CW/2.0f), dy = (y-CH/2.0f)/(CH/2.0f);
        float vig = 1.0f - 0.35f*(dx*dx+dy*dy);
        o[0] *= vig; o[1] *= vig; o[2] *= vig;
    }
    /* grava o BMP igual ao frame_gravar acima (bottom-up, BGR) */
    extern void frame_gravar(const char *dir, int n);
    frame_gravar(dir, n);
}
```

### Gravação do frame (BMP bottom-up, BGR)

```c
static void frame_gravar(const char *dir, int n) {
    char path[300]; snprintf(path, sizeof path, "%s/%04d.bmp", dir, n);
    FILE *f = fopen(path, "wb");
    int lin = CW * 3;
    unsigned char *inv = malloc((size_t)lin * CH);
    for (int y = 0; y < CH; y++)
        for (int x = 0; x < CW; x++) {           /* RGB -> BGR + flip */
            inv[(size_t)y*lin + x*3]     = img[(size_t)(CH-1-y)*CW*3 + x*3+2];
            inv[(size_t)y*lin + x*3 + 1] = img[(size_t)(CH-1-y)*CW*3 + x*3+1];
            inv[(size_t)y*lin + x*3 + 2] = img[(size_t)(CH-1-y)*CW*3 + x*3];
        }
    unsigned char cab[54] = {'B','M'};
    int sz = 54 + lin*CH; unsigned zero = 0;
    memcpy(cab+2, &sz, 4); memcpy(cab+10, &(unsigned){54}, 4);
    memcpy(cab+14, &(unsigned){40}, 4); memcpy(cab+18, &CW, 4); memcpy(cab+22, &CH, 4);
    unsigned short um = 1; memcpy(cab+26, &um, 2);
    memcpy(cab+28, &(unsigned short){24}, 2); memcpy(cab+34, &sz, 4);
    fwrite(cab, 1, 54, f); fwrite(inv, 1, (size_t)lin*CH, f);
    fclose(f); free(inv);
}
```

## Receitas dos padrões estudados

- **Typewriter**: `texto(copia_str_ate_idx, ...)` crescentando a substring
  por frame; o cursor pisca com `(frame / 15) % 2`;
- **Highlight atrás da palavra**: desenhe o `ret` colorido crescendo pela
  esquerda (`larg = largura_palavra * ease(clamp01((t-t0)*6 / larg))`) e
  RE-depore o texto por cima com alpha 1 naquela palavra;
- **Kicker small-caps**: draw letra a letra com avanço extra (letter-spacing
  manual), fonte pequena peso 600, cor esmaecida;
- **Ícones flat**: NÃO desenhe nada 3D — primitivas: play = `circulo_preenche`
  preto + triângulo branco (linhas); envelope = `ret` colorido + 2 diagonais
  para o aba da aba; celular = rect_arred escuro + rects claros dentro;
- **Chevrons vivos**: 3-4 setas repetidas (linhas em V) com alpha em
  progressão (0.9, 0.6, 0.3) e o conjunto oscilando com sin(t*ritmo);
- **Fundo borrado (profundidade)**: desenhe o conteúdo grande no fundo,
  depois o card-herói no meio — falso-blur = desenhe no fundo o MESMO título
  bem expandido e esmaecido (sem filtro de blur real);
- **Card + sombra**: `sombra()` ANTES do `rect_arred` branco (double pass).

## Regras de qualidade

- **Clamp01 SEMPRE no argumento do ease** (`ease(clamp01(t*3))`) — o ease é
  parábola: com t > 1 ele VOLTA e a animação entra e sai sozinha (o bug nº 1);
- **Cenas não se sobreponham**: a cena sai (fade) antes da próxima entrar;
- **Uma ideia por vídeo**; paleta de 2-3 cores exatas no brief;
- **30fps x 6-20s** — o gerador tem que rodar em segundos;
- **Textos: máximo 2 níveis** (kicker pequeno + headline grande);
- **Revise com olhos**: extraia frames de volta e OLHE antes de entregar.

## Antipadrões

- Começar a codificar sem o brief cena-por-cena;
- Elementos que teletransportam (sem ease) ou easing sem clamp01;
- Cenas sobrepostas; emoji ints como ícones; texto ilegível;
- Criar DC/DIB/fontes dentro de função por frame (esgota o GDI e o texto
  some no meio do vídeo);
- Entregar sem revisar os frames de volta.
