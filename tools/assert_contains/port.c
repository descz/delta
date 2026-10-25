/* ============================================================================
 * assert_contains — PORT C pronto para o seu harness
 * ----------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"condition_description":{"type":"string"},"value":{"type":"string"},"substring":{"type":"string"}},"required":["condition_description","value","substring"]}
 * efeito    : READ_LOCAL
 * handler   : zt_tool_assert_contains
 * motor     : zt_verify_op(op="contains")
 * registro  : zt_register_verify(h)   (ou zt_register_all(h))
 * ----------------------------------------------------------------------------
 * Dependencias: apenas zenotools.h + libc. Todo IO passa pela vtable zt_host.
 * ========================================================================= */

/* --- handler registrado no registry --- */
int zt_tool_assert_contains(zt_harness *h, const char *a, char **o, char **e) { return zt_verify_op(h, "contains", a, o, e); }

/* --- motor da familia (compartilhado por todas as tools de "assert") --- */
int zt_verify_op(zt_harness *h, const char *op, const char *args_json, char **out, char **err) {
    (void)h;
    char *description = NULL, *value = NULL, *expected = NULL, *substring = NULL;
    zt_arg_string(args_json, "condition_description", &description, "assertion");
    zt_arg_string(args_json, "value", &value, "");
    zt_arg_string(args_json, "expected", &expected, "");
    zt_arg_string(args_json, "substring", &substring, "");
    int ok = 0;
    if (strcmp(op, "equals") == 0) {
        ok = value != NULL && expected != NULL && strcmp(value, expected) == 0;
        if (out != NULL) *out = ok ? zt_format("PASS: %s (valor identico ao esperado)", description)
                                   : zt_format("FAIL: %s\n  esperado: %s\n  obtido  : %s", description, expected != NULL ? expected : "", value != NULL ? value : "");
    } else if (strcmp(op, "contains") == 0) {
        ok = value != NULL && substring != NULL && strstr(value, substring) != NULL;
        if (out != NULL) *out = ok ? zt_format("PASS: %s (contem \"%s\")", description, substring != NULL ? substring : "")
                                   : zt_format("FAIL: %s\n  nao contem: %s\n  valor: %.500s", description, substring != NULL ? substring : "", value != NULL ? value : "");
    } else {
        if (out != NULL) *out = zt_format("Error: operacao desconhecida de assert: %s", op);
    }
    if (!ok && err != NULL) *err = zt_format("assertion falhou: %s", description != NULL ? description : "");
    free(description); free(value); free(expected); free(substring);
    return ok;
}
