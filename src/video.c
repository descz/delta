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

    for (int i = 0; i < max; i++) {
        if (dur > 0) {
            long long t = (long long)((double)dur * ((double)i + 0.5) / (double)max);
            PROPVARIANT pv;
            PropVariantInit(&pv);
            pv.vt = VT_I8;
            pv.hVal.QuadPart = t;
            if (FAILED(IMFSourceReader_SetCurrentPosition(reader, &GUID_NULL, &pv))) continue;
        }

        IMFSample *sp = NULL;
        DWORD flags = 0;
        if (FAILED(IMFSourceReader_ReadSample(reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                              0, NULL, &flags, NULL, &sp)))
            continue;
        if (!sp) continue;
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) { IMFSample_Release(sp); break; }

        IMFMediaBuffer *mb = NULL;
        if (FAILED(IMFSample_ConvertToContiguousBuffer(sp, &mb))) {
            IMFSample_Release(sp);
            continue;
        }
        BYTE *dados = NULL;
        DWORD len = 0;
        if (FAILED(IMFMediaBuffer_Lock(mb, &dados, NULL, &len))) {
            IMFMediaBuffer_Release(mb);
            IMFSample_Release(sp);
            continue;
        }
        {
            /* o buffer tem que caber Y (w*h) + UV (w*h/2): amostra menor =
               formato inesperado; converte NAO (leitura fora do buffer) */
            size_t precisa = (size_t)w * (size_t)h * 3 / 2;
            if (len >= precisa) {
                char dest[1024];
                snprintf(dest, sizeof dest, "%s/%s-%02d.png", dest_dir, prefix, i + 1);
                if (frame_para_png(dados, w, h, dest)) feitos++;
            }
        }
        IMFMediaBuffer_Unlock(mb);
        IMFMediaBuffer_Release(mb);
        IMFSample_Release(sp);
    }

fim:
    if (reader) IMFSourceReader_Release(reader);
    MFShutdown();
    free(wpath);
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
