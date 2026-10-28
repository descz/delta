/* modelsdev.c - leitor streaming do models.dev/api.json + cache local */
#include "modelsdev.h"
#include "provider.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

/* ================================================================== */
/* Leitor JSON em streaming (sem DOM, sem alocacao para o que e ignorado) */
/* ================================================================== */
typedef struct {
    const char *p;
    const char *end;
    int         err;
} R;

static void skip_ws(R *r)
{
    while (r->p < r->end) {
        char c = *r->p;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') r->p++;
        else break;
    }
}

static int peek(R *r)
{
    skip_ws(r);
    return (r->p < r->end) ? (unsigned char)*r->p : -1;
}

static int expect(R *r, char c)
{
    if (peek(r) != (unsigned char)c) { r->err = 1; return 0; }
    r->p++;
    return 1;
}

/* Le uma string JSON; se out != NULL guarda o UTF-8 decodificado. */
static int read_string(R *r, Str *out)
{
    if (peek(r) != '"') { r->err = 1; return 0; }
    r->p++;
    while (r->p < r->end) {
        unsigned char c = (unsigned char)*r->p;
        if (c == '"') { r->p++; if (out) str_addc(out, 0), out->len--; return 1; }
        if (c == '\\') {
            r->p++;
            if (r->p >= r->end) break;
            char e = *r->p++;
            char ch = 0;
            switch (e) {
                case '"': ch = '"'; break;
                case '\\': ch = '\\'; break;
                case '/': ch = '/'; break;
                case 'b': ch = '\b'; break;
                case 'f': ch = '\f'; break;
                case 'n': ch = '\n'; break;
                case 'r': ch = '\r'; break;
                case 't': ch = '\t'; break;
                case 'u': {
                    if (r->end - r->p < 4) { r->err = 1; return 0; }
                    char hex[5] = { r->p[0], r->p[1], r->p[2], r->p[3], 0 };
                    r->p += 4;
                    uint32_t cp = (uint32_t)strtoul(hex, NULL, 16);
                    if (cp >= 0xD800 && cp <= 0xDBFF && r->end - r->p >= 6 &&
                        r->p[0] == '\\' && r->p[1] == 'u') {
                        char hex2[5] = { r->p[2], r->p[3], r->p[4], r->p[5], 0 };
                        uint32_t lo = (uint32_t)strtoul(hex2, NULL, 16);
                        if (lo >= 0xDC00 && lo <= 0xDFFF) {
                            r->p += 6;
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                    }
                    if (out) str_addcp(out, cp);
                    continue;
                }
                default: ch = e; break;
            }
            if (out) str_addc(out, ch);
            continue;
        }
        if (out) str_addc(out, (char)c);
        r->p++;
    }
    r->err = 1;
    return 0;
}

static double read_number(R *r)
{
    skip_ws(r);
    char *endp = NULL;
    double v = strtod(r->p, &endp);
    if (!endp || endp == r->p) { r->err = 1; return 0; }
    r->p = endp;
    return v;
}

static int read_bool(R *r)
{
    skip_ws(r);
    if (r->end - r->p >= 4 && memcmp(r->p, "true", 4) == 0) { r->p += 4; return 1; }
    if (r->end - r->p >= 5 && memcmp(r->p, "false", 5) == 0) { r->p += 5; return 0; }
    r->err = 1;
    return 0;
}

static void skip_value(R *r);

/* Le uma string se for string; em null/outro valor, apenas pula (retorna 0). */
static int read_string_opt(R *r, Str *out)
{
    if (peek(r) == '"') return read_string(r, out);
    skip_value(r);
    return 0;
}

/* Pula qualquer valor (sem alocar). */
static void skip_value(R *r)
{
    int kind = peek(r);
    if (kind < 0) { r->err = 1; return; }
    if (kind == '"') { read_string(r, NULL); return; }
    if (kind == '{' || kind == '[') {
        char close = (kind == '{') ? '}' : ']';
        r->p++;
        if (peek(r) == (unsigned char)close) { r->p++; return; }
        for (;;) {
            if (kind == '{') {
                if (!read_string(r, NULL)) return;
                if (!expect(r, ':')) return;
            }
            skip_value(r);
            int n = peek(r);
            if (n == ',') { r->p++; continue; }
            if (n == (unsigned char)close) { r->p++; return; }
            r->err = 1;
            return;
        }
    }
    if (r->end - r->p >= 4 && memcmp(r->p, "true", 4) == 0) { r->p += 4; return; }
    if (r->end - r->p >= 5 && memcmp(r->p, "false", 5) == 0) { r->p += 5; return; }
    if (r->end - r->p >= 4 && memcmp(r->p, "null", 4) == 0) { r->p += 4; return; }
    read_number(r);
}

/* ================================================================== */
/* Mapeamento do JSON -> Catalog                                       */
/* ================================================================== */
static void model_free(MdModel *m)
{
    free(m->id);
    free(m->name);
    free(m->canonical);
    memset(m, 0, sizeof(*m));
}

static void provider_free(MdProvider *p)
{
    free(p->id);
    free(p->name);
    free(p->api);
    free(p->env);
    for (size_t i = 0; i < p->n_models; i++) model_free(&p->models[i]);
    free(p->models);
    memset(p, 0, sizeof(*p));
}

/* Nome do nivel -> bit. */
static unsigned level_bit(const char *name)
{
    if (!name) return 0;
    /* os nomes vem do models.dev: none, minimal, low, medium, high, xhigh, max, default */
    if (str_eq(name, "off") || str_eq(name, "none") || str_eq(name, "disabled")) return THINK_OFF;
    if (str_eq(name, "minimal") || str_eq(name, "min"))  return THINK_MINIMAL;
    if (str_eq(name, "low"))                             return THINK_LOW;
    if (str_eq(name, "medium") || str_eq(name, "mid") || str_eq(name, "default")) return THINK_MEDIUM;
    if (str_eq(name, "high"))                            return THINK_HIGH;
    if (str_eq(name, "xhigh") || str_eq(name, "very-high")) return THINK_XHIGH;
    if (str_eq(name, "max") || str_eq(name, "maximum"))  return THINK_MAX;
    return 0;
}

/* "reasoning_options": [{type, values?|min?/max?}] */
static void parse_reasoning_options(R *r, MdModel *m)
{
    if (!expect(r, '[')) return;
    if (peek(r) == ']') { r->p++; return; }

    for (;;) {
        if (!expect(r, '{')) return;
        char tipo[32] = "";
        unsigned mask = 0;
        int bmin = -1, bmax = -1;

        if (peek(r) == '}') { r->p++; }
        else for (;;) {
            Str k; str_init(&k);
            if (!read_string(r, &k) || !expect(r, ':')) { str_free(&k); return; }

            if (str_eq(k.data, "type")) {
                Str v; str_init(&v);
                if (read_string(r, &v) && v.data)
                    snprintf(tipo, sizeof tipo, "%s", v.data);
                str_free(&v);
            } else if (str_eq(k.data, "values")) {
                if (!expect(r, '[')) { str_free(&k); return; }
                if (peek(r) == ']') { r->p++; }
                else for (;;) {
                    Str v; str_init(&v);
                    read_string_opt(r, &v);
                    mask |= level_bit(v.data);
                    str_free(&v);
                    int n = peek(r);
                    if (n == ',') { r->p++; continue; }
                    if (n == ']') { r->p++; break; }
                    str_free(&k);
                    return;
                }
            } else if (str_eq(k.data, "min")) {
                bmin = (int)read_number(r);
            } else if (str_eq(k.data, "max")) {
                bmax = (int)read_number(r);
            } else {
                skip_value(r);
            }

            str_free(&k);
            int n = peek(r);
            if (n == ',') { r->p++; continue; }
            if (n == '}') { r->p++; break; }
            return;
        }

        if (str_eq(tipo, "effort")) {
            m->think_kind = 1;
            m->think_mask = mask | THINK_OFF;
        } else if (str_eq(tipo, "toggle")) {
            m->think_kind = 2;
            m->think_mask = THINK_OFF | THINK_MINIMAL;
        } else if (str_eq(tipo, "budget_tokens")) {
            m->think_kind = 3;
            m->think_mask = THINK_ALL;
            m->budget_min = bmin;
            m->budget_max = bmax;
        }

        int n = peek(r);
        if (n == ',') { r->p++; continue; }
        if (n == ']') { r->p++; return; }
        return;
    }
}

/* "models": { "<id>": { ...campos... }, ... } */
static void parse_model(R *r, MdProvider *prov)
{
    MdModel m;
    memset(&m, 0, sizeof m);

    if (!expect(r, '{')) return;
    if (peek(r) == '}') { r->p++; goto done; }
    for (;;) {
        Str key; str_init(&key);
        if (!read_string(r, &key) || !expect(r, ':')) { str_free(&key); return; }

        if (str_eq(key.data, "id")) {
            Str v; str_init(&v);
            if (read_string(r, &v)) { free(m.id); m.id = v.data; } else str_free(&v);
        } else if (str_eq(key.data, "name")) {
            Str v; str_init(&v);
            if (read_string(r, &v)) { free(m.name); m.name = v.data; } else str_free(&v);
        } else if (str_eq(key.data, "reasoning")) {
            m.reasoning = read_bool(r);
        } else if (str_eq(key.data, "tool_call")) {
            m.tool_call = read_bool(r);
        } else if (str_eq(key.data, "canonical_model_id")) {
            Str v; str_init(&v);
            if (read_string(r, &v)) {
                if (v.data) { free(m.canonical); m.canonical = v.data; } else str_free(&v);
            } else str_free(&v);
        } else if (str_eq(key.data, "reasoning_options")) {
            parse_reasoning_options(r, &m);
        } else if (str_eq(key.data, "modalities")) {
            /* {"input":["text","image","pdf"],"output":["text"]} */
            if (expect(r, '{')) {
                if (peek(r) == '}') { r->p++; }
                else for (;;) {
                    Str k2; str_init(&k2);
                    if (!read_string(r, &k2) || !expect(r, ':')) { str_free(&k2); str_free(&key); return; }
                    if (str_eq(k2.data, "input")) {
                        str_free(&k2);
                        if (!expect(r, '[')) { str_free(&key); return; }
                        if (peek(r) == ']') { r->p++; }
                        else for (;;) {
                            Str v; str_init(&v);
                            read_string_opt(r, &v);
                            if (v.data && str_eq(v.data, "image")) m.image_in = 1;
                            str_free(&v);
                            int n2 = peek(r);
                            if (n2 == ',') { r->p++; continue; }
                            if (n2 == ']') { r->p++; break; }
                            str_free(&key);
                            return;
                        }
                        goto proximo_campo;
                    } else {
                        skip_value(r);
                    }
                    str_free(&k2);
                proximo_campo:
                    {
                        int n2 = peek(r);
                        if (n2 == ',') { r->p++; continue; }
                        if (n2 == '}') { r->p++; break; }
                        str_free(&key);
                        return;
                    }
                }
            }
        } else if (str_eq(key.data, "limit") || str_eq(key.data, "cost")) {
            int is_limit = str_eq(key.data, "limit");
            if (!expect(r, '{')) { str_free(&key); return; }
            if (peek(r) == '}') { r->p++; }
            else for (;;) {
                Str k2; str_init(&k2);
                if (!read_string(r, &k2) || !expect(r, ':')) { str_free(&k2); str_free(&key); return; }
                if (is_limit && str_eq(k2.data, "context")) m.ctx = (int)read_number(r);
                else if (is_limit && str_eq(k2.data, "output")) m.out = (int)read_number(r);
                else if (!is_limit && str_eq(k2.data, "input")) m.cost_in = read_number(r);
                else if (!is_limit && str_eq(k2.data, "output")) m.cost_out = read_number(r);
                else if (!is_limit && str_eq(k2.data, "cache_read")) m.cost_cache_read = read_number(r);
                else if (!is_limit && str_eq(k2.data, "cache_write")) m.cost_cache_write = read_number(r);
                else skip_value(r);
                str_free(&k2);
                int n = peek(r);
                if (n == ',') { r->p++; continue; }
                if (n == '}') { r->p++; break; }
                r->err = 1;
                str_free(&key);
                return;
            }
        } else {
            skip_value(r);
        }
        str_free(&key);
        int n = peek(r);
        if (n == ',') { r->p++; continue; }
        if (n == '}') { r->p++; break; }
        r->err = 1;
        return;
    }

done:
    if (!m.id) {
        model_free(&m);
        return;
    }
    if (!m.name) m.name = str_dup(m.id);
    /* "primary": o canonical do modelo pertence a este provedor */
    if (m.canonical && prov->id) {
        size_t l = strlen(prov->id);
        if (strncmp(m.canonical, prov->id, l) == 0 && m.canonical[l] == '/') m.primary = 1;
    }
    free(m.canonical);
    m.canonical = NULL;
    if (m.primary) prov->n_primary++;
    prov->models = (MdModel *)realloc(prov->models, (prov->n_models + 1) * sizeof(MdModel));
    prov->models[prov->n_models++] = m;
}

static void parse_models_map(R *r, MdProvider *prov)
{
    if (!expect(r, '{')) return;
    if (peek(r) == '}') { r->p++; return; }
    for (;;) {
        Str key; str_init(&key);
        if (!read_string(r, &key) || !expect(r, ':')) { str_free(&key); return; }
        str_free(&key);
        parse_model(r, prov);
        int n = peek(r);
        if (n == ',') { r->p++; continue; }
        if (n == '}') { r->p++; return; }
        r->err = 1;
        return;
    }
}

static void parse_provider(R *r, Catalog *c)
{
    MdProvider p;
    memset(&p, 0, sizeof p);

    if (!expect(r, '{')) return;
    if (peek(r) == '}') { r->p++; goto done; }
    for (;;) {
        Str key; str_init(&key);
        if (!read_string(r, &key) || !expect(r, ':')) { str_free(&key); return; }

        if (str_eq(key.data, "id")) {
            Str v; str_init(&v);
            if (read_string(r, &v)) { free(p.id); p.id = v.data; } else str_free(&v);
        } else if (str_eq(key.data, "name")) {
            Str v; str_init(&v);
            if (read_string(r, &v)) { free(p.name); p.name = v.data; } else str_free(&v);
        } else if (str_eq(key.data, "api")) {
            Str v; str_init(&v);
            if (read_string(r, &v)) { free(p.api); p.api = v.data; } else str_free(&v);
        } else if (str_eq(key.data, "env")) {
            if (!expect(r, '[')) { str_free(&key); return; }
            if (peek(r) == ']') { r->p++; }
            else for (;;) {
                Str v; str_init(&v);
                read_string_opt(r, &v);
                if (!p.env && v.data) p.env = v.data; else str_free(&v);
                int n = peek(r);
                if (n == ',') { r->p++; continue; }
                if (n == ']') { r->p++; break; }
                r->err = 1;
                str_free(&key);
                return;
            }
        } else if (str_eq(key.data, "models")) {
            parse_models_map(r, &p);
        } else {
            skip_value(r);
        }
        str_free(&key);
        int n = peek(r);
        if (n == ',') { r->p++; continue; }
        if (n == '}') { r->p++; break; }
        r->err = 1;
        return;
    }

done:
    if (!p.id || p.n_models == 0) { provider_free(&p); return; }
    if (!p.name) p.name = str_dup(p.id);
    c->providers = (MdProvider *)realloc(c->providers, (c->n_providers + 1) * sizeof(MdProvider));
    c->providers[c->n_providers++] = p;
}

static int catalog_parse(Catalog *c, const char *json, size_t len)
{
    R r = { json, json + len, 0 };
    if (!expect(&r, '{')) return 0;
    if (peek(&r) == '}') return 1;
    for (;;) {
        Str key; str_init(&key);
        if (!read_string(&r, &key) || !expect(&r, ':')) { str_free(&key); return 0; }
        str_free(&key);
        parse_provider(&r, c);
        if (r.err) {
            if (getenv("DELTA_CATALOG_DEBUG")) {
                long off = (long)(r.p - json);
                const char *ini = (off > 60) ? r.p - 60 : json;
                fprintf(stderr, "[catalog] parse falhou no byte %ld de %zu. contexto: %.70s\n",
                        off, len, ini);
            }
            return 0;
        }
        int n = peek(&r);
        if (n == ',') { r.p++; continue; }
        if (n == '}') return 1;
        return 0;
    }
}

/* ================================================================== */
/* Cache                                                               */
/* ================================================================== */
char *catalog_cache_path(void)
{
    char *dir = delta_home();
    dir_make(dir);
    /* o nome carrega a versao do formato: cache antigo e ignorado sozinho */
    char *p = path_join(dir, "models.v2.json");
    free(dir);
    return p;
}

static void catalog_write_cache(const Catalog *c)
{
    char *path = catalog_cache_path();
    Str o; str_init(&o);
    char tmp[256];      /* cabe o registro inteiro do modelo, sem truncar */
    snprintf(tmp, sizeof tmp, "{\"fetched\":%lld,\"providers\":[", (long long)time(NULL));
    str_adds(&o, tmp);

    for (size_t i = 0; i < c->n_providers; i++) {
        const MdProvider *p = &c->providers[i];
        if (i) str_addc(&o, ',');
        str_adds(&o, "{\"id\":");
        json_write_string(&o, p->id, strlen(p->id));
        str_adds(&o, ",\"name\":");
        json_write_string(&o, p->name, strlen(p->name));
        str_adds(&o, ",\"api\":");
        json_write_string(&o, p->api ? p->api : "", strlen(p->api ? p->api : ""));
        str_adds(&o, ",\"env\":[");
        if (p->env) json_write_string(&o, p->env, strlen(p->env));
        str_adds(&o, "],\"models\":[");
        for (size_t k = 0; k < p->n_models; k++) {
            const MdModel *m = &p->models[k];
            if (k) str_addc(&o, ',');
            str_adds(&o, "{\"id\":");
            json_write_string(&o, m->id, strlen(m->id));
            str_adds(&o, ",\"name\":");
            json_write_string(&o, m->name, strlen(m->name));
            int n = snprintf(tmp, sizeof tmp,
                             ",\"ctx\":%d,\"out\":%d,\"cin\":%.4f,\"cout\":%.4f,\"ccr\":%.4f,\"ccw\":%.4f,"
                             "\"r\":%d,\"t\":%d,\"pr\":%d,\"tk\":%d,\"tm\":%u,\"bmn\":%d,\"bmx\":%d,\"im\":%d}",
                             m->ctx, m->out, m->cost_in, m->cost_out, m->cost_cache_read, m->cost_cache_write,
                             m->reasoning, m->tool_call, m->primary,
                             m->think_kind, m->think_mask, m->budget_min, m->budget_max, m->image_in);
            if (n < 0 || (size_t)n >= sizeof tmp) {
                /* nunca gravar JSON truncado: melhor nao ter cache */
                str_free(&o);
                free(path);
                remove(catalog_cache_path());
                return;
            }
            str_adds(&o, tmp);
        }
        str_adds(&o, "]}");
    }
    str_adds(&o, "]}");
    file_write(path, o.data, o.len);
    str_free(&o);
    free(path);
}

/* Cache tem a mesma forma do original, mas com "models" como array. */
static int catalog_parse_cache(Catalog *c, const char *json, size_t len, long long *fetched)
{
    R r = { json, json + len, 0 };
    if (!expect(&r, '{')) return 0;
    if (peek(&r) == '}') return 1;
    for (;;) {
        Str key; str_init(&key);
        if (!read_string(&r, &key) || !expect(&r, ':')) { str_free(&key); return 0; }

        if (str_eq(key.data, "fetched")) {
            *fetched = (long long)read_number(&r);
            str_free(&key);
        } else if (str_eq(key.data, "providers")) {
            str_free(&key);
            if (!expect(&r, '[')) return 0;
            if (peek(&r) == ']') { r.p++; }
            else for (;;) {
                MdProvider p;
                memset(&p, 0, sizeof p);
                if (!expect(&r, '{')) return 0;
                if (peek(&r) == '}') { r.p++; }
                else for (;;) {
                    Str k2; str_init(&k2);
                    if (!read_string(&r, &k2) || !expect(&r, ':')) { str_free(&k2); return 0; }
                    if (str_eq(k2.data, "id")) {
                        Str v; str_init(&v);
                        if (read_string(&r, &v)) p.id = v.data; else str_free(&v);
                    } else if (str_eq(k2.data, "name")) {
                        Str v; str_init(&v);
                        if (read_string(&r, &v)) p.name = v.data; else str_free(&v);
                    } else if (str_eq(k2.data, "api")) {
                        Str v; str_init(&v);
                        if (read_string(&r, &v)) { if (v.len) p.api = v.data; else str_free(&v); }
                        else str_free(&v);
                    } else if (str_eq(k2.data, "env")) {
                        str_free(&k2);
                        if (!expect(&r, '[')) return 0;
                        if (peek(&r) == ']') { r.p++; }
                        else for (;;) {
                            Str v; str_init(&v);
                            read_string_opt(&r, &v);
                            if (!p.env && v.data) p.env = v.data; else str_free(&v);
                            int n = peek(&r);
                            if (n == ',') { r.p++; continue; }
                            if (n == ']') { r.p++; break; }
                            return 0;
                        }
                        goto next_field;
                    } else if (str_eq(k2.data, "models")) {
                        str_free(&k2);
                        if (!expect(&r, '[')) return 0;
                        if (peek(&r) == ']') { r.p++; }
                        else for (;;) {
                            MdModel m;
                            memset(&m, 0, sizeof m);
                            if (!expect(&r, '{')) return 0;
                            if (peek(&r) == '}') { r.p++; }
                            else for (;;) {
                                Str k3; str_init(&k3);
                                if (!read_string(&r, &k3) || !expect(&r, ':')) { str_free(&k3); return 0; }
                                if (str_eq(k3.data, "id")) {
                                    Str v; str_init(&v);
                                    if (read_string(&r, &v)) m.id = v.data; else str_free(&v);
                                } else if (str_eq(k3.data, "name")) {
                                    Str v; str_init(&v);
                                    if (read_string(&r, &v)) m.name = v.data; else str_free(&v);
                                } else if (str_eq(k3.data, "ctx")) m.ctx = (int)read_number(&r);
                                else if (str_eq(k3.data, "out")) m.out = (int)read_number(&r);
                                else if (str_eq(k3.data, "cin")) m.cost_in = read_number(&r);
                                else if (str_eq(k3.data, "cout")) m.cost_out = read_number(&r);
                                else if (str_eq(k3.data, "ccr")) m.cost_cache_read = read_number(&r);
                                else if (str_eq(k3.data, "ccw")) m.cost_cache_write = read_number(&r);
                                else if (str_eq(k3.data, "r")) m.reasoning = (int)read_number(&r);
                                else if (str_eq(k3.data, "t")) m.tool_call = (int)read_number(&r);
                                else if (str_eq(k3.data, "pr")) m.primary = (int)read_number(&r);
                                else if (str_eq(k3.data, "tk")) m.think_kind = (int)read_number(&r);
                                else if (str_eq(k3.data, "tm")) m.think_mask = (unsigned)read_number(&r);
                                else if (str_eq(k3.data, "bmn")) m.budget_min = (int)read_number(&r);
                                else if (str_eq(k3.data, "bmx")) m.budget_max = (int)read_number(&r);
                                else if (str_eq(k3.data, "im")) m.image_in = (int)read_number(&r);
                                else skip_value(&r);
                                str_free(&k3);
                                int n = peek(&r);
                                if (n == ',') { r.p++; continue; }
                                if (n == '}') { r.p++; break; }
                                return 0;
                            }
                            if (m.id) {
                                if (!m.name) m.name = str_dup(m.id);
                                p.models = (MdModel *)realloc(p.models, (p.n_models + 1) * sizeof(MdModel));
                                p.models[p.n_models++] = m;
                            }
                            int n = peek(&r);
                            if (n == ',') { r.p++; continue; }
                            if (n == ']') { r.p++; break; }
                            return 0;
                        }
                        goto next_field;
                    } else {
                        skip_value(&r);
                    }
                    str_free(&k2);
                next_field:
                    int n = peek(&r);
                    if (n == ',') { r.p++; continue; }
                    if (n == '}') { r.p++; break; }
                    return 0;
                }
                if (p.id && p.n_models) {
                    if (!p.name) p.name = str_dup(p.id);
                    for (size_t z = 0; z < p.n_models; z++) if (p.models[z].primary) p.n_primary++;
                    c->providers = (MdProvider *)realloc(c->providers, (c->n_providers + 1) * sizeof(MdProvider));
                    c->providers[c->n_providers++] = p;
                } else {
                    provider_free(&p);
                }
                int n = peek(&r);
                if (n == ',') { r.p++; continue; }
                if (n == ']') { r.p++; break; }
                return 0;
            }
            /* depois do array de provedores ainda falta o '}' do objeto */
            {
                int n = peek(&r);
                if (n == ',') { r.p++; continue; }
                if (n == '}') return 1;
                return 0;
            }
        } else {
            str_free(&key);
            skip_value(&r);
        }

        int n = peek(&r);
        if (n == ',') { r.p++; continue; }
        if (n == '}') return 1;
        return 0;
    }
}

/* ================================================================== */
/* Ordenacao: chave configurada primeiro, depois quem e dono do modelo  */
/* ================================================================== */
static int ci_cmp(const char *a, const char *b)
{
    if (!a) a = "";
    if (!b) b = "";
    for (;;) {
        char x = *a++, y = *b++;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return (unsigned char)x < (unsigned char)y ? -1 : 1;
        if (!x) return 0;
    }
}

static int prov_group(const MdProvider *p)
{
    if (p->env && *p->env && getenv(p->env)) return 0;   /* chave no ambiente */
    if (p->n_primary > 0) return 1;                       /* dono dos modelos */
    return 2;
}

static int cmp_provider(const void *a, const void *b)
{
    const MdProvider *x = (const MdProvider *)a;
    const MdProvider *y = (const MdProvider *)b;
    int gx = prov_group(x), gy = prov_group(y);
    if (gx != gy) return gx - gy;
    return ci_cmp(x->name, y->name);
}

static int cmp_model(const void *a, const void *b)
{
    const MdModel *x = (const MdModel *)a;
    const MdModel *y = (const MdModel *)b;
    if (x->primary != y->primary) return y->primary - x->primary;
    return ci_cmp(x->id, y->id);
}

static void catalog_sort(Catalog *c)
{
    for (size_t i = 0; i < c->n_providers; i++) {
        MdProvider *p = &c->providers[i];
        if (p->n_models > 1) qsort(p->models, p->n_models, sizeof(MdModel), cmp_model);
    }
    if (c->n_providers > 1) qsort(c->providers, c->n_providers, sizeof(MdProvider), cmp_provider);
}

/* ================================================================== */
/* API                                                                 */
/* ================================================================== */
void catalog_init(Catalog *c) { memset(c, 0, sizeof(*c)); }

void catalog_free(Catalog *c)
{
    if (!c) return;
    for (size_t i = 0; i < c->n_providers; i++) provider_free(&c->providers[i]);
    free(c->providers);
    memset(c, 0, sizeof(*c));
}

/* Carrega somente do cache local, sem rede. */
int catalog_load_cached(Catalog *c)
{
    catalog_free(c);
    char *cache = catalog_cache_path();
    size_t len = 0;
    char *text = file_read(cache, &len);
    free(cache);
    if (!text) return 0;

    long long f = 0;
    int ok = catalog_parse_cache(c, text, len, &f) && c->n_providers > 0;
    free(text);
    if (!ok) { catalog_free(c); return 0; }

    c->fetched = f;
    catalog_sort(c);
    return 1;
}

int catalog_load(Catalog *c, int force_refresh, Str *err)
{
    Catalog velho;
    catalog_init(&velho);
    int tem_velho = 0;

    if (!force_refresh) {
        if (catalog_load_cached(&velho)) {
            if (velho.fetched > 0 &&
                (long long)time(NULL) - velho.fetched < CATALOG_TTL_SECONDS) {
                catalog_free(c);
                *c = velho;
                return 1;
            }
            tem_velho = 1;      /* cache vencido: serve de reserva */
        }
    } else {
        tem_velho = catalog_load_cached(&velho);
    }

    Str body; str_init(&body);
    Str e; str_init(&e);
    if (http_get_text_to(MODELSDEV_URL, &body, &e, 20)) {   /* 20 s: e' comando de TUI */
        catalog_free(c);
        catalog_init(c);
        if (catalog_parse(c, body.data ? body.data : "", body.len) && c->n_providers > 0) {
            c->fetched = (long long)time(NULL);
            catalog_sort(c);
            catalog_write_cache(c);
            str_free(&body);
            str_free(&e);
            catalog_free(&velho);
            return 1;
        }
        catalog_free(c);
    }

    str_free(&body);
    str_free(&e);
    if (tem_velho) {
        catalog_free(c);
        *c = velho;
        return 1;
    }
    catalog_free(&velho);
    if (err) str_addf(err, "falha ao baixar %s (%s)", MODELSDEV_URL, e.data ? e.data : "sem detalhe");
    return 0;
}

/* Variavel de ambiente da chave, quando o models.dev nao informa. */
static const char *builtin_env(const char *provider)
{
    if (!provider) return NULL;
    if (str_eq(provider, "anthropic"))  return "ANTHROPIC_API_KEY";
    if (str_eq(provider, "openai"))     return "OPENAI_API_KEY";
    if (str_eq(provider, "google"))     return "GOOGLE_API_KEY";
    if (str_eq(provider, "groq"))       return "GROQ_API_KEY";
    if (str_eq(provider, "xai"))        return "XAI_API_KEY";
    if (str_eq(provider, "mistral"))    return "MISTRAL_API_KEY";
    if (str_eq(provider, "deepseek"))   return "DEEPSEEK_API_KEY";
    if (str_eq(provider, "cerebras"))   return "CEREBRAS_API_KEY";
    if (str_eq(provider, "openrouter")) return "OPENROUTER_API_KEY";
    if (str_eq(provider, "together"))   return "TOGETHER_API_KEY";
    if (str_eq(provider, "fireworks"))  return "FIREWORKS_API_KEY";
    if (str_eq(provider, "perplexity")) return "PERPLEXITY_API_KEY";
    if (str_eq(provider, "zai"))        return "ZHIPU_API_KEY";
    if (str_eq(provider, "moonshotai")) return "MOONSHOT_API_KEY";
    return NULL;
}

const char *catalog_env_var(const Catalog *c, const char *provider)
{
    const MdProvider *p = catalog_provider(c, provider);
    if (p && p->env && *p->env) return p->env;
    return builtin_env(provider);
}

const MdProvider *catalog_provider(const Catalog *c, const char *id)
{
    if (!c || !id) return NULL;
    for (size_t i = 0; i < c->n_providers; i++)
        if (str_eq(c->providers[i].id, id)) return &c->providers[i];
    return NULL;
}

const MdModel *catalog_model(const Catalog *c, const char *provider, const char *model)
{
    const MdProvider *p = catalog_provider(c, provider);
    if (!p || !model) return NULL;
    for (size_t i = 0; i < p->n_models; i++)
        if (str_eq(p->models[i].id, model)) return &p->models[i];
    return NULL;
}

const char *catalog_endpoint(const Catalog *c, const char *provider)
{
    const MdProvider *p = catalog_provider(c, provider);
    if (p && p->api && *p->api) return p->api;
    if (!provider) return "";
    if (str_eq(provider, "anthropic"))      return "https://api.anthropic.com";
    if (str_eq(provider, "openai"))         return "https://api.openai.com/v1";
    if (str_eq(provider, "google"))         return "https://generativelanguage.googleapis.com/v1beta/openai";
    if (str_eq(provider, "groq"))           return "https://api.groq.com/openai/v1";
    if (str_eq(provider, "xai"))            return "https://api.x.ai/v1";
    if (str_eq(provider, "mistral"))        return "https://api.mistral.ai/v1";
    if (str_eq(provider, "deepseek"))       return "https://api.deepseek.com";
    if (str_eq(provider, "cerebras"))       return "https://api.cerebras.ai/v1";
    if (str_eq(provider, "openrouter"))     return "https://openrouter.ai/api/v1";
    if (str_eq(provider, "together"))       return "https://api.together.xyz/v1";
    if (str_eq(provider, "fireworks"))      return "https://api.fireworks.ai/inference/v1";
    if (str_eq(provider, "perplexity"))     return "https://api.perplexity.ai";
    if (str_eq(provider, "zai"))            return "https://api.z.ai/api/paas/v4";
    if (str_eq(provider, "moonshotai"))     return "https://api.moonshot.ai/v1";
    return "";
}

int catalog_anthropic_style(const char *provider)
{
    return provider && str_eq(provider, "anthropic");
}

const MdModel *catalog_default_model(const Catalog *c, const char *provider)
{
    const MdProvider *p = catalog_provider(c, provider);
    if (!p) return NULL;
    /* prefere um modelo com tool_call e contexto grande */
    const MdModel *best = NULL;
    for (size_t i = 0; i < p->n_models; i++) {
        const MdModel *m = &p->models[i];
        if (!m->tool_call) continue;
        if (!best || m->ctx > best->ctx) best = m;
    }
    if (best) return best;
    return p->n_models ? &p->models[0] : NULL;
}
