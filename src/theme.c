/* theme.c - carrega os temas JSON do Delta (vars + colors + fallbacks) */
#include "theme.h"
#include "json.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Tabela de tokens (nome, slot) na ordem do THEME_TOKENS do Delta          */
/* ------------------------------------------------------------------ */
static const char *TOKEN_NAMES[TK_COUNT] = {
    "accent", "border", "borderAccent", "borderMuted",
    "success", "error", "warning", "muted", "dim", "text", "thinkingText",
    "scrollbarTrack", "scrollbarThumb", "selectedBg",
    "searchMatchBg", "searchMatchText",
    "userMessageBg", "userMessageText",
    "customMessageBg", "customMessageText", "customMessageLabel",
    "toolPendingBg", "toolSuccessBg", "toolErrorBg", "toolTitle", "toolOutput",
    "mdHeading", "mdLink", "mdLinkUrl", "mdCode", "mdCodeBlock",
    "mdCodeBlockBorder", "mdQuote", "mdQuoteBorder", "mdHr", "mdListBullet",
    "toolDiffAdded", "toolDiffRemoved", "toolDiffContext",
    "syntaxComment", "syntaxKeyword", "syntaxFunction", "syntaxVariable",
    "syntaxString", "syntaxNumber", "syntaxType", "syntaxOperator", "syntaxPunctuation",
    "thinkingOff", "thinkingMinimal", "thinkingLow", "thinkingMedium",
    "thinkingHigh", "thinkingXhigh", "thinkingMax",
    "bashMode",
};

static const TokenSlot TOKEN_SLOTS[TK_COUNT] = {
    SLOT_FG, SLOT_FG, SLOT_FG, SLOT_FG,
    SLOT_FG, SLOT_FG, SLOT_FG, SLOT_FG, SLOT_FG, SLOT_FG, SLOT_FG,
    SLOT_FG, SLOT_FG, SLOT_BG,
    SLOT_BG, SLOT_FG,
    SLOT_BG, SLOT_FG,
    SLOT_BG, SLOT_FG, SLOT_FG,
    SLOT_BG, SLOT_BG, SLOT_BG, SLOT_FG, SLOT_FG,
    SLOT_FG, SLOT_FG, SLOT_FG, SLOT_FG, SLOT_FG,
    SLOT_FG, SLOT_FG, SLOT_FG, SLOT_FG, SLOT_FG,
    SLOT_FG, SLOT_FG, SLOT_FG,
    SLOT_FG, SLOT_FG, SLOT_FG, SLOT_FG,
    SLOT_FG, SLOT_FG, SLOT_FG, SLOT_FG, SLOT_FG,
    SLOT_FG, SLOT_FG, SLOT_FG, SLOT_FG,
    SLOT_FG, SLOT_FG, SLOT_FG,
    SLOT_FG,
};

/* Tokens com fallback (iguais ao theme-tokens.ts). */
static const struct { Token tk; Token fb; } TOKEN_FALLBACKS[] = {
    { TK_SCROLLBAR_TRACK,   TK_MUTED },
    { TK_SCROLLBAR_THUMB,   TK_TEXT },
    { TK_SEARCH_MATCH_BG,   TK_SELECTED_BG },
    { TK_SEARCH_MATCH_TEXT, TK_TEXT },
    { TK_THINKING_MAX,      TK_THINKING_XHIGH },
};

const char *token_name(Token tk) { return (tk < TK_COUNT) ? TOKEN_NAMES[tk] : "?"; }
TokenSlot   token_slot(Token tk) { return (tk < TK_COUNT) ? TOKEN_SLOTS[tk] : SLOT_FG; }

Token token_from_name(const char *name)
{
    for (int i = 0; i < TK_COUNT; i++)
        if (str_eq(TOKEN_NAMES[i], name)) return (Token)i;
    return TK_COUNT;
}

const TColor *theme_tok(const Theme *t, Token tk) { return &t->tok[tk]; }

uint32_t tcolor_hex(const TColor *c)
{
    if (!c || c->kind == CHEAP_DEFAULT) return TCOLOR_DEFAULT;
    if (c->kind == CHEAP_INDEXED) {
        int idx = c->index;
        if (idx < 0) return TCOLOR_DEFAULT;
        if (idx < 16) {
            static const uint32_t b16[16] = {
                0x000000, 0x800000, 0x008000, 0x808000, 0x000080, 0x800080, 0x008080, 0xc0c0c0,
                0x808080, 0xff0000, 0x00ff00, 0xffff00, 0x0000ff, 0xff00ff, 0x00ffff, 0xffffff
            };
            return b16[idx];
        }
        if (idx < 232) {
            int v = idx - 16;
            int r = v / 36, g = (v / 6) % 6, b = v % 6;
            uint32_t rr = (uint32_t)(r ? r * 40 + 55 : 0);
            uint32_t gg = (uint32_t)(g ? g * 40 + 55 : 0);
            uint32_t bb = (uint32_t)(b ? b * 40 + 55 : 0);
            return (rr << 16) | (gg << 8) | bb;
        }
        uint32_t v = (uint32_t)(8 + (idx - 232) * 10);
        return (v << 16) | (v << 8) | v;
    }
    Rgb8 b = rgb_to_rgb8(c->rgb);
    return ((uint32_t)b.r << 16) | ((uint32_t)b.g << 8) | b.b;
}

/* ------------------------------------------------------------------ */
/* Parsing de valores de cor                                           */
/* ------------------------------------------------------------------ */
static TColor color_default(void)
{
    TColor c;
    c.kind = CHEAP_DEFAULT;
    c.rgb.r = c.rgb.g = c.rgb.b = 0;
    c.index = -1;
    return c;
}

static TColor color_rgb(Rgb rgb);

static TColor color_of_rgb8(Rgb8 c)
{
    return color_rgb(rgb_from_rgb8(c));
}

static TColor color_rgb(Rgb rgb)
{
    TColor c;
    c.kind = CHEAP_RGB;
    c.rgb = rgb;
    c.index = -1;
    return c;
}

static int parse_fn_args(const char *s, double out[3], int pct[3])
{
    /* s aponta para dentro de "okhsl(...)" */
    const char *p = s;
    for (int i = 0; i < 3; i++) {
        while (*p == ' ' || *p == ',' || *p == '\t' || *p == '/' || *p == '%') p++;
        char *endp = NULL;
        out[i] = strtod(p, &endp);
        if (endp == p) return -1;
        p = endp;
        pct[i] = (*p == '%');
    }
    return 0;
}

static TColor color_parse(const char *raw, JValue *vars, int depth)
{
    TColor out = color_default();
    if (!raw) return out;

    /* "" => cor padrao do terminal */
    if (!*raw) return out;

    if (raw[0] == '#') return color_of_rgb8(hex_rgb8(raw));

    if (str_istarts(raw, "okhsl(") || str_istarts(raw, "oklch(")) {
        const char *arg = strchr(raw, '(');
        double v[3] = { 0, 0, 0 };
        int pct[3] = { 0, 0, 0 };
        if (parse_fn_args(arg + 1, v, pct) != 0) return out;
        if (str_istarts(raw, "okhsl(")) {
            double h = v[0];
            double s = pct[1] ? v[1] / 100.0 : v[1];
            double l = pct[2] ? v[2] / 100.0 : v[2];
            return color_of_rgb8(okhsl_rgb8(h, s, l));
        } else {
            double l = pct[0] ? v[0] / 100.0 : v[0];
            double c = pct[1] ? v[1] / 100.0 : v[1];
            return color_of_rgb8(oklch_rgb8(l, c, v[2]));
        }
    }

    if (str_istarts(raw, "rgb(")) {
        const char *arg = strchr(raw, '(');
        double v[3] = { 0, 0, 0 };
        int pct[3] = { 0, 0, 0 };
        if (parse_fn_args(arg + 1, v, pct) != 0) return out;
        Rgb8 c = { (uint8_t)(v[0] < 0 ? 0 : (v[0] > 255 ? 255 : v[0])),
                   (uint8_t)(v[1] < 0 ? 0 : (v[1] > 255 ? 255 : v[1])),
                   (uint8_t)(v[2] < 0 ? 0 : (v[2] > 255 ? 255 : v[2])) };
        return color_of_rgb8(c);
    }

    /* Referencia a var */
    if (depth < 16 && vars && vars->kind == J_OBJ) {
        JValue *ref = json_get(vars, raw);
        if (ref && ref->kind == J_STR) return color_parse(ref->str, vars, depth + 1);
        if (ref && ref->kind == J_NUM) {
            TColor c = color_default();
            c.kind = CHEAP_INDEXED;
            c.index = (int)ref->num;
            return c;
        }
    }

    /* Numero puro (cor indexada 0..255) */
    {
        char *endp = NULL;
        long n = strtol(raw, &endp, 10);
        if (endp && *endp == 0 && n >= 0 && n <= 255) {
            TColor c = color_default();
            c.kind = CHEAP_INDEXED;
            c.index = (int)n;
            return c;
        }
    }

    return out;
}

/* ------------------------------------------------------------------ */
/* Load                                                                */
/* ------------------------------------------------------------------ */
static void apply_fallbacks(Theme *t)
{
    for (size_t i = 0; i < sizeof(TOKEN_FALLBACKS) / sizeof(TOKEN_FALLBACKS[0]); i++) {
        Token tk = TOKEN_FALLBACKS[i].tk;
        Token fb = TOKEN_FALLBACKS[i].fb;
        if (t->tok[tk].kind == CHEAP_DEFAULT) t->tok[tk] = t->tok[fb];
    }
    /* selectedBg sem valor nao pode ficar default: cai no accent. */
    if (t->tok[TK_SELECTED_BG].kind == CHEAP_DEFAULT) t->tok[TK_SELECTED_BG] = t->tok[TK_ACCENT];
}

int theme_load(Theme *t, const char *path)
{
    memset(t, 0, sizeof(*t));
    t->dark = 1;

    if (!path) { theme_load_builtin_dark(t); return 1; }

    size_t len = 0;
    char *text = file_read(path, &len);
    if (!text) return 0;

    JValue *root = json_parse(text, len);
    free(text);
    if (!root) return 0;

    JValue *name = json_get(root, "name");
    if (name && name->kind == J_STR) t->name = str_dup(name->str);
    JValue *app = json_get(root, "appearance");
    if (app && app->kind == J_STR) t->dark = str_eq(app->str, "dark");

    JValue *vars = json_get(root, "vars");
    JValue *colors = json_get(root, "colors");

    /* Comeca do tema dark embutido para nunca faltar um token. */
    Theme base;
    theme_load_builtin_dark(&base);
    for (int i = 0; i < TK_COUNT; i++)
        if (base.tok[i].kind != CHEAP_DEFAULT) t->tok[i] = base.tok[i];
    theme_free(&base);

    if (colors && colors->kind == J_OBJ) {
        for (size_t i = 0; i < colors->count; i++) {
            Token tk = token_from_name(colors->pairs[i].key);
            if (tk == TK_COUNT) continue;
            JValue *v = colors->pairs[i].val;
            if (v->kind == J_STR) {
                t->tok[tk] = color_parse(v->str, vars, 0);
            } else if (v->kind == J_NUM) {
                TColor c = color_default();
                c.kind = CHEAP_INDEXED;
                c.index = (int)v->num;
                t->tok[tk] = c;
            }
        }
    }

    /* "text": "" => mantem a cor padrao do terminal mesmo se o base tinha outra. */
    if (colors && colors->kind == J_OBJ) {
        JValue *tv = json_get(colors, "text");
        if (tv && tv->kind == J_STR && tv->str[0] == 0) t->tok[TK_TEXT] = color_default();
    }

    apply_fallbacks(t);
    json_free(root);
    return 1;
}

void theme_free(Theme *t)
{
    if (!t) return;
    free(t->name);
    t->name = NULL;
}

void theme_load_builtin_dark(Theme *t)
{
    memset(t, 0, sizeof(*t));
    t->dark = 1;
    t->name = str_dup("dark");

    for (int i = 0; i < TK_COUNT; i++) t->tok[i] = color_default();

    /* Cores exatamente como em dark.json (convertidas de okhsl). */
    t->tok[TK_TEXT]              = color_of_rgb8(okhsl_rgb8(234.0, 0.03, 0.89));
    t->tok[TK_MUTED]             = color_of_rgb8(okhsl_rgb8(229.0, 0.06, 0.67));
    t->tok[TK_ACCENT]            = color_of_rgb8(okhsl_rgb8(295.0, 0.50, 0.67));
    t->tok[TK_BORDER]            = color_of_rgb8(okhsl_rgb8(231.0, 0.57, 0.65));
    t->tok[TK_BORDER_ACCENT]     = color_of_rgb8(okhsl_rgb8(295.0, 0.53, 0.64));
    t->tok[TK_BORDER_MUTED]      = color_of_rgb8(okhsl_rgb8(229.0, 0.08, 0.53));
    t->tok[TK_SUCCESS]           = color_of_rgb8(okhsl_rgb8(159.0, 0.59, 0.67));
    t->tok[TK_ERROR]             = color_of_rgb8(okhsl_rgb8(20.0, 0.72, 0.67));
    t->tok[TK_WARNING]           = color_of_rgb8(okhsl_rgb8(83.0, 0.88, 0.67));
    t->tok[TK_DIM]               = color_of_rgb8(okhsl_rgb8(229.0, 0.08, 0.56));
    t->tok[TK_THINKING_TEXT]     = color_of_rgb8(okhsl_rgb8(226.0, 0.07, 0.65));
    t->tok[TK_SELECTED_BG]       = color_of_rgb8(okhsl_rgb8(233.0, 0.41, 0.24));
    t->tok[TK_SCROLLBAR_TRACK]   = color_of_rgb8(okhsl_rgb8(237.0, 0.07, 0.33));
    t->tok[TK_SCROLLBAR_THUMB]   = color_of_rgb8(okhsl_rgb8(232.0, 0.07, 0.65));
    t->tok[TK_SEARCH_MATCH_BG]   = color_of_rgb8(okhsl_rgb8(53.0, 0.51, 0.24));
    t->tok[TK_SEARCH_MATCH_TEXT] = t->tok[TK_MUTED];
    t->tok[TK_USER_MESSAGE_BG]   = color_of_rgb8(okhsl_rgb8(233.0, 0.41, 0.24));
    t->tok[TK_USER_MESSAGE_TEXT] = t->tok[TK_TEXT];
    t->tok[TK_CUSTOM_MESSAGE_BG] = color_of_rgb8(okhsl_rgb8(295.0, 0.42, 0.24));
    t->tok[TK_CUSTOM_MESSAGE_TEXT] = t->tok[TK_MUTED];
    t->tok[TK_CUSTOM_MESSAGE_LABEL] = t->tok[TK_ACCENT];
    t->tok[TK_TOOL_PENDING_BG]   = color_of_rgb8(okhsl_rgb8(229.0, 0.05, 0.24));
    t->tok[TK_TOOL_SUCCESS_BG]   = color_of_rgb8(okhsl_rgb8(158.0, 0.46, 0.25));
    t->tok[TK_TOOL_ERROR_BG]     = color_of_rgb8(okhsl_rgb8(19.0, 0.54, 0.25));
    t->tok[TK_TOOL_TITLE]        = t->tok[TK_TEXT];
    t->tok[TK_TOOL_OUTPUT]       = t->tok[TK_MUTED];
    t->tok[TK_MD_HEADING]        = color_of_rgb8(okhsl_rgb8(83.0, 0.88, 0.67));
    t->tok[TK_MD_LINK]           = color_of_rgb8(okhsl_rgb8(232.0, 0.54, 0.67));
    t->tok[TK_MD_LINK_URL]       = t->tok[TK_MUTED];
    t->tok[TK_MD_CODE]           = t->tok[TK_ACCENT];
    t->tok[TK_MD_CODE_BLOCK]     = t->tok[TK_SUCCESS];
    t->tok[TK_MD_CODE_BLOCK_BORDER] = t->tok[TK_MUTED];
    t->tok[TK_MD_QUOTE]          = t->tok[TK_MUTED];
    t->tok[TK_MD_QUOTE_BORDER]   = t->tok[TK_MUTED];
    t->tok[TK_MD_HR]             = t->tok[TK_MUTED];
    t->tok[TK_MD_LIST_BULLET]    = t->tok[TK_ACCENT];
    t->tok[TK_TOOL_DIFF_ADDED]   = t->tok[TK_SUCCESS];
    t->tok[TK_TOOL_DIFF_REMOVED] = t->tok[TK_ERROR];
    t->tok[TK_TOOL_DIFF_CONTEXT] = t->tok[TK_MUTED];
    t->tok[TK_SYNTAX_COMMENT]    = t->tok[TK_MUTED];
    t->tok[TK_SYNTAX_KEYWORD]    = color_of_rgb8(okhsl_rgb8(232.0, 0.54, 0.67));
    t->tok[TK_SYNTAX_FUNCTION]   = color_of_rgb8(okhsl_rgb8(83.0, 0.88, 0.67));
    t->tok[TK_SYNTAX_VARIABLE]   = color_of_rgb8(okhsl_rgb8(202.0, 0.58, 0.67));
    t->tok[TK_SYNTAX_STRING]     = color_of_rgb8(okhsl_rgb8(52.0, 0.67, 0.67));
    t->tok[TK_SYNTAX_NUMBER]     = t->tok[TK_SUCCESS];
    t->tok[TK_SYNTAX_TYPE]       = t->tok[TK_ACCENT];
    t->tok[TK_SYNTAX_OPERATOR]   = t->tok[TK_MUTED];
    t->tok[TK_SYNTAX_PUNCTUATION] = t->tok[TK_MUTED];
    t->tok[TK_THINKING_OFF]      = color_of_rgb8(okhsl_rgb8(229.0, 0.08, 0.49));
    t->tok[TK_THINKING_MINIMAL]  = color_of_rgb8(okhsl_rgb8(232.0, 0.20, 0.52));
    t->tok[TK_THINKING_LOW]      = color_of_rgb8(okhsl_rgb8(232.0, 0.45, 0.54));
    t->tok[TK_THINKING_MEDIUM]   = color_of_rgb8(okhsl_rgb8(263.0, 0.59, 0.56));
    t->tok[TK_THINKING_HIGH]     = color_of_rgb8(okhsl_rgb8(295.0, 0.73, 0.59));
    t->tok[TK_THINKING_XHIGH]    = color_of_rgb8(okhsl_rgb8(337.0, 0.81, 0.61));
    t->tok[TK_THINKING_MAX]      = color_of_rgb8(okhsl_rgb8(20.0, 0.99, 0.63));
    t->tok[TK_BASH_MODE]         = color_of_rgb8(okhsl_rgb8(159.0, 0.64, 0.65));
}

/* ------------------------------------------------------------------ */
/* Tema embutido "mono": so branco e tons de cinza                     */
/* ------------------------------------------------------------------ */
static void set_hex(Theme *t, Token tk, const char *hex)
{
    t->tok[tk] = color_of_rgb8(hex_rgb8(hex));
}

void theme_load_builtin_mono(Theme *t)
{
    memset(t, 0, sizeof(*t));
    t->dark = 1;
    t->mono = 1;
    t->name = str_dup("mono");

    for (int i = 0; i < TK_COUNT; i++) t->tok[i] = color_default();

    /* frente */
    set_hex(t, TK_TEXT,          "#e8e8e8");
    set_hex(t, TK_MUTED,         "#a8a8a8");
    set_hex(t, TK_DIM,           "#7c7c7c");
    set_hex(t, TK_ACCENT,        "#ffffff");
    set_hex(t, TK_BORDER,        "#4a4a4a");
    set_hex(t, TK_BORDER_ACCENT, "#c8c8c8");
    set_hex(t, TK_BORDER_MUTED,  "#3a3a3a");
    set_hex(t, TK_SUCCESS,       "#d8d8d8");
    set_hex(t, TK_ERROR,         "#ffffff");
    set_hex(t, TK_WARNING,       "#c0c0c0");
    set_hex(t, TK_THINKING_TEXT, "#949494");
    set_hex(t, TK_SCROLLBAR_TRACK, "#3a3a3a");
    set_hex(t, TK_SCROLLBAR_THUMB, "#c8c8c8");
    set_hex(t, TK_SEARCH_MATCH_TEXT, "#e8e8e8");
    set_hex(t, TK_USER_MESSAGE_TEXT, "#e8e8e8");
    set_hex(t, TK_CUSTOM_MESSAGE_TEXT, "#a8a8a8");
    set_hex(t, TK_CUSTOM_MESSAGE_LABEL, "#d8d8d8");
    set_hex(t, TK_TOOL_TITLE,    "#e8e8e8");
    set_hex(t, TK_TOOL_OUTPUT,   "#a8a8a8");
    set_hex(t, TK_MD_HEADING,    "#ffffff");
    set_hex(t, TK_MD_LINK,       "#d0d0d0");
    set_hex(t, TK_MD_LINK_URL,   "#7c7c7c");
    set_hex(t, TK_MD_CODE,       "#ffffff");
    set_hex(t, TK_MD_CODE_BLOCK, "#d0d0d0");
    set_hex(t, TK_MD_CODE_BLOCK_BORDER, "#7c7c7c");
    set_hex(t, TK_MD_QUOTE,      "#a8a8a8");
    set_hex(t, TK_MD_QUOTE_BORDER, "#565656");
    set_hex(t, TK_MD_HR,         "#565656");
    set_hex(t, TK_MD_LIST_BULLET, "#ffffff");
    set_hex(t, TK_TOOL_DIFF_ADDED,   "#ffffff");
    set_hex(t, TK_TOOL_DIFF_REMOVED, "#8c8c8c");
    set_hex(t, TK_TOOL_DIFF_CONTEXT, "#7c7c7c");
    set_hex(t, TK_SYNTAX_COMMENT,   "#7c7c7c");
    set_hex(t, TK_SYNTAX_KEYWORD,   "#d0d0d0");
    set_hex(t, TK_SYNTAX_FUNCTION,  "#ffffff");
    set_hex(t, TK_SYNTAX_VARIABLE,  "#c0c0c0");
    set_hex(t, TK_SYNTAX_STRING,    "#b8b8b8");
    set_hex(t, TK_SYNTAX_NUMBER,    "#d8d8d8");
    set_hex(t, TK_SYNTAX_TYPE,      "#e0e0e0");
    set_hex(t, TK_SYNTAX_OPERATOR,  "#909090");
    set_hex(t, TK_SYNTAX_PUNCTUATION, "#909090");
    /* niveis de thinking: cinza do escuro ao branco */
    set_hex(t, TK_THINKING_OFF,     "#565656");
    set_hex(t, TK_THINKING_MINIMAL, "#6e6e6e");
    set_hex(t, TK_THINKING_LOW,     "#8a8a8a");
    set_hex(t, TK_THINKING_MEDIUM,  "#a8a8a8");
    set_hex(t, TK_THINKING_HIGH,    "#c8c8c8");
    set_hex(t, TK_THINKING_XHIGH,   "#e0e0e0");
    set_hex(t, TK_THINKING_MAX,     "#ffffff");
    set_hex(t, TK_BASH_MODE,        "#c8c8c8");

    /* fundo: cinzas opacos */
    set_hex(t, TK_SELECTED_BG,     "#333333");
    set_hex(t, TK_SEARCH_MATCH_BG, "#3f3f3f");
    set_hex(t, TK_USER_MESSAGE_BG, "#262626");
    set_hex(t, TK_CUSTOM_MESSAGE_BG, "#262626");
    set_hex(t, TK_TOOL_PENDING_BG, "#232323");
    set_hex(t, TK_TOOL_SUCCESS_BG, "#2b2b2b");
    set_hex(t, TK_TOOL_ERROR_BG,  "#2f2f2f");

    apply_fallbacks(t);
}

/* ------------------------------------------------------------------ */
/* Descoberta de temas                                                 */
/* ------------------------------------------------------------------ */
char *theme_find_path(const char *name)
{
    if (!name || !*name) return NULL;

    /* caminho direto */
    if (strchr(name, '/') || strchr(name, '\\') || str_ends(name, ".json")) {
        if (file_exists(name)) return str_dup(name);
    }

    char *home = home_get();
    char *cands[7];
    char file[256];
    snprintf(file, sizeof file, "%s.json", name);
    cands[0] = path_join(path_join(home, ".pi/agent/themes"), file);
    cands[1] = path_join(path_join(home, ".pi/themes"), file);
    {
        char *env = env_get("DELTA_THEMES_DIR");
        cands[2] = env ? path_join(env, file) : NULL;
        free(env);
    }
    cands[3] = path_join(path_join(delta_home(), "themes"), file);
    cands[4] = path_join("themes", name && *name ? name : "dark");   /* ./themes/<nome>.json */
    cands[5] = str_dup(file);                                          /* ./<nome>.json */
    cands[6] = NULL;

    char *found = NULL;
    for (int i = 0; i < 6 && !found; i++) {
        if (cands[i] && file_exists(cands[i])) found = str_dup(cands[i]);
    }
    for (int i = 0; i < 6; i++) free(cands[i]);
    free(home);
    return found;
}
