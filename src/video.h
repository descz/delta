/* video.h - extrator de frames de video, sem dependencia externa.
 *
 * Windows: decodifica com Media Foundation (codec do proprio sistema) e
 * codifica JPEG com WIC — nada e' baixado nem linkado estaticamente.
 * POSIX: usa o ffmpeg do usuario se estiver no PATH; sem ele devolve 0 e
 * o chamador cai no placeholder "[video: caminho]".
 *
 * A saida sao ARQUIVOS JPEG (<dest>/<prefix>-NN.jpg) porque a infraestrutura
 * de imagens da mensagem inteira trabalha com caminhos (base64 na hora de
 * enviar). */
#ifndef DELTA_VIDEO_H
#define DELTA_VIDEO_H

/* Extrai ate' `max` frames do video, espacados pela duracao, reduzidos para
 * caber em ~512 px de largura. Retorna o numero de frames gravados (0 = nao
 * consegui: codec ausente, arquivo ruim, sem ffmpeg no POSIX...). */
int video_frames(const char *video_path, const char *dest_dir,
                 const char *prefix, int max);

/* Renderiza UM mp4 a partir de frames BMP (24bpp, nome livre, ordenados) de
 * um diretorio. fps sem sinal = 30. Retorna o numero de frames codificados
 * (0 = nada: diretorio sem .bmp, codec ausente...). Windows: Media
 * Foundation (H.264 do sistema); POSIX: ffmpeg do usuario. */
int video_render(const char *frames_dir, const char *out_mp4, int fps);

#endif /* DELTA_VIDEO_H */
