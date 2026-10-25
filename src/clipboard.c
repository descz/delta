/* clipboard.c - clipboard do Windows + encoder PNG proprio.
 * O PNG usa deflate "stored" (sem compressao), entao nao precisa de zlib. */
#include "clipboard.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================== */
/* PNG (zlib com blocos stored)                                        */
/* ================================================================== */
static uint32_t crc_table[256];
static int crc_ready = 0;

static void crc_init(void)
{
    if (crc_ready) return;
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
    crc_ready = 1;
}

static uint32_t crc32_buf(const unsigned char *d, size_t n, uint32_t crc)
{
    crc_init();
    crc = ~crc;
    for (size_t i = 0; i < n; i++) crc = crc_table[(crc ^ d[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

static void put_be32(Str *o, uint32_t v)
{
    str_addc(o, (char)((v >> 24) & 0xFF));
    str_addc(o, (char)((v >> 16) & 0xFF));
    str_addc(o, (char)((v >> 8) & 0xFF));
    str_addc(o, (char)(v & 0xFF));
}

static void png_chunk(Str *o, const char *type, const unsigned char *data, size_t n)
{
    put_be32(o, (uint32_t)n);
    size_t start = o->len;
    str_add(o, type, 4);
    if (n) str_add(o, (const char *)data, n);
    uint32_t crc = crc32_buf((const unsigned char *)o->data + start, o->len - start, 0);
    put_be32(o, crc);
}

static uint32_t adler32_buf(const unsigned char *d, size_t n)
{
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < n; i++) {
        a = (a + d[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

static void zlib_stored(Str *o, const unsigned char *data, size_t n)
{
    str_addc(o, 0x78);
    str_addc(o, 0x01);
    size_t i = 0;
    do {
        size_t bloco = n - i;
        if (bloco > 65535) bloco = 65535;
        int ultimo = (i + bloco >= n);
        str_addc(o, (char)(ultimo ? 1 : 0));
        str_addc(o, (char)(bloco & 0xFF));
        str_addc(o, (char)((bloco >> 8) & 0xFF));
        str_addc(o, (char)(~bloco & 0xFF));
        str_addc(o, (char)((~bloco >> 8) & 0xFF));
        if (bloco) str_add(o, (const char *)data + i, bloco);
        i += bloco;
    } while (i < n);
    uint32_t ad = adler32_buf(data, n);
    put_be32(o, ad);
}

int png_encode(const unsigned char *rgb, int w, int h, int channels, Str *out)
{
    if (!rgb || w <= 0 || h <= 0 || (channels != 3 && channels != 4)) return 0;
    str_clear(out);
    static const unsigned char sig[8] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };
    str_add(out, (const char *)sig, 8);

    unsigned char ihdr[13];
    ihdr[0] = (unsigned char)(w >> 24); ihdr[1] = (unsigned char)(w >> 16);
    ihdr[2] = (unsigned char)(w >> 8);  ihdr[3] = (unsigned char)(w);
    ihdr[4] = (unsigned char)(h >> 24); ihdr[5] = (unsigned char)(h >> 16);
    ihdr[6] = (unsigned char)(h >> 8);  ihdr[7] = (unsigned char)(h);
    ihdr[8] = 8;                                     /* 8 bits por canal */
    ihdr[9] = (channels == 4) ? 6 : 2;               /* RGBA ou RGB */
    ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    png_chunk(out, "IHDR", ihdr, 13);

    /* scanlines com filtro 0 */
    size_t linha = (size_t)w * (size_t)channels + 1;
    unsigned char *raw = (unsigned char *)malloc(linha * (size_t)h);
    if (!raw) return 0;
    for (int y = 0; y < h; y++) {
        raw[(size_t)y * linha] = 0;
        memcpy(raw + (size_t)y * linha + 1, rgb + (size_t)y * (size_t)w * (size_t)channels,
               (size_t)w * (size_t)channels);
    }
    Str z; str_init(&z);
    zlib_stored(&z, raw, linha * (size_t)h);
    free(raw);
    png_chunk(out, "IDAT", (const unsigned char *)z.data, z.len);
    str_free(&z);
    png_chunk(out, "IEND", NULL, 0);
    return 1;
}

/* ================================================================== */
/* Clipboard do Windows                                                */
/* ================================================================== */
#ifdef _WIN32
#include <windows.h>

static char *wide_to_utf8(const WCHAR *w)
{
    if (!w) return NULL;
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 0) return NULL;
    char *s = (char *)malloc((size_t)n);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
    return s;
}

int clipboard_available(void) { return 1; }

int clipboard_write_text(const char *utf8)
{
    if (!utf8) return 0;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (wlen <= 0) return 0;
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)wlen * sizeof(WCHAR));
    if (!h) return 0;
    WCHAR *w = (WCHAR *)GlobalLock(h);
    if (!w) { GlobalFree(h); return 0; }
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, wlen);
    GlobalUnlock(h);
    if (!OpenClipboard(NULL)) { GlobalFree(h); return 0; }
    EmptyClipboard();
    int ok = SetClipboardData(CF_UNICODETEXT, h) != NULL;
    CloseClipboard();
    if (!ok) GlobalFree(h);
    return ok;
}

/* Extrai um canal de um pixel 32bpp usando a mascara do DIB. */
static unsigned char canal(unsigned v, uint32_t m)
{
    if (!m) return 0;
    int s = 0;
    while (!(m & 1u)) { m >>= 1; s++; }
    v = (v >> s) & m;
    if (m < 255) v = v * 255u / m;          /* expande 5/10 bits para 8 */
    return (unsigned char)v;
}

static char *salvar_imagem_dib(HGLOBAL h, const char *dir)
{
    const unsigned char *d = (const unsigned char *)GlobalLock(h);
    if (!d) return NULL;

    /* BITMAPINFOHEADER */
    int      header = 0;
    int32_t  w = 0, hgt = 0;
    uint16_t bpp = 0;
    uint32_t comp = 0;
    memcpy(&header, d, 4);
    memcpy(&w, d + 4, 4);
    memcpy(&hgt, d + 8, 4);
    memcpy(&bpp, d + 14, 2);
    memcpy(&comp, d + 16, 4);

    int top_down = 0;
    if (hgt < 0) { hgt = -hgt; top_down = 1; }
    if (getenv("DELTA_CLIP_DEBUG"))
        fprintf(stderr, "[clip] dib: header=%d w=%d h=%d bpp=%u comp=%u\n",
                header, w, hgt, (unsigned)bpp, (unsigned)comp);

    /* comp 3 = BI_BITFIELDS (GDI+ escreve DIB 32bpp assim); o default
       BI_RGB 32bpp usa as mesmas mascaras (B,G,R) e cai no mesmo caminho */
    uint32_t mR = 0x00FF0000u, mG = 0x0000FF00u, mB = 0x000000FFu;
    int bitfields = 0;
    if (comp == 3 && bpp == 32) {
        bitfields = 1;
        const unsigned char *masks = (header >= 108) ? d + 40 : d + header;
        memcpy(&mR, masks, 4);
        memcpy(&mG, masks + 4, 4);
        memcpy(&mB, masks + 8, 4);
    } else if (comp != 0 || (bpp != 24 && bpp != 32)) {
        GlobalUnlock(h);
        return NULL;
    }

    size_t origem_bpp = (size_t)bpp / 8;
    size_t origem_linha = (((size_t)w * origem_bpp + 3) / 4) * 4;   /* linhas alinhadas em 4 */
    const unsigned char *pixels = d + header + (bitfields && header < 108 ? 12 : 0);
    int canais = 3;
    unsigned char *rgb = (unsigned char *)malloc((size_t)w * (size_t)hgt * (size_t)canais);
    if (!rgb) { GlobalUnlock(h); return NULL; }

    for (int y = 0; y < hgt; y++) {
        int src = top_down ? y : (hgt - 1 - y);
        const unsigned char *linha = pixels + (size_t)src * origem_linha;
        unsigned char *dst = rgb + (size_t)y * (size_t)w * (size_t)canais;
        if (bpp == 24) {
            for (int x = 0; x < w; x++) {
                dst[x * 3 + 0] = linha[(size_t)x * 3 + 2];   /* BGR -> RGB */
                dst[x * 3 + 1] = linha[(size_t)x * 3 + 1];
                dst[x * 3 + 2] = linha[(size_t)x * 3 + 0];
            }
        } else {
            for (int x = 0; x < w; x++) {
                unsigned v;
                memcpy(&v, linha + (size_t)x * 4, 4);
                dst[x * 3 + 0] = canal(v, mR);
                dst[x * 3 + 1] = canal(v, mG);
                dst[x * 3 + 2] = canal(v, mB);
            }
        }
    }
    GlobalUnlock(h);

    Str png; str_init(&png);
    int ok = png_encode(rgb, w, hgt, canais, &png);
    free(rgb);
    if (!ok) { str_free(&png); if (getenv("DELTA_CLIP_DEBUG")) fprintf(stderr, "[clip] png_encode falhou\n"); return NULL; }

    dir_make(dir);
    /* nome = hash do PNG: reler o mesmo clipboard gera o mesmo arquivo, o que
       deixa a colagem idempotente (dois fallbacks nao viram duas imagens) */
    uint64_t hash = 1469598103934665603ULL;
    for (size_t i = 0; i < png.len; i++) {
        hash ^= (unsigned char)png.data[i];
        hash *= 1099511628211ULL;
    }
    char nome[128];
    snprintf(nome, sizeof nome, "colado-%016llx.png", (unsigned long long)hash);
    char *path = path_join(dir, nome);
    ok = file_write(path, png.data, png.len);
    str_free(&png);
    if (!ok) { if (getenv("DELTA_CLIP_DEBUG")) fprintf(stderr, "[clip] file_write falhou em %s\n", path); free(path); return NULL; }
    return path;
}

static int eh_imagem(const char *nome)
{
    const char *ext[] = { ".png", ".jpg", ".jpeg", ".gif", ".webp", ".bmp", NULL };
    size_t n = strlen(nome);
    for (int i = 0; ext[i]; i++) {
        size_t l = strlen(ext[i]);
        if (n > l) {
            int igual = 1;
            for (size_t k = 0; k < l; k++) {
                char a = nome[n - l + k], b = ext[i][k];
                if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
                if (a != b) { igual = 0; break; }
            }
            if (igual) return 1;
        }
    }
    return 0;
}

static int eh_video(const char *nome)
{
    const char *ext[] = { ".mp4", ".mov", ".avi", ".mkv", ".webm", ".m4v", ".mpg", ".mpeg", ".wmv", NULL };
    size_t n = strlen(nome);
    for (int i = 0; ext[i]; i++) {
        size_t l = strlen(ext[i]);
        if (n > l) {
            int igual = 1;
            for (size_t k = 0; k < l; k++) {
                char a = nome[n - l + k], b = ext[i][k];
                if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
                if (b >= 'A' && a != b) { igual = 0; break; }
                if (a != b) { igual = 0; break; }
            }
            if (igual) return 1;
        }
    }
    return 0;
}

int clipboard_read(Clip *c, const char *dir)
{
    clip_init(c);
    if (!OpenClipboard(NULL)) {
        if (getenv("DELTA_CLIP_DEBUG")) fprintf(stderr, "[clip] OpenClipboard falhou (erro %lu)\n", GetLastError());
        return 0;
    }
    if (getenv("DELTA_CLIP_DEBUG")) {
        fprintf(stderr, "[clip] formatos:");
        UINT f = 0;
        while ((f = EnumClipboardFormats(f)) != 0) fprintf(stderr, " %u", f);
        fprintf(stderr, "\n");
    }

    /* 1) texto */
    if (IsClipboardFormatAvailable(CF_UNICODETEXT) && GetClipboardData(CF_UNICODETEXT)) {
        HGLOBAL h = GetClipboardData(CF_UNICODETEXT);
        WCHAR *w = (WCHAR *)GlobalLock(h);
        char *t = wide_to_utf8(w);
        if (w) GlobalUnlock(h);
        if (t) {
            c->kind = CLIP_TEXT;
            c->text = t;
            CloseClipboard();
            return 1;
        }
    }

    /* 2) arquivos (inclui videos copiados no Explorer) */
    if (IsClipboardFormatAvailable(CF_HDROP)) {
        HGLOBAL h = GetClipboardData(CF_HDROP);
        if (h) {
            HDROP drop = (HDROP)h;
            UINT n = DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0);
            if (n > 0) {
                WCHAR buf[1024];
                if (DragQueryFileW(drop, 0, buf, 1024) > 0) {
                    char *path = wide_to_utf8(buf);
                    if (path) {
                        /* imagem copiada como arquivo tambem entra como imagem */
                        if (eh_imagem(path)) {
                            c->kind = CLIP_IMAGE;
                        } else {
                            c->kind = CLIP_FILES;
                            c->is_video = eh_video(path);
                        }
                        c->path = path;
                        CloseClipboard();
                        return 1;
                    }
                }
            }
        }
    }

    /* 3) imagem */
    if (IsClipboardFormatAvailable(CF_DIB)) {
        HGLOBAL h = GetClipboardData(CF_DIB);
        if (h) {
            char *path = salvar_imagem_dib(h, dir);
            if (path) {
                c->kind = CLIP_IMAGE;
                c->path = path;
                CloseClipboard();
                return 1;
            }
        }
    }

    CloseClipboard();
    return 0;
}
#else
int clipboard_available(void) { return 0; }
int clipboard_write_text(const char *utf8)
{
    (void)utf8;
    return 0;
}
int clipboard_read(Clip *c, const char *dir)
{
    (void)dir;
    clip_init(c);
    return 0;
}
#endif

void clip_init(Clip *c)
{
    memset(c, 0, sizeof(*c));
    c->kind = CLIP_NONE;
}

void clip_free(Clip *c)
{
    if (!c) return;
    free(c->text);
    free(c->path);
    clip_init(c);
}
