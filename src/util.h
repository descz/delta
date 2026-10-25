/* util.h - buffers, UTF-8, largura de exibicao, arquivos.
 * Zero dependencias. C11. */
#ifndef DELTA_UTIL_H
#define DELTA_UTIL_H

#define DELTA_VERSION "1.0.0"

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

/* ------------------------------------------------------------------ */
/* Str: buffer de bytes com terminacao garantida                       */
/* ------------------------------------------------------------------ */
typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} Str;

void  str_init(Str *s);
void  str_free(Str *s);
void  str_clear(Str *s);
void  str_reserve(Str *s, size_t extra);
void  str_add(Str *s, const char *bytes, size_t n);
void  str_adds(Str *s, const char *cstr);
void  str_addc(Str *s, char c);
void  str_addf(Str *s, const char *fmt, ...);
void  str_addcp(Str *s, uint32_t cp);
void  str_addn(Str *s, const char *cstr, size_t max);
void  str_truncate(Str *s, size_t len);
char *str_steal(Str *s);

/* ------------------------------------------------------------------ */
/* UTF-8                                                               */
/* ------------------------------------------------------------------ */
/* Decodifica 1 codepoint. Retorna bytes consumidos (0 se invalido). */
int      utf8_decode(const char *s, size_t len, uint32_t *out);
/* Escreve codepoint em buf (>=4 bytes). Retorna bytes escritos. */
int      utf8_encode(uint32_t cp, char *buf);
size_t   utf8_len(const char *s);
/* Largura de coluna de um codepoint (0 = combinante, 2 = wide). */
int      cp_width(uint32_t cp);
/* Largura visivel de uma string UTF-8, ignorando sequencias ANSI. */
int      str_width(const char *s);
int      strn_width(const char *s, size_t n);
/* Copia ate `cols` colunas visiveis. */
size_t   str_clip_cols(const char *s, size_t n, int cols);

/* ------------------------------------------------------------------ */
/* Strings                                                             */
/* ------------------------------------------------------------------ */
int      str_eq(const char *a, const char *b);
int      str_ieq(const char *a, const char *b);
int      str_starts(const char *s, const char *prefix);
int      str_istarts(const char *s, const char *prefix);
int      str_ends(const char *s, const char *suffix);
const char *str_find(const char *hay, const char *needle);
char    *str_dup(const char *s);
char    *str_ndup(const char *s, size_t n);

/* ------------------------------------------------------------------ */
/* Arquivos                                                            */
/* ------------------------------------------------------------------ */
/* Le arquivo inteiro. Retorna NULL em erro. *out_len opcional. */
char    *file_read(const char *path, size_t *out_len);
int      file_write(const char *path, const char *data, size_t len);
/* Acrescenta ao fim (cria se nao existir). */
int      file_append(const char *path, const char *data, size_t len);
int      file_exists(const char *path);
int      dir_exists(const char *path);
int      dir_make(const char *path);           /* cria recursivamente */
int      path_is_dir(const char *path);
char    *path_join(const char *a, const char *b);
char    *path_dirname(const char *path);
char    *path_basename(const char *path);
/* Expande ~ usando HOME/USERPROFILE. Retorna alocado. */
char    *path_expand(const char *path);
char    *env_get(const char *name);
char    *cwd_get(void);
char    *home_get(void);
/* Raiz do Delta (cria se faltar). Ordem: --core, DELTA_CORE, pointer.txt
   em <home>/.deltaCore, <home>/.deltaCore. No Windows <home> = C:\Users\<nome>. */
char    *delta_home(void);
/* Forca a raiz nesta execucao (flag --core). */
void     delta_set_core(const char *dir);
/* Cria/garante ~/.deltaCore com skills/, mcps/, extensions/, system/,
   sessions/, paste/, jobs/, themes/ + memory.json e system/prompt.md. */
void     delta_bootstrap(void);
/* Lista entradas de um diretorio (nomes, sem . e ..). Termina com NULL. */
char   **dir_list(const char *path, size_t *count);
void     dir_list_free(char **list, size_t count);

/* ------------------------------------------------------------------ */
/* Diversos                                                            */
/* ------------------------------------------------------------------ */
uint64_t now_ms(void);
void     msleep(int ms);
/* id curto hex de 8 chars, como o Delta usa nos entries de sessao. */
void     short_id(char out[9]);
void     uuid_v4(char out[37]);
void     iso_now(char out[32]);
/* Codifica em base64 (usado para anexar imagens). */
void     base64_encode(Str *out, const unsigned char *data, size_t n);
/* Isola a string para JSON (sem as aspas). */
void     json_escape(Str *out, const char *s, size_t n);

#endif /* DELTA_UTIL_H */
