---
name: video-de-codigo
description: Gerar videos animados de alta qualidade com codigo C (estilo motion graphics do X.com) — brief visual, gerador de frames em C, MP4 pelo Delta. Use quando o usuario pedir "video animado", "motion graphic", "animacao programatica" ou video explicativo.
---

# Vídeo com código

Vídeos nascem de CÓDIGO: um gerador em C desenha os frames, o Delta codifica
o MP4 (codec do sistema). Nada de gravar tela.

## O pipeline (sempre este)

1. **BRIEF VISUAL primeiro** (nunca pule): a ideia da animação, paleta de
   2-3 cores exatas em hex, o ritmo (o que entra quando), dimensões
   (1280x720 paisagem ou 1080x1920 vertical) e duração (8-20s basta).
2. **Escreva `gera.c`** com o mini-canvas (abaixo) — cada frame desenhado em
   função de `t` (0 a 1).
3. **Compile e rode**: `gcc -O2 gera.c -o gera -lm && ./gera` → `frames/0000.bmp ...`
4. **Codifique**: `delta --render-video frames/ video.mp4 30`
5. **Revise de verdade**: `delta --video-frames video.mp4 check/` e olhe 2-3
   frames extraídos; se a composição/movimento não bateu com o brief, ajuste
   e repita. Video bom nasce de 2-3 iterações.

## As 6 técnicas que separam o vídeo BOM do "vídeo de IA"

Estas não são opcionais — são o que dá o visual dos vídeos bons:

1. **Supersampling 2x**: render em 2560x1440, rebaixa para 1280x720
   (anti-aliasing — sem isso tudo tem borda de escada);
2. **Acumulação HDR + tonemap**: o brilho acumula em buffer float e só na
   hora de gravar aplica `x*(2.51x+0.03)/(x*(2.43x+0.59)+0.14)` — o glow
   fica quente sem estourar em branco chapado;
3. **Rastros**: NUNCA limpe o buffer — multiplique por ~0.88 a cada frame
   (movimento deixa cauda luminosa = a assinatura visual);
4. **Glow aditivo com falloff quadrático**: núcleo 3x mais brilhante + halo
   suave; tudo ADITIVO (brilho soma, nunca substitui);
5. **Rampa de cor, não cores aleatórias**: cor por profundidade/camada numa
   interpolação de 3 paradas (ex.: âmbar → branco-azulado);
6. **Parallax em camadas**: 3-4 anéis/camadas com velocidades diferentes —
   profundidade de graça.

## O mini-canvas (cole no topo do gera.c)

```c
static float *hdr; static unsigned char *img; static int CW, CH, RW, RH;
static void canvas_init(int w, int h) {
    CW = w; CH = h; RW = w*2; RH = h*2;          /* supersample 2x */
    hdr = calloc((size_t)RW * RH * 3, sizeof(float));
    img = malloc((size_t)CW * CH * 3);
}
static void px_add(float x, float y, float r, float g, float b, float f) {
    int xi = (int)x, yi = (int)y;                 /* jah em espaco de render */
    if (xi < 0 || yi < 0 || xi >= RW || yi >= RH) return;
    float *p = hdr + ((size_t)yi * RW + xi) * 3;
    p[0] += r*f; p[1] += g*f; p[2] += b*f;        /* aditivo: brilho soma */
}
static void glow(float x, float y, float r, float cr, float cg, float cb) {
    x *= 2; y *= 2; r *= 2;                       /* canvas final -> render 2x */
    int x0 = (int)(x - r*4), x1 = (int)(x + r*4);
    int y0 = (int)(y - r*4), y1 = (int)(y + r*4);
    float r2 = r * r;
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 > RW) x1 = RW; if (y1 > RH) y1 = RH;
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++) {
            float d2 = (xx-x)*(xx-x) + (yy-y)*(yy-y);
            float f = 1.0f - d2/(r2*16);
            if (f <= 0) continue;
            float nuc = 1.0f - d2/r2;
            px_add(xx, yy, cr, cg, cb, (f*f) * (nuc > 0 ? 1.0f + nuc*2.0f : 0.0f));
        }
}
static void rastro(float decaimento) {           /* chame por frame: 0.85-0.93 */
    for (size_t k = 0; k < (size_t)RW * RH * 3; k++) hdr[k] *= decaimento;
}
static float tonemap(float x) {                   /* x ~ [0, 3+] */
    return x*(2.51f*x+0.03f)/(x*(2.43f*x+0.59f)+0.14f);
}
static void revela(float exposicao, const char *dir, int n) {  /* 1 frame -> BMP */
    for (int y = 0; y < CH; y++) for (int x = 0; x < CW; x++) {
        long r = 0, g = 0, b = 0;
        for (int sy = 0; sy < 2; sy++) for (int sx = 0; sx < 2; sx++) {
            float *p = hdr + ((size_t)(y*2+sy) * RW + (x*2+sx)) * 3;
            r += (long)(tonemap(p[0]*exposicao)*255);
            g += (long)(tonemap(p[1]*exposicao)*255);
            b += (long)(tonemap(p[2]*exposicao)*255);
        }
        unsigned char *o = img + ((size_t)y*CW + x)*3;
        o[0] = b/4; o[1] = g/4; o[2] = r/4;
        float dx = (x - CW/2.0f)/(CW/2.0f), dy = (y - CH/2.0f)/(CH/2.0f);
        float vig = 1.0f - 0.35f*(dx*dx + dy*dy);  /* vinheta */
        o[0] *= vig; o[1] *= vig; o[2] *= vig;
    }
    char path[300]; snprintf(path, sizeof path, "%s/%04d.bmp", dir, n);
    FILE *f = fopen(path, "wb");
    int lin = CW*3; unsigned char *inv = malloc((size_t)lin*CH);
    for (int y = 0; y < CH; y++) memcpy(inv + (size_t)y*lin, img + (size_t)(CH-1-y)*lin, lin);
    unsigned char cab[54] = {'B','M'};
    int sz = 54 + lin*CH; unsigned zero = 0;
    memcpy(cab+2, &sz, 4); memcpy(cab+10, &(unsigned){54}, 4);
    memcpy(cab+14, &(unsigned){40}, 4);
    memcpy(cab+18, &CW, 4); memcpy(cab+22, &CH, 4);
    unsigned short um = 1; memcpy(cab+26, &um, 2);
    memcpy(cab+28, &(unsigned short){24}, 2); memcpy(cab+34, &sz, 4);
    fwrite(cab, 1, 54, f); fwrite(inv, 1, (size_t)lin*CH, f);
    fclose(f); free(inv);
}
static float ease(float t) { return t < .5f ? 2*t*t : 1 - (-2*t+2)*(-2*t+2)/2; }
```

## Estrutura do gerador

```c
int main(void) {
    canvas_init(1280, 720);
    system("mkdir frames 2>NUL 1>NUL");
    int TOTAL = 240;                        /* 8s a 30fps */
    for (int f = 0; f < TOTAL; f++) {
        float t = (float)f / TOTAL;
        rastro(0.88f);
        /* desenhe a cena no instante t: particulas com glow(), camadas com
           velocidades diferentes, o nucleo com glow() maior... */
        revela(0.01f, "frames", f);         /* exposicao: ajuste ate o brilho ficar certo */
    }
    return 0;
}
```

## Referência completa

O gerador exemplo (galáxia: 900 partículas em 4 camadas de parallax, rastros,
HDR, rampa âmbar→azulado, sol central) está no histórico do projeto — a
estrutura é sempre a mesma: estado das partículas fora do loop (só 1x),
frame = função de t, rastro, revela.

## Regras de qualidade

- **Uma ideia por vídeo**: um conceito visual claro; paleta definida no brief;
  fundo escuro escala melhor com glow;
- **Movimento SEMPRE com easing** — nada linear, nada teleporta;
- **Ritmo**: mudança de estado a cada 1.5-3s; movimento contínuo mantém vivo;
- **Geometria > texto**: fonte em C é sofrimento — motion graphics geométrico
  (formas, grades, ondas, órbitas, partículas) é o forte;
- **Dimensões pares** (múltiplo de 16 ideal): 1280x720, 1080x1920;
- **Gerador rápido**: sem alocação por frame no loop de desenho (30fps x 20s =
  600 frames têm que sair em segundos).

## Antipadrões

- Codificar sem o brief (vídeo vira enfeite sem narrativa);
- Cores saturadas aleatórias por partícula (use rampa);
- Limpar o buffer por frame (mata os rastros);
- Brilho estourado em branco chapado (use HDR + tonemap, não clamp);
- Entregar sem revisar os frames de volta.
