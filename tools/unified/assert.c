/* ============================================================================
 * assert — TOOL UNIFICADA (substitui 2 tools granulares)
 * ----------------------------------------------------------------------------
 * ops      : equals, contains
 * cobre    : assert_true, assert_contains
 * handler  : zt_tool_assert_unified
 * motor    : zt_verify_op()
 * ========================================================================= */

/* --- schema (use em zt_register_unified ou no seu proprio registro) --- */
/* schema da tool "assert": ver ZenoTools/harness/src/zt_register.c (tabela UNIFIED_TOOLS) */

/* --- handler unificado --- */
int zt_tool_assert_unified(zt_harness *h, const char *args_json, char **out, char **err) {
    char *op = NULL;
    if (!zt_arg_string(args_json, "op", &op, NULL) || op == NULL) {
        if (out != NULL) *out = zt_strdup("Error: 'op' e obrigatorio (equals|contains).");
        return 0;
    }
    /* aceita os apelidos assert_true/assert_contains */
    int ok = zt_verify_op(h, (strcmp(op, "assert_true") == 0) ? "equals" : (strcmp(op, "assert_contains") == 0) ? "contains" : op, args_json, out, err);
    free(op);
    return ok;
}

/* --- motor compartilhado com as tools granulares --- */
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

/* --- tools granulares cobertas --- */
/* assert_true: zt_tool_assert_true() -> zt_invoke(h, "assert_true", args) */
/* assert_contains: zt_tool_assert_contains() -> zt_invoke(h, "assert_contains", args) */
