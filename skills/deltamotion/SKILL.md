---
name: deltamotion
description: Use when the user asks for an animated video, motion graphic, explainer, cinematic title or programmatic animation delivered as MP4 and rendered by C code, without a browser, HTML, Three.js or screen recording.
---

# Deltamotion: vídeo por código (C → BMP → MP4)

O vídeo nasce de um programa C que desenha cada quadro em BMP. O `delta` junta
a sequência em H.264. Não há navegador, HTML, WebGL nem gravação de tela neste
fluxo. Cada quadro depende só de `t` (segundos), então o resultado é repetível.

## Escopo

- **Serve para:** explicativos, intros, títulos, motion gráfico 2D com profundidade
  simulada (camadas e paralaxe), transições, letterbox, texto em Windows.
- **Não serve para:** 3D real (não existe motor 3D aqui), páginas HTML/CSS ou Three.js
  (use a skill `3jsskill`, que trata de páginas web), gravação de tela.

## Fluxo obrigatório

1. **Brief.** Duração (6 a 20 s), formato (16:9 em 1280×720 ou 1920×1080; vertical
   1080×1920), uma frase com a mensagem, 2 a 3 cores em hex, ritmo de referência e se
   haverá áudio.
2. **Storyboard.** Uma linha por plano: `t_in`, `t_out`, enquadramento (aberto, médio,
   detalhe), movimento de câmera, elemento principal, entrada, saída e motivo. Não
   escreva código antes disso.
3. **Código.** Parta do modelo abaixo e troque cena por cena, seguindo o storyboard.
4. **Compilação.** `gcc -std=c11 -O2 -Wall -Wextra -Wpedantic gera.c -o gera -lm`
   (com texto no Windows, acrescente `-lgdi32`). Zero avisos.
5. **Quadros.** `mkdir frames` e rode `./gera`. Confira que a contagem bate com
   duração × fps (ex.: 8 s × 30 = 240 arquivos).
6. **Revisão.** Antes de codificar, abra pelo menos 6 quadros: 1/6, 1/3, 1/2, 2/3,
   5/6 e um de cada transição. Se um quadro mostra duas cenas, a janela de tempo
   está errada. Depois de codificar, extraia quadros com `delta --video-frames video.mp4 check`.
7. **Codificação.** `delta --render-video frames video.mp4 30`. O encoder lê só `.bmp`,
   em ordem de nome.
8. **Conferência.** `ffprobe -v error -show_entries stream=codec_name,width,height,r_frame_rate -show_entries format=duration -of default=nw=1 video.mp4`.
   Esperado: `h264`, as dimensões do brief, `30/1` e a duração.
9. **Áudio (opcional).** Só se `ffmpeg` estiver no PATH. Veja a seção final.
10. **Limpeza.** Apague a pasta `frames/`. Ela é grande.

## Compilador e ambiente (Windows)

O Delta executa comandos pelo `cmd.exe`. Nesse ambiente o `gcc` costuma não estar no PATH.

1. Verifique com `where gcc`. Se não achar, procure um toolchain MinGW-w64 (ex.: `C:\MinGW\bin\gcc.exe` ou `mingw64\bin\gcc.exe` do WinLibs).
2. Se o caminho do compilador tiver espaço (ex.: a pasta do usuário "GM METELURGICA"), o link falha com `file not recognized`. Solução testada: copiar o toolchain para um caminho sem espaços (ex.: `C:\dmtc\mingw64`) e zerar `COMPILER_PATH` e `LIBRARY_PATH` antes de compilar.
3. Compile por um `.bat` com finais de linha CRLF, em vez de encadear comandos longos:

```bat
@echo off
cd /d "%~dp0"
set COMPILER_PATH=
set LIBRARY_PATH=
C:\dmtc\mingw64\bin\gcc.exe -std=c11 -O2 -Wall -Wextra -Wpedantic gera.c -o gera.exe -lm -lgdi32
if errorlevel 1 (echo FALHOU) else (echo COMPILADO)
```

No Linux/macOS: `gcc -std=c11 -O2 -Wall -Wextra gera.c -o gera -lm`, sem `-lgdi32`.
Se `delta` não estiver no PATH, use o caminho completo do executável.

## Regras técnicas (não negocie)

- **Dimensões:** largura múltipla de 4 e altura par. Pelo código de `video_render`, o
  padding das linhas BMP não é tratado; por isso a largura deve ser múltipla de 4. Os
  formatos deste guia (1280×720, 1920×1080, 1080×1920) atendem. Larguras fora disso
  não foram testadas.
- **Nomes:** `%04d.bmp` começando em `0000`, sem buracos. A ordem vem do nome.
- **BMP:** 24 bpp, de baixo para cima, em BGR. O modelo já faz isso.
- **Supersampling 2×:** desenhe em 2× e reduza com média. Formas finas e texto precisam disso.
- **Motion blur:** `SUB = 4` subquadros com obturador de `0.5`. Mais subquadros dão blur
  mais suave e custam mais tempo.
- **Precisão:** acumule em `float`. Converta para 8 bits só no fim.
- **Tempo:** toda animação usa `t` em segundos, nunca o índice do quadro direto.
- **Disco:** um quadro 1280×720 tem cerca de 2,7 MB. Oito segundos a 30 fps geram cerca
  de 650 MB de BMP temporário.

## Linguagem de direção

Cada escolha abaixo tem uma razão. Use a razão para decidir quando aplicar.

1. **Escala de plano.** Alterne aberto, médio e detalhe a cada 2 a 4 s. O aberto dá
   contexto; o detalhe dá ênfase.
2. **Curvas de entrada e saída.** Entrada com ease-out (desacelera ao chegar); saída
   com ease-in suave. Movimento linear parece mecânico.
3. **Câmera 2.5D.** Cada camada tem profundidade `prof` (1 = mais perto). O deslocamento
   da câmera é `cam * prof`, e a diferença entre camadas cria paralaxe. Para dolly, some
   uma escala de 1.00 a 1.04 no conjunto. Como referência prática, mantenha o movimento
   abaixo de cerca de 150 px/s em 1280×720.
4. **Janela exclusiva por cena.** Uma cena por vez. O fade de saída termina antes da
   entrada da próxima, salvo uma transição planejada de 0,3 a 0,6 s.
5. **Tempo de leitura.** Texto parado por pelo menos 2 s a cada 5 a 7 palavras. Uma
   ideia por cena.
6. **Transições com motivo.**
   - Corte seco: impacto ou mudança de assunto.
   - Fade: passagem de tempo.
   - Match cut: forma que muda de lugar e tamanho entre duas cenas (mesma posição e
     escala no fim da A e no início da B).
   - Anel ou onda: energia ou revelação.
7. **Cor.** Paleta de 2 a 3 cores e um neutro. Acento em menos de 10% da área. Use a grade
   de cor abaixo, sem exagero.
8. **Enquadramento.** Letterbox 2,39:1 em 1280×720 deixa barras de cerca de 92 px em
   cima e embaixo. Margem segura de 6%: cerca de 77 px na horizontal e 43 px na vertical.
9. **Textura.** Grão de 0,02 e vinheta entre 0,2 e 0,35. Acima disso vira ruído.
10. **Quadro parado?** Ajuste a curva de ease ou o tempo de pausa, não o tamanho do elemento.

### Presets de partida

| Preset | Fundo | Ritmo | Textura |
|---|---|---|---|
| Explicativo | claro (creme) | 2 a 3 s por cena, tipo grande | sem grão, vinheta leve |
| Dark motion | quase preto | glow e rastros, 3 a 5 s por cena | vinheta 0,3 e grão 0,02 |
| Título cinematográfico | escuro com letterbox | dolly lento, 5 a 8 s | grade de cor e grão |

## Modelo base (compilado e renderizado)

Este arquivo foi compilado com MinGW-w64 (sem avisos com `-Wall -Wextra -Wpedantic`),
gerou 240 quadros em cerca de 43 s, e o encoder produziu H.264 1280×720 a 30 fps com
8,000 s. Troque apenas as cenas dentro de `cena()`.

```c
/* gera.c - cena de exemplo para a skill deltamotion: C puro, sem navegador.
 * Compilar: gcc -std=c11 -O2 -Wall -Wextra gera.c -o gera -lm
 * Rodar:    mkdir frames && ./gera
 * MP4:      delta --render-video frames video.mp4 30
 *
 * Tecnicas: supersampling 2x, motion blur por obturador (SUB subquadros),
 * camera 2.5D com paralaxe, cenas com janela exclusiva e fade, vinheta e
 * grao deterministico. Saida: frames/0000.bmp ... (24 bpp, topo-esq -> BMP).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define W 1280            /* multiplo de 4: linha BMP sem padding */
#define H 720             /* par: o encoder H.264 exige dimensao par */
#define SS 2              /* supersampling: desenha em 2x e reduz (anti-aliasing) */
#define FPS 30
#define DUR 8.0f          /* duracao em segundos */
#define NFRAMES ((int)(DUR * FPS))
#define SUB 4             /* subquadros por quadro = motion blur */
#define SHUTTER 0.5f      /* obturador aberto em metade do intervalo do quadro */

typedef struct { float r, g, b; } Cor;   /* 0..1, no espaco de exibicao */

static Cor *buf;                          /* W*SS x H*SS: a cena do subquadro */
static int BW, BH;

static float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
static float smooth01(float t) { t = clampf(t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }
static float ease_out3(float t) { t = clampf(t, 0.0f, 1.0f); float u = 1.0f - t; return 1.0f - u * u * u; }

/* janela EXCLUSIVA da cena: 0 fora de [a,b]; sobe em fi s, desce em fo s */
static float janela(float t, float a, float b, float fi, float fo) {
    if (t < a || t > b) return 0.0f;
    float up = fi > 0.0f ? smooth01((t - a) / fi) : 1.0f;
    float dn = fo > 0.0f ? smooth01((b - t) / fo) : 1.0f;
    return up < dn ? up : dn;
}

/* mistura um pixel do buffer de supersampling (coordenadas em subpixels) */
static void mistura(int x, int y, Cor c, float a) {
    if (a <= 0.0f || x < 0 || y < 0 || x >= BW || y >= BH) return;
    Cor *p = &buf[(size_t)y * BW + (size_t)x];
    p->r += (c.r - p->r) * a;
    p->g += (c.g - p->g) * a;
    p->b += (c.b - p->b) * a;
}

/* coordenadas aqui sao do quadro final (1 unidade = 1 pixel de saida) */
static void retangulo(float x0, float y0, float x1, float y1, Cor c, float a) {
    int sx0 = (int)floorf(x0 * SS), sy0 = (int)floorf(y0 * SS);
    int sx1 = (int)ceilf(x1 * SS),  sy1 = (int)ceilf(y1 * SS);
    for (int y = sy0; y < sy1; y++)
        for (int x = sx0; x < sx1; x++) mistura(x, y, c, a);
}

/* brilho radial suave (glow): alfa cai com o quadrado da distancia */
static void brilho(float cx, float cy, float raio, Cor c, float a) {
    float R = raio * SS, X = cx * SS, Y = cy * SS;
    int x0 = (int)floorf(X - R), x1 = (int)ceilf(X + R);
    int y0 = (int)floorf(Y - R), y1 = (int)ceilf(Y + R);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            float dx = (float)x + 0.5f - X, dy = (float)y + 0.5f - Y;
            float q = 1.0f - (dx * dx + dy * dy) / (R * R);
            if (q > 0.0f) mistura(x, y, c, a * q * q);
        }
}

/* anel de espessura fixa, usado para ondas de impacto */
static void anel(float cx, float cy, float r, float espessura, Cor c, float a) {
    float R = r * SS, E = espessura * SS, X = cx * SS, Y = cy * SS;
    float ro = R + E * 0.5f, ri = R - E * 0.5f;
    if (ri < 0.0f) ri = 0.0f;
    int lim = (int)ceilf(ro) + 1;
    for (int y = (int)Y - lim; y <= (int)Y + lim; y++)
        for (int x = (int)X - lim; x <= (int)X + lim; x++) {
            float dx = (float)x + 0.5f - X, dy = (float)y + 0.5f - Y;
            float d = sqrtf(dx * dx + dy * dy);
            if (d >= ri && d <= ro) mistura(x, y, c, a);
        }
}

/* cena no tempo t (segundos) - aqui fica a direcao: entradas, paralaxe, cortes */
static void cena(float t) {
    /* fundo: gradiente vertical escuro (paleta: azul-noite + ambar) */
    for (int y = 0; y < BH; y++) {
        float k = (float)y / (float)BH;
        Cor c = { 0.035f + 0.05f * k, 0.040f + 0.03f * k, 0.085f + 0.05f * k };
        for (int x = 0; x < BW; x++) buf[(size_t)y * BW + (size_t)x] = c;
    }

    /* camera 2.5D: dolly lateral com ease in-out; cada camada anda conforme
       a sua profundidade (prof 1 = perto, anda mais; prof 0.2 = longe) */
    float cam = smooth01(t / DUR) * 120.0f;

    /* CENA 1 (0.0-3.0 s): orbes em 3 profundidades, entram e saem com fade */
    float a1 = janela(t, 0.0f, 3.0f, 0.5f, 0.5f);
    if (a1 > 0.0f) {
        const float prof[3] = { 0.25f, 0.6f, 1.0f };
        Cor cor[3] = { { 0.20f, 0.35f, 0.75f }, { 0.35f, 0.55f, 0.95f }, { 1.0f, 0.62f, 0.25f } };
        for (int k = 0; k < 3; k++) {
            float x = 640.0f + 220.0f * sinf(t * 0.9f + (float)k * 1.3f) - cam * prof[k];
            float y = 360.0f + 60.0f * (float)(k - 1);
            brilho(x, y, 70.0f + 40.0f * prof[k], cor[k], 0.9f * a1);
        }
    }

    /* CENA 2 (2.8-5.6 s): cartoes deslizam de baixo com ease-out escalonado */
    float a2 = janela(t, 2.8f, 5.6f, 0.4f, 0.4f);
    if (a2 > 0.0f) {
        for (int k = 0; k < 3; k++) {
            float p = ease_out3((t - (2.8f + 0.18f * (float)k)) / 0.7f);
            float x0 = 180.0f + 340.0f * (float)k;
            float y0 = 520.0f - 90.0f * p + 60.0f * (1.0f - p);
            Cor sombra = { 0.0f, 0.0f, 0.0f };
            retangulo(x0 + 10.0f, y0 + 16.0f, x0 + 290.0f + 10.0f, y0 + 200.0f + 16.0f, sombra, 0.35f * a2 * p);
            Cor cartao = { 0.96f, 0.93f, 0.86f };
            retangulo(x0, y0, x0 + 290.0f, y0 + 200.0f, cartao, a2 * p);
            Cor acento = { 0.90f, 0.42f, 0.18f };
            retangulo(x0 + 24.0f, y0 + 26.0f, x0 + 24.0f + 60.0f, y0 + 34.0f, acento, a2 * p);
        }
    }

    /* CENA 3 (5.4-8.0 s): onda de impacto que se expande e some */
    float a3 = janela(t, 5.4f, 8.0f, 0.3f, 0.6f);
    if (a3 > 0.0f) {
        float p = ease_out3((t - 5.4f) / 1.8f);
        Cor ambar = { 1.0f, 0.62f, 0.25f };
        anel(640.0f, 360.0f, 40.0f + 380.0f * p, 6.0f + 10.0f * (1.0f - p), ambar, a3 * (1.0f - p));
        brilho(640.0f, 360.0f, 120.0f, ambar, 0.5f * a3 * (1.0f - p));
    }
}

/* reducao 2x2 -> saida, acumulando com peso (media dos subquadros) */
static void reduz_acumula(float *acc, float peso) {
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float r = 0, g = 0, b = 0;
            for (int sy = 0; sy < SS; sy++)
                for (int sx = 0; sx < SS; sx++) {
                    const Cor *p = &buf[(size_t)(y * SS + sy) * BW + (size_t)(x * SS + sx)];
                    r += p->r; g += p->g; b += p->b;
                }
            float n = (float)(SS * SS);
            float *o = acc + ((size_t)y * W + (size_t)x) * 3;
            o[0] += r / n * peso;
            o[1] += g / n * peso;
            o[2] += b / n * peso;
        }
}

/* hash deterministico: o grao e' igual em toda execucao */
static float ruido(int x, int y, int f) {
    unsigned int h = (unsigned int)x * 374761393u + (unsigned int)y * 668265263u + (unsigned int)f * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xFFFFu) / 65535.0f - 0.5f;
}

/* vinheta + grao + 8 bits. O grao e' curto (0.02) para nao virar chuvisco */
static void finaliza(const float *acc, unsigned char *rgb, int f) {
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float dx = ((float)x - W * 0.5f) / (W * 0.5f);
            float dy = ((float)y - H * 0.5f) / (H * 0.5f);
            float vig = 1.0f - 0.30f * (dx * dx + dy * dy);
            float g = ruido(x, y, f) * 0.02f;
            const float *s = acc + ((size_t)y * W + (size_t)x) * 3;
            unsigned char *o = rgb + ((size_t)y * W + (size_t)x) * 3;
            for (int c = 0; c < 3; c++)
                o[c] = (unsigned char)(clampf((s[c] * vig + g), 0.0f, 1.0f) * 255.0f + 0.5f);
        }
}

static void le16(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static void le32(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

/* BMP 24 bpp, bottom-up, BGR; largura*3 e' multiplo de 4 (sem padding) */
static int bmp_salvar(const char *path, const unsigned char *rgb) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    unsigned int linha = (unsigned int)((W * 3 + 3) & ~3);
    unsigned int dados = linha * (unsigned int)H;
    unsigned char cab[54];
    memset(cab, 0, sizeof cab);
    cab[0] = 'B'; cab[1] = 'M';
    le32(cab + 2, 54u + dados);
    le32(cab + 10, 54u);
    le32(cab + 14, 40u);
    le32(cab + 18, (unsigned)W);
    le32(cab + 22, (unsigned)H);
    le16(cab + 26, 1u);
    le16(cab + 28, 24u);
    le32(cab + 34, dados);
    int ok = fwrite(cab, 1, sizeof cab, f) == sizeof cab;
    unsigned char *lin = calloc(linha, 1);
    if (!lin) { fclose(f); return 0; }
    for (int y = H - 1; y >= 0 && ok; y--) {
        const unsigned char *src = rgb + (size_t)y * W * 3;
        for (int x = 0; x < W; x++) {
            lin[x * 3 + 0] = src[x * 3 + 2];
            lin[x * 3 + 1] = src[x * 3 + 1];
            lin[x * 3 + 2] = src[x * 3 + 0];
        }
        ok = fwrite(lin, 1, linha, f) == linha;
    }
    free(lin);
    return fclose(f) == 0 && ok;
}

int main(void) {
    BW = W * SS; BH = H * SS;
    buf = malloc(sizeof(Cor) * (size_t)BW * (size_t)BH);
    float *acc = malloc(sizeof(float) * (size_t)W * H * 3);
    unsigned char *rgb = malloc((size_t)W * H * 3);
    if (!buf || !acc || !rgb) { fprintf(stderr, "sem memoria\n"); return 1; }

    char path[256];
    for (int f = 0; f < NFRAMES; f++) {
        memset(acc, 0, sizeof(float) * (size_t)W * H * 3);
        for (int s = 0; s < SUB; s++) {
            /* instante do subquadro dentro do obturador aberto */
            float t = ((float)f + SHUTTER * ((float)s + 0.5f) / (float)SUB) / (float)FPS;
            cena(t);
            reduz_acumula(acc, 1.0f / (float)SUB);
        }
        finaliza(acc, rgb, f);
        snprintf(path, sizeof path, "frames/%04d.bmp", f);
        if (!bmp_salvar(path, rgb)) { fprintf(stderr, "falha em %s\n", path); return 1; }
    }
    printf("%d quadros em frames/\n", NFRAMES);
    free(buf); free(acc); free(rgb);
    return 0;
}
```

## Acréscimos de edição (testados)

Os blocos abaixo foram compilados e renderizados. Para usar:

- **Grade e letterbox:** cole as duas funções antes de `finaliza`. Dentro de `finaliza`,
  troque a linha de saída por `o[c] = (unsigned char)(grade(s[c] * vig + g, 0.02f, 1.08f, 0.98f) * 255.0f + 0.5f);`
  e chame `letterbox(rgb, 2.39f);` no fim da função.
- **Texto (Windows):** cole o bloco `#ifdef _WIN32`. Inclua `<windows.h>` no topo.
  Em `main`, chame `texto_init()` logo após `BW = W * SS; BH = H * SS;` e `texto_fim()`
  antes de liberar memória. Compile com `-lgdi32`. Dentro de `cena()`, chame
  `texto("DELTA MOTION", 380.0f, 120.0f, 56, 800, ct, 0.9f);` com `Cor ct = { 1.0f, 1.0f, 1.0f };`
  declarada na própria `cena()`. Não use o nome `cor`: o modelo base já tem um
  array local `Cor cor[3]` e a chamada não compila. **Chame o texto no fim de `cena()`** —
  o fundo do modelo pinta o buffer inteiro e apaga texto desenhado antes dele.

```c
/* grade de cor: lift (sombras), gamma (meios), gain (altas), por canal */
static float grade(float v, float lift, float gamma, float gain) {
    v = clampf(v, 0.0f, 1.0f);
    return clampf(powf(v, 1.0f / gamma) * gain + lift, 0.0f, 1.0f);
}

/* letterbox cinematografico: barras pretas para a proporcao (ex.: 2.39) */
static void letterbox(unsigned char *rgb, float prop) {
    int alt = (int)((float)W / prop + 0.5f);
    int barra = (H - alt) / 2;
    if (barra <= 0) return;
    memset(rgb, 0, (size_t)barra * W * 3);
    memset(rgb + (size_t)(H - barra) * W * 3, 0, (size_t)barra * W * 3);
}
```

```c
#ifdef _WIN32
static HDC g_dc = NULL;
static HBITMAP g_dib = NULL;
static unsigned char *g_px = NULL;   /* BW x BH x 4, topo-esq */

/* cria DC e DIB UMA vez: criar a cada chamada esgota o GDI e o texto some */
static int texto_init(void) {
    HDC tela = GetDC(NULL);
    g_dc = CreateCompatibleDC(tela);
    ReleaseDC(NULL, tela);
    if (!g_dc) return 0;
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = BW;
    bi.bmiHeader.biHeight = -BH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void *bits = NULL;
    g_dib = CreateDIBSection(g_dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!g_dib) return 0;
    g_px = (unsigned char *)bits;
    SelectObject(g_dc, g_dib);
    SetBkMode(g_dc, TRANSPARENT);
    SetTextColor(g_dc, RGB(255, 255, 255));
    return 1;
}

static void texto_fim(void) {
    if (g_dib) DeleteObject(g_dib);
    if (g_dc) DeleteDC(g_dc);
}

/* texto UTF-8 com canto superior esquerdo em (x,y) do quadro final; altura em px */
static void texto(const char *s, float x, float y, int altura, int peso, Cor c, float alpha) {
    if (!g_px || alpha <= 0.0f) return;
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 1) return;
    wchar_t *w = malloc(sizeof(wchar_t) * (size_t)n);
    if (!w) return;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    HFONT fonte = CreateFontW(-altura * SS, 0, 0, 0, peso, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HGDIOBJ velha = SelectObject(g_dc, fonte);
    SIZE sz;
    memset(&sz, 0, sizeof sz);
    GetTextExtentPoint32W(g_dc, w, n - 1, &sz);
    int x0 = (int)floorf(x * SS), y0 = (int)floorf(y * SS);
    int lw = sz.cx + 4, lh = sz.cy + 4;
    if (lw > BW) lw = BW;
    if (lh > BH) lh = BH;
    if (x0 < 0 || y0 < 0 || x0 >= BW || y0 >= BH || lw <= 0 || lh <= 0) {
        SelectObject(g_dc, velha); DeleteObject(fonte); free(w); return;
    }
    /* limpa so a area usada: o DIB inteiro seria caro a cada texto */
    for (int yy = 0; yy < lh; yy++) memset(g_px + (size_t)yy * BW * 4, 0, (size_t)lw * 4);
    TextOutW(g_dc, 0, 0, w, n - 1);
    for (int yy = 0; yy < lh; yy++)
        for (int xx = 0; xx < lw; xx++) {
            const unsigned char *p = g_px + ((size_t)yy * BW + (size_t)xx) * 4;
            float k = ((float)p[0] + (float)p[1] + (float)p[2]) / (3.0f * 255.0f);
            if (k > 0.01f) mistura(x0 + xx, y0 + yy, c, k * alpha);
        }
    SelectObject(g_dc, velha);
    DeleteObject(fonte);
    free(w);
}
#else
/* fora do Windows nao ha GDI: o texto e' ignorado (ver a secao de texto) */
static int texto_init(void) { return 0; }
static void texto_fim(void) {}
static void texto(const char *s, float x, float y, int altura, int peso, Cor c, float alpha) {
    (void)s; (void)x; (void)y; (void)altura; (void)peso; (void)c; (void)alpha;
}
#endif
```

## Áudio (opcional)

`--render-video` gera só vídeo. Se o brief pedir som e o `ffmpeg` existir no PATH,
faça a mixagem depois. Teste com `ffmpeg -version` antes de prometer isso.

```bash
ffmpeg -y -i video.mp4 -i trilha.wav -map 0:v:0 -map 1:a:0 -c:v copy -c:a aac -b:a 160k -shortest final.mp4
```

Testado: com trilha de teste, o resultado ficou com H.264 + AAC e duração de 8,000 s.
Sem FFmpeg, entregue só o vídeo e diga isso ao usuário.

## Checklist final

- [ ] Brief e storyboard feitos antes do código.
- [ ] Compila com `-Wall -Wextra -Wpedantic` sem avisos.
- [ ] Contagem de quadros = duração × fps.
- [ ] Pelo menos 6 quadros revisados, incluindo transições.
- [ ] Nenhuma cena sobreposta sem transição planejada.
- [ ] Texto legível (contraste forte) e dentro das margens de 6%.
- [ ] `ffprobe` confirma codec, dimensões, fps e duração.
- [ ] Pasta `frames/` apagada.

## Limites conhecidos

- Não há 3D real nem navegador.
- Texto é GDI, só no Windows. Fora dele, a versão atual não renderiza texto.
- Sem áudio nativo; mixagem depende de FFmpeg externo.
- Quadros BMP ocupam muito disco. Prefira vídeos curtos.
- O encoder não avalia qualidade. Quem julga é a revisão dos quadros.
