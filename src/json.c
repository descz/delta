/* json.c - parser JSON minimalista, sem dependencias */
#include "json.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    const char *p;
    const char *end;
    const char *err;
} P;

static JValue *parse_value(P *p);

static JValue *jnew(JKind k)
{
    JValue *v = (JValue *)calloc(1, sizeof(JValue));
    v->kind = k;
    return v;
}

static void skip_ws(P *p)
{
    while (p->p < p->end) {
        char c = *p->p;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') p->p++;
        else break;
    }
}

static JValue *parse_string_raw(P *p, char **out)
{
    if (p->p >= p->end || *p->p != '"') { p->err = "expected string"; return NULL; }
    p->p++;
    Str s; str_init(&s);
    while (p->p < p->end) {
        unsigned char c = (unsigned char)*p->p;
        if (c == '"') { p->p++; *out = str_steal(&s); return (JValue *)1; }
        if (c == '\\') {
            p->p++;
            if (p->p >= p->end) break;
            char e = *p->p++;
            switch (e) {
                case '"':  str_addc(&s, '"'); break;
                case '\\': str_addc(&s, '\\'); break;
                case '/':  str_addc(&s, '/'); break;
                case 'b':  str_addc(&s, '\b'); break;
                case 'f':  str_addc(&s, '\f'); break;
                case 'n':  str_addc(&s, '\n'); break;
                case 'r':  str_addc(&s, '\r'); break;
                case 't':  str_addc(&s, '\t'); break;
                case 'u': {
                    if (p->end - p->p < 4) { p->err = "bad \\u"; str_free(&s); return NULL; }
                    char hex[5] = { p->p[0], p->p[1], p->p[2], p->p[3], 0 };
                    p->p += 4;
                    uint32_t cp = (uint32_t)strtoul(hex, NULL, 16);
                    if (cp >= 0xD800 && cp <= 0xDBFF && p->end - p->p >= 6 &&
                        p->p[0] == '\\' && p->p[1] == 'u') {
                        char hex2[5] = { p->p[2], p->p[3], p->p[4], p->p[5], 0 };
                        uint32_t lo = (uint32_t)strtoul(hex2, NULL, 16);
                        if (lo >= 0xDC00 && lo <= 0xDFFF) {
                            p->p += 6;
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                    }
                    str_addcp(&s, cp);
                    break;
                }
                default: str_addc(&s, e); break;
            }
        } else {
            str_addc(&s, (char)c);
            p->p++;
        }
    }
    str_free(&s);
    p->err = "unterminated string";
    return NULL;
}

static JValue *parse_value(P *p)
{
    skip_ws(p);
    if (p->p >= p->end) { p->err = "unexpected end"; return NULL; }
    char c = *p->p;
    if (c == '{') {
        p->p++;
        JValue *v = jnew(J_OBJ);
        size_t cap = 0;
        skip_ws(p);
        if (p->p < p->end && *p->p == '}') { p->p++; return v; }
        for (;;) {
            skip_ws(p);
            char *key = NULL;
            if (!parse_string_raw(p, &key)) { json_free(v); return NULL; }
            skip_ws(p);
            if (p->p >= p->end || *p->p != ':') { p->err = "expected :"; free(key); json_free(v); return NULL; }
            p->p++;
            JValue *val = parse_value(p);
            if (!val) { free(key); json_free(v); return NULL; }
            if (v->count + 1 > cap) {
                cap = cap ? cap * 2 : 8;
                v->pairs = (JPair *)realloc(v->pairs, cap * sizeof(JPair));
            }
            v->pairs[v->count].key = key;
            v->pairs[v->count].val = val;
            v->count++;
            skip_ws(p);
            if (p->p < p->end && *p->p == ',') { p->p++; continue; }
            if (p->p < p->end && *p->p == '}') { p->p++; return v; }
            p->err = "expected , or }";
            json_free(v);
            return NULL;
        }
    }
    if (c == '[') {
        p->p++;
        JValue *v = jnew(J_ARR);
        size_t cap = 0;
        skip_ws(p);
        if (p->p < p->end && *p->p == ']') { p->p++; return v; }
        for (;;) {
            JValue *item = parse_value(p);
            if (!item) { json_free(v); return NULL; }
            if (v->count + 1 > cap) {
                cap = cap ? cap * 2 : 8;
                v->items = (JValue **)realloc(v->items, cap * sizeof(JValue *));
            }
            v->items[v->count++] = item;
            skip_ws(p);
            if (p->p < p->end && *p->p == ',') { p->p++; continue; }
            if (p->p < p->end && *p->p == ']') { p->p++; return v; }
            p->err = "expected , or ]";
            json_free(v);
            return NULL;
        }
    }
    if (c == '"') {
        JValue *v = jnew(J_STR);
        if (!parse_string_raw(p, &v->str)) { json_free(v); return NULL; }
        return v;
    }
    if (c == 't' && p->end - p->p >= 4 && memcmp(p->p, "true", 4) == 0) {
        p->p += 4; JValue *v = jnew(J_BOOL); v->boolean = 1; return v;
    }
    if (c == 'f' && p->end - p->p >= 5 && memcmp(p->p, "false", 5) == 0) {
        p->p += 5; return jnew(J_BOOL);
    }
    if (c == 'n' && p->end - p->p >= 4 && memcmp(p->p, "null", 4) == 0) {
        p->p += 4; return jnew(J_NULL);
    }
    /* numero */
    {
        const char *start = p->p;
        if (p->p < p->end && (*p->p == '-' || *p->p == '+')) p->p++;
        while (p->p < p->end) {
            char d = *p->p;
            if ((d >= '0' && d <= '9') || d == '.' || d == 'e' || d == 'E' || d == '+' || d == '-') p->p++;
            else break;
        }
        if (p->p == start) { p->err = "invalid value"; return NULL; }
        char buf[64];
        size_t n = (size_t)(p->p - start);
        if (n >= sizeof buf) n = sizeof buf - 1;
        memcpy(buf, start, n);
        buf[n] = 0;
        JValue *v = jnew(J_NUM);
        v->num = strtod(buf, NULL);
        return v;
    }
}

static const char *g_err = "";

JValue *json_parse(const char *text, size_t len)
{
    P p = { text, text + len, NULL };
    JValue *v = parse_value(&p);
    if (!v) { g_err = p.err ? p.err : "parse error"; return NULL; }
    skip_ws(&p);
    if (p.p != p.end) { json_free(v); g_err = "trailing content"; return NULL; }
    g_err = "";
    return v;
}

JValue *json_parse_cstr(const char *text) { return json_parse(text, strlen(text)); }
const char *json_error(void) { return g_err; }

void json_free(JValue *v)
{
    if (!v) return;
    switch (v->kind) {
        case J_STR: free(v->str); break;
        case J_ARR:
            for (size_t i = 0; i < v->count; i++) json_free(v->items[i]);
            free(v->items);
            break;
        case J_OBJ:
            for (size_t i = 0; i < v->count; i++) {
                free(v->pairs[i].key);
                json_free(v->pairs[i].val);
            }
            free(v->pairs);
            break;
        default: break;
    }
    free(v);
}

JValue *json_get(JValue *obj, const char *key)
{
    if (!obj || obj->kind != J_OBJ || !key) return NULL;
    for (size_t i = 0; i < obj->count; i++)
        if (str_eq(obj->pairs[i].key, key)) return obj->pairs[i].val;
    return NULL;
}

JValue *json_at(JValue *arr, size_t i)
{
    if (!arr || arr->kind != J_ARR || i >= arr->count) return NULL;
    return arr->items[i];
}

const char *json_str(JValue *v) { return (v && v->kind == J_STR && v->str) ? v->str : ""; }
double json_num(JValue *v, double def) { return (v && v->kind == J_NUM) ? v->num : def; }
int json_bool(JValue *v, int def) { return (v && v->kind == J_BOOL) ? v->boolean : def; }
size_t json_count(JValue *v) { return v ? v->count : 0; }

void json_write_string(Str *out, const char *s, size_t n)
{
    str_addc(out, '"');
    if (s) json_escape(out, s, n);
    str_addc(out, '"');
}
