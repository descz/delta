/* theme.h - temas do Delta (mesmo formato JSON e mesmos tokens) */
#ifndef DELTA_THEME_H
#define DELTA_THEME_H

#include "okcolor.h"

/* Ordem identica a THEME_TOKENS do Delta. */
typedef enum {
    TK_ACCENT, TK_BORDER, TK_BORDER_ACCENT, TK_BORDER_MUTED,
    TK_SUCCESS, TK_ERROR, TK_WARNING, TK_MUTED, TK_DIM, TK_TEXT, TK_THINKING_TEXT,
    TK_SCROLLBAR_TRACK, TK_SCROLLBAR_THUMB, TK_SELECTED_BG,
    TK_SEARCH_MATCH_BG, TK_SEARCH_MATCH_TEXT,
    TK_USER_MESSAGE_BG, TK_USER_MESSAGE_TEXT,
    TK_CUSTOM_MESSAGE_BG, TK_CUSTOM_MESSAGE_TEXT, TK_CUSTOM_MESSAGE_LABEL,
    TK_TOOL_PENDING_BG, TK_TOOL_SUCCESS_BG, TK_TOOL_ERROR_BG, TK_TOOL_TITLE, TK_TOOL_OUTPUT,
    TK_MD_HEADING, TK_MD_LINK, TK_MD_LINK_URL, TK_MD_CODE, TK_MD_CODE_BLOCK,
    TK_MD_CODE_BLOCK_BORDER, TK_MD_QUOTE, TK_MD_QUOTE_BORDER, TK_MD_HR, TK_MD_LIST_BULLET,
    TK_TOOL_DIFF_ADDED, TK_TOOL_DIFF_REMOVED, TK_TOOL_DIFF_CONTEXT,
    TK_SYNTAX_COMMENT, TK_SYNTAX_KEYWORD, TK_SYNTAX_FUNCTION, TK_SYNTAX_VARIABLE,
    TK_SYNTAX_STRING, TK_SYNTAX_NUMBER, TK_SYNTAX_TYPE, TK_SYNTAX_OPERATOR, TK_SYNTAX_PUNCTUATION,
    TK_THINKING_OFF, TK_THINKING_MINIMAL, TK_THINKING_LOW, TK_THINKING_MEDIUM,
    TK_THINKING_HIGH, TK_THINKING_XHIGH, TK_THINKING_MAX,
    TK_BASH_MODE,
    TK_COUNT
} Token;

typedef enum { SLOT_FG, SLOT_BG } TokenSlot;

typedef enum { CHEAP_DEFAULT, CHEAP_RGB, CHEAP_INDEXED } ColorKind;

typedef struct {
    ColorKind kind;
    Rgb       rgb;
    int       index;   /* CHEAP_INDEXED: 0..255 */
} TColor;

typedef struct {
    char   *name;
    int     dark;
    int     mono;          /* paleta so de branco/cinzas */
    TColor  tok[TK_COUNT];
} Theme;

/* Cor padrao do terminal (sem SGR de cor). */
#define TCOLOR_DEFAULT 0xFFFFFFFFu

/* Converte um token para 0xRRGGBB (ou TCOLOR_DEFAULT). */
uint32_t     tcolor_hex(const TColor *c);

/* Carrega o tema. Se path for NULL usa as cores embutidas do tema "dark". */
int          theme_load(Theme *t, const char *path);
void         theme_free(Theme *t);
const TColor *theme_tok(const Theme *t, Token tk);
TokenSlot    token_slot(Token tk);
const char  *token_name(Token tk);
Token        token_from_name(const char *name);   /* TK_COUNT se desconhecido */

/* Temas embutidos (fallback quando nenhum arquivo existe). */
void         theme_load_builtin_dark(Theme *t);
void         theme_load_builtin_mono(Theme *t);

/* Descoberta: procura um tema por nome nos diretorios do Delta. */
char        *theme_find_path(const char *name);

#endif /* DELTA_THEME_H */
