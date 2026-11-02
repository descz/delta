/* video.c - frames de video para o modelo "ver", sem dependencia externa.
 *
 * Windows: Media Foundation decodifica (codec nativo do sistema — H.264 de
 * fabrica; HEVC/VP9/AV1 via extensao) no formato NV12 nativo; a conversao
 * YUV->RGB e a reducao (box filter) sao daqui, e o PNG sai do png_encode
 * proprio (o mesmo do clipboard). Zero API de terceiros.
 * POSIX: ffmpeg/ffprobe do usuario se existirem no PATH.
 */
#include "video.h"
#include "util.h"
#include "clipboard.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
/* ================= Windows: Media Foundation ================= */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <objbase.h>

static const GUID g_MFVideoFormat_NV12 =
    {0x3231564e,0x0000,0x0010,{0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71}}; /* FOURCC 'NV12' */

/* NV12 (Y plano + UV intercalado em metade da resolucao) -> RGB reduzido.
 * Downscale box filter no Y e no UV junto. */
static void nv12_para_rgb(const unsigned char *yv, const unsigned char *uv,
                          int w, int h, unsigned char *dst, int tw, int th)
{
    for (int y = 0; y < th; y++) {
        int sy0 = (int)((long long)y * h / th);
        int sy1 = (int)((long long)(y + 1) * h / th);
        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (int x = 0; x < tw; x++) {
            int sx0 = (int)((long long)x * w / tw);
            int sx1 = (int)((long long)(x + 1) * w / tw);
            if (sx1 <= sx0) sx1 = sx0 + 1;
            long r = 0, g = 0, b = 0, cnt = 0;
            for (int yy = sy0; yy < sy1; yy++) {
                const unsigned char *linha_y = yv + (size_t)yy * (size_t)w;
                for (int xx = sx0; xx < sx1; xx++) {
                    int u = uv[(size_t)((yy >> 1) * (size_t)w + (size_t)(xx & ~1))];
                    int v = uv[(size_t)((yy >> 1) * (size_t)w + (size_t)(xx & ~1) + 1)];
                    int y8 = linha_y[xx];
                    int rr = (int)(y8 + 1.402 * (v - 128));
                    int gg = (int)(y8 - 0.344136 * (u - 128) - 0.714136 * (v - 128));
                    int bb = (int)(y8 + 1.772 * (u - 128));
                    r += rr > 255 ? 255 : (rr < 0 ? 0 : rr);
                    g += gg > 255 ? 255 : (gg < 0 ? 0 : gg);
                    b += bb > 255 ? 255 : (bb < 0 ? 0 : bb);
                    cnt++;
                }
            }
            if (!cnt) cnt = 1;
            dst[((size_t)y * (size_t)tw + (size_t)x) * 3]     = (unsigned char)(r / cnt);
            dst[((size_t)y * (size_t)tw + (size_t)x) * 3 + 1] = (unsigned char)(g / cnt);
            dst[((size_t)y * (size_t)tw + (size_t)x) * 3 + 2] = (unsigned char)(b / cnt);
        }
    }
}

static int frame_para_png(const unsigned char *dados, int w, int h, const char *dest)
{
    size_t plano_y = (size_t)w * (size_t)h;
    const unsigned char *yv = dados;
    const unsigned char *uv = dados + plano_y;

    int tw = w > 512 ? 512 : w;
    int th = (int)((long long)h * tw / (w ? w : 1));
    if (th < 1) th = 1;
    unsigned char *rgb = (unsigned char *)malloc((size_t)tw * 3 * (size_t)th);
    if (!rgb) return 0;
    nv12_para_rgb(yv, uv, w, h, rgb, tw, th);

    Str png;
    str_init(&png);
    int ok = png_encode(rgb, tw, th, 3, &png);
    FILE *fpng = fopen(dest, "wb");
    if (fpng) {
        size_t wr = fwrite(png.data ? png.data : "", 1, png.len, fpng);
        fclose(fpng);
        ok = (wr == png.len);
    } else ok = 0;
    str_free(&png);
    free(rgb);
    return ok;
}

int video_frames(const char *video_path, const char *dest_dir,
                 const char *prefix, int max)
{
    if (!video_path || !dest_dir || !prefix || max <= 0) return 0;
    dir_make(dest_dir);   /* o destino tem que existir (fopen nao cria pasta) */
    if (CoInitializeEx(NULL, COINIT_MULTITHREADED) == RPC_E_CHANGED_MODE)
        return 0;

    int feitos = 0;
    IMFSourceReader *reader = NULL;
    int w = 0, h = 0;

    int nw = MultiByteToWideChar(CP_UTF8, 0, video_path, -1, NULL, 0);
    WCHAR *wpath = NULL;
    if (nw <= 0) { CoUninitialize(); return 0; }
    wpath = (WCHAR *)malloc((size_t)nw * sizeof(WCHAR));
    if (!wpath) { CoUninitialize(); return 0; }
    MultiByteToWideChar(CP_UTF8, 0, video_path, -1, wpath, nw);

    if (FAILED(MFStartup(MF_VERSION, 0))) { free(wpath); CoUninitialize(); return 0; }
    if (FAILED(MFCreateSourceReaderFromURL(wpath, NULL, &reader))) goto fim;

    /* pede saida NV12 (a nativa do decoder h264): e' o que ativa a DECODIFICAO
       no SourceReader — sem isso ele entrega os samples crus (comprimidos) */
    {
        IMFMediaType *mt = NULL;
        if (FAILED(MFCreateMediaType(&mt))) goto fim;
        IMFAttributes_SetGUID((IMFAttributes *)mt, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
        IMFAttributes_SetGUID((IMFAttributes *)mt, &MF_MT_SUBTYPE, &g_MFVideoFormat_NV12);
        HRESULT hrm = IMFSourceReader_SetCurrentMediaType(reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                          NULL, mt);
        IMFAttributes_Release((IMFAttributes *)mt);
        if (FAILED(hrm)) goto fim;
    }

    /* dimensoes do media type atual */
    {
        IMFMediaType *atual = NULL;
        UINT64 fwh = 0;
        if (FAILED(IMFSourceReader_GetCurrentMediaType(reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                       &atual)))
            goto fim;
        if (SUCCEEDED(IMFAttributes_GetUINT64((IMFAttributes *)atual, &MF_MT_FRAME_SIZE, &fwh))) {
            w = (int)(fwh >> 32);
            h = (int)(fwh & 0xFFFFFFFF);
        }
        IMFMediaType_Release(atual);
    }
    if (w < 8 || h < 8) goto fim;

    /* duracao (100ns) */
    long long dur = 0;
    {
        PROPVARIANT vd;
        PropVariantInit(&vd);
        if (SUCCEEDED(IMFSourceReader_GetPresentationAttribute(
                reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, &MF_PD_DURATION, &vd))) {
            if (vd.vt == VT_I8)  dur = vd.hVal.QuadPart;
            if (vd.vt == VT_UI8) dur = (long long)vd.uhVal.QuadPart;
            PropVariantClear(&vd);
        }
    }

    /* DUAS passadas: (1) conta os frames decodificando ate o fim; (2) reabre
     * e salva os frames nos indices espacados. Sem seek e sem duracao — mp4
     * de qualquer gerador funciona (o seek e a fonte de E_FAIL dos mp4s). */
    long long total_frames = 0;
    for (;;) {
        IMFSample *sp = NULL;
        DWORD flags = 0;
        HRESULT hrc = IMFSourceReader_ReadSample(reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                 0, NULL, &flags, NULL, &sp);
        if (FAILED(hrc)) break;
        if (sp) IMFSample_Release(sp);
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        total_frames++;
        if (total_frames > 20000) break;   /* teto de seguranca */
    }
    if (total_frames < 1) goto fim;

    IMFSourceReader_Release(reader);
    reader = NULL;
    if (FAILED(MFCreateSourceReaderFromURL(wpath, NULL, &reader))) goto fim;
    {
        IMFMediaType *mt2 = NULL;
        if (FAILED(MFCreateMediaType(&mt2))) goto fim;
        IMFAttributes_SetGUID((IMFAttributes *)mt2, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
        IMFAttributes_SetGUID((IMFAttributes *)mt2, &MF_MT_SUBTYPE, &g_MFVideoFormat_NV12);
        IMFSourceReader_SetCurrentMediaType(reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, mt2);
        IMFAttributes_Release((IMFAttributes *)mt2);
    }

    /* passada 2: decodifica de novo e grava os indices alvo (espacados) */
    {
        long long idx_f = -1;
        for (;;) {
            IMFSample *sp = NULL;
            DWORD flags = 0;
            HRESULT hrc = IMFSourceReader_ReadSample(reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                     0, NULL, &flags, NULL, &sp);
            if (FAILED(hrc)) break;
            idx_f++;
            int fim_st = (flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0;
            int eh_alvo = 0;
            if (sp) {
                for (int k = 0; k < max; k++)
                    if ((long long)((double)total_frames * ((double)k + 0.5) / (double)max) == idx_f)
                        eh_alvo = 1;
            }
            if (!eh_alvo || !sp) { if (sp) IMFSample_Release(sp); if (fim_st) break; continue; }

            IMFMediaBuffer *mb = NULL;
            if (FAILED(IMFSample_ConvertToContiguousBuffer(sp, &mb))) {
                IMFSample_Release(sp);
                if (fim_st) break;
                continue;
            }
            BYTE *dados = NULL;
            DWORD len = 0;
            if (FAILED(IMFMediaBuffer_Lock(mb, &dados, NULL, &len))) {
                IMFMediaBuffer_Release(mb);
                IMFSample_Release(sp);
                if (fim_st) break;
                continue;
            }
            {
                /* o buffer tem que caber Y (w*h) + UV (w*h/2): amostra menor =
                   formato inesperado; converte NAO (leitura fora do buffer) */
                size_t precisa = (size_t)w * (size_t)h * 3 / 2;
                if (len >= precisa) {
                    char dest[1024];
                    snprintf(dest, sizeof dest, "%s/%s-%02d.png", dest_dir, prefix, feitos + 1);
                    if (frame_para_png(dados, w, h, dest)) feitos++;
                }
            }
            IMFMediaBuffer_Unlock(mb);
            IMFMediaBuffer_Release(mb);
            IMFSample_Release(sp);
            if (fim_st) break;
            if (feitos >= max) break;
        }
    }

fim:
    if (reader) IMFSourceReader_Release(reader);
    MFShutdown();
    free(wpath);
    CoUninitialize();
    return feitos;
}

/* ================= render: frames BMP -> mp4 (SinkWriter) ============ */

/* le um BMP 24bpp: devolve o buffer BGR bottom-up (exatamente o que o MF
 * espera com stride positivo), dimensões por referencia. Alocado. */
static unsigned char *bmp_le(const char *path, int *w, int *h)
{
    size_t n = 0;
    unsigned char *tudo = (unsigned char *)file_read(path, &n);
    if (!tudo || n < 54 || tudo[0] != 'B' || tudo[1] != 'M') { free(tudo); return NULL; }
    int larg = 0, alt = 0;
    unsigned short bpp = 0;
    unsigned int off = 54;
    memcpy(&larg, tudo + 18, 4);
    memcpy(&alt, tudo + 22, 4);
    memcpy(&bpp, tudo + 28, 2);
    memcpy(&off, tudo + 10, 4);
    if (bpp != 24 || larg <= 0 || (alt == 0)) { free(tudo); return NULL; }
    int invertido = alt > 0;             /* BMP padrao: bottom-up */
    int ah = alt < 0 ? -alt : alt;
    size_t stride = ((size_t)larg * 3 + 3) & ~(size_t)3;
    if (n < off + stride * (size_t)ah) { free(tudo); return NULL; }
    (void)invertido;
    *w = larg;
    *h = ah;
    unsigned char *px = (unsigned char *)malloc(stride * (size_t)ah);
    if (!px) { free(tudo); return NULL; }
    memcpy(px, tudo + off, stride * (size_t)ah);
    free(tudo);
    return px;
}

int video_render(const char *frames_dir, const char *out_mp4, int fps)
{
    if (!frames_dir || !out_mp4) return 0;
    if (fps <= 0 || fps > 60) fps = 30;

    size_t n_dir = 0;
    char **arqs = dir_list(frames_dir, &n_dir);
    if (!arqs) return 0;

    /* so' BMPs, ordenados (dir_list ja' ordena por nome) */
    int n_f = 0;
    for (size_t i = 0; arqs[i]; i++) {
        size_t l = strlen(arqs[i]);
        if (l > 4 && (str_ieq(arqs[i] + l - 4, ".bmp"))) n_f++;
    }
    if (n_f == 0) { for (size_t i = 0; arqs[i]; i++) free(arqs[i]); free(arqs); return 0; }

    if (CoInitializeEx(NULL, COINIT_MULTITHREADED) == RPC_E_CHANGED_MODE) {
        for (size_t i = 0; arqs[i]; i++) free(arqs[i]);
        free(arqs);
        return 0;
    }

    int feitos = 0;
    HRESULT hrmf2 = 0;
    IMFSinkWriter *writer = NULL;
    int w = 0, h = 0;
    unsigned char *primeiro = NULL;
    WCHAR *wout = NULL;

    {
        int nw = MultiByteToWideChar(CP_UTF8, 0, out_mp4, -1, NULL, 0);
        if (nw <= 0) goto fim_r;
        wout = (WCHAR *)malloc((size_t)nw * sizeof(WCHAR));
        if (!wout) goto fim_r;
        MultiByteToWideChar(CP_UTF8, 0, out_mp4, -1, wout, nw);
    }

    /* dimensoes pelo PRIMEIRO frame (todos tem que bater) */
    for (size_t i = 0; arqs[i] && !primeiro; i++) {
        size_t l = strlen(arqs[i]);
        if (l > 4 && str_ieq(arqs[i] + l - 4, ".bmp")) {
            char abs1[1300];
            snprintf(abs1, sizeof abs1, "%s/%s", frames_dir, arqs[i]);
            int tw = 0, th = 0;
            primeiro = bmp_le(abs1, &tw, &th);
            if (!primeiro) continue;
            w = tw;
            h = th;
        }
    }
    if (!primeiro || w < 16 || h < 16) goto fim_r;

    if (FAILED(MFStartup(MF_VERSION, 0))) goto fim_r;

    {
        IMFSinkWriter *sw = NULL;
        IMFMediaType *tipo_out = NULL, *tipo_in = NULL;
        if (FAILED(MFCreateSinkWriterFromURL(wout, NULL, NULL, &sw))) goto encerra;

        MFCreateMediaType(&tipo_out);
        IMFAttributes_SetGUID((IMFAttributes *)tipo_out, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
        IMFAttributes_SetGUID((IMFAttributes *)tipo_out, &MF_MT_SUBTYPE, &MFVideoFormat_H264);
        IMFAttributes_SetUINT32((IMFAttributes *)tipo_out, &MF_MT_AVG_BITRATE, 5000000);
        IMFAttributes_SetUINT64((IMFAttributes *)tipo_out, &MF_MT_FRAME_SIZE,
                                ((UINT64)w << 32) | (UINT64)h);
        IMFAttributes_SetUINT32((IMFAttributes *)tipo_out, &MF_MT_INTERLACE_MODE,
                                (UINT32)MFVideoInterlace_Progressive);
        IMFAttributes_SetUINT64((IMFAttributes *)tipo_out, &MF_MT_FRAME_RATE,
                                ((UINT64)fps << 32) | 1);
        IMFAttributes_SetUINT64((IMFAttributes *)tipo_out, &MF_MT_PIXEL_ASPECT_RATIO,
                                ((UINT64)1 << 32) | 1);
        DWORD idx_stream = 0;
        hrmf2 = IMFSinkWriter_AddStream(sw, tipo_out, &idx_stream);
        if (FAILED(hrmf2)) goto encerra;

        MFCreateMediaType(&tipo_in);
        IMFAttributes_SetGUID((IMFAttributes *)tipo_in, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
        IMFAttributes_SetGUID((IMFAttributes *)tipo_in, &MF_MT_SUBTYPE, &MFVideoFormat_RGB24);
        IMFAttributes_SetUINT32((IMFAttributes *)tipo_in, &MF_MT_DEFAULT_STRIDE,
                                (UINT32)(-(int)(w * 3)));   /* negativo = top-down (conv. do sink) */
        IMFAttributes_SetUINT64((IMFAttributes *)tipo_in, &MF_MT_FRAME_SIZE,
                                ((UINT64)w << 32) | (UINT64)h);
        IMFAttributes_SetUINT64((IMFAttributes *)tipo_in, &MF_MT_FRAME_RATE,
                                ((UINT64)fps << 32) | 1);
        IMFAttributes_SetUINT64((IMFAttributes *)tipo_in, &MF_MT_PIXEL_ASPECT_RATIO,
                                ((UINT64)1 << 32) | 1);
        hrmf2 = IMFSinkWriter_SetInputMediaType(sw, idx_stream, tipo_in, NULL);
        if (FAILED(hrmf2))
            goto encerra;

        IMFSinkWriter_BeginWriting(sw);

        long long tick = 10000000LL / fps;
        for (int i = 0; i < n_f; i++) {
            char abs1[1300];
            snprintf(abs1, sizeof abs1, "%s/%s", frames_dir, arqs[i]);
            /* ordem: dir_list ordena por nome; so' bmps */
            size_t l = strlen(arqs[i]);
            if (!(l > 4 && str_ieq(arqs[i] + l - 4, ".bmp"))) continue;
            int tw = 0, th = 0;
            unsigned char *d = bmp_le(abs1, &tw, &th);
            if (!d || tw != w || th != h) { if (d) free(d); continue; }

            IMFMediaBuffer *mb = NULL;
            if (FAILED(MFCreateMemoryBuffer((DWORD)((size_t)w * 3 * (size_t)h), &mb))) {
                free(d);
                continue;
            }
            {
                BYTE *pd = NULL;
                if (SUCCEEDED(IMFMediaBuffer_Lock(mb, &pd, NULL, NULL))) {
                    /* BMP = bottom-up; o sink quer top-down (stride negativo):
                       copia invertendo as linhas */
                    size_t stride = ((size_t)w * 3 + 3) & ~(size_t)3;
                    for (int y = 0; y < h; y++)
                        memcpy(pd + (size_t)y * stride, d + (size_t)(h - 1 - y) * stride, stride);
                    IMFMediaBuffer_SetCurrentLength(mb, (DWORD)((size_t)w * 3 * (size_t)h));
                    IMFMediaBuffer_Unlock(mb);
                    IMFSample *sp = NULL;
                    if (SUCCEEDED(MFCreateSample(&sp))) {
                        IMFSample_AddBuffer(sp, mb);
                        IMFSample_SetSampleTime(sp, (LONGLONG)i * tick);
                        IMFSample_SetSampleDuration(sp, tick);
                        hrmf2 = IMFSinkWriter_WriteSample(sw, idx_stream, sp);
                        if (FAILED(hrmf2))                        IMFSample_Release(sp);
                        feitos++;
                    }
                }
                IMFMediaBuffer_Release(mb);
            }
            free(d);
        }
        hrmf2 = IMFSinkWriter_Finalize(sw);
encerra:
        if (tipo_in) IMFAttributes_Release((IMFAttributes *)tipo_in);
        if (tipo_out) IMFAttributes_Release((IMFAttributes *)tipo_out);
        if (sw) IMFSinkWriter_Release(sw);
    }

    MFShutdown();

fim_r:
    free(wout);
    free(primeiro);
    for (size_t i = 0; arqs[i]; i++) free(arqs[i]);
    free(arqs);
    CoUninitialize();
    return feitos;
}

#else
/* ================= POSIX: ffmpeg do usuario, se houver ================= */
#include <unistd.h>

static void shell_aspas(const char *de, char *ate, size_t n)
{
    size_t j = 0;
    for (size_t i = 0; de[i] && j + 8 < n; i++) {
        if (de[i] == '\'') { j += (size_t)snprintf(ate + j, n - j, "'\\''"); continue; }
        ate[j++] = de[i];
    }
    ate[j] = 0;
}

int video_frames(const char *video_path, const char *dest_dir,
                 const char *prefix, int max)
{
    char cmd[2600], vq[1200], dq[1200];
    shell_aspas(video_path, vq, sizeof vq);
    shell_aspas(dest_dir, dq, sizeof dq);

    /* duracao para espacar os frames */
    double dur = 0;
    snprintf(cmd, sizeof cmd, "ffprobe -v error -show_entries format=duration "
                               "-of csv=p=0 '%s' 2>/dev/null", vq);
    FILE *f = popen(cmd, "r");
    if (f) {
        char lin[64] = {0};
        if (fgets(lin, sizeof lin, f)) dur = atof(lin);
        pclose(f);
    }
    double fps = (dur > 0.5) ? (double)max / dur : 1.0;

    snprintf(cmd, sizeof cmd,
             "ffmpeg -v error -y -i '%s' -vf \"fps=%.4f,scale=512:-1\" "
             "-frames:v %d '%s/%s-%%02d.png' 2>/dev/null",
             vq, fps, max, dq, prefix);
    if (system(cmd) != 0) return 0;

    /* ffmpeg grava a sequencia 01..NN sem buracos: conta ate faltar */
    int feitos = 0;
    for (int i = 1; i <= max; i++) {
        char nome[600];
        snprintf(nome, sizeof nome, "%s/%s-%02d.png", dest_dir, prefix, i);
        if (!file_exists(nome)) break;
        feitos = i;
    }
    return feitos;
}
#endif
