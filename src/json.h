/* json.h - parser e escritor JSON minimo (DOM proprio, ordem preservada) */
#ifndef DELTA_JSON_H
#define DELTA_JSON_H

#include "util.h"

typedef enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } JKind;

typedef struct JValue JValue;

typedef struct {
    char   *key;
    JValue *val;
} JPair;

struct JValue {
    JKind kind;
    double num;          /* J_NUM */
    int    boolean;      /* J_BOOL */
    char  *str;          /* J_STR */
    /* array */
    JValue **items;
    size_t   count;
    /* object */
    JPair  *pairs;
};

/* Parser */
JValue *json_parse(const char *text, size_t len);
JValue *json_parse_cstr(const char *text);
void    json_free(JValue *v);
/* Retorna NULL quando o texto nao e um JSON completo e valido. */
const char *json_error(void);

/* Acesso (todos tolerantes a NULL) */
JValue     *json_get(JValue *obj, const char *key);      /* NULL se ausente */
JValue     *json_at(JValue *arr, size_t i);
const char *json_str(JValue *v);                          /* "" se ausente */
double      json_num(JValue *v, double def);
int         json_bool(JValue *v, int def);
size_t      json_count(JValue *v);
#define json_is_str(v) ((v) && (v)->kind == J_STR)
#define json_is_obj(v) ((v) && (v)->kind == J_OBJ)
#define json_is_arr(v) ((v) && (v)->kind == J_ARR)

/* Escritor */
void json_write_string(Str *out, const char *s, size_t n);

#endif /* DELTA_JSON_H */
