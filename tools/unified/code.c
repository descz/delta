/* ============================================================================
 * code — TOOL UNIFICADA (substitui 3 tools granulares)
 * ----------------------------------------------------------------------------
 * ops      : structure, symbol, tests
 * cobre    : run_tests, codebase_structure, find_symbol
 * handler  : zt_tool_code_unified
 * motor    : zt_code_op()
 * ========================================================================= */

/* --- schema (use em zt_register_unified ou no seu proprio registro) --- */
/* schema da tool "code": ver ZenoTools/harness/src/zt_register.c (tabela UNIFIED_TOOLS) */

/* --- handler unificado --- */
int zt_tool_code_unified(zt_harness *h, const char *args_json, char **out, char **err) {
    char *op = NULL;
    if (!zt_arg_string(args_json, "op", &op, NULL) || op == NULL) {
        if (out != NULL) *out = zt_strdup("Error: 'op' e obrigatorio (structure|symbol|tests).");
        return 0;
    }
    int ok = zt_code_op(h, op, args_json, out, err);
    free(op);
    return ok;
}

/* --- motor compartilhado com as tools granulares --- */
int zt_code_op(zt_harness *h, const char *op, const char *args_json, char **out, char **err) {
    if (h == NULL || op == NULL) return 0;
    if (strcmp(op, "structure") == 0) { *out = code_structure(h); return *out != NULL; }
    if (strcmp(op, "symbol") == 0) {
        char *name = NULL;
        zt_arg_string(args_json, "name", &name, "");
        if (name == NULL || *name == '\0') { if (out != NULL) *out = zt_strdup("Error: 'name' e obrigatorio."); return 0; }
        if (h->host.search == NULL) { if (out != NULL) *out = zt_strdup("Error: hook host.search nao configurado (necessario para find_symbol)."); free(name); return 0; }
        char *hook_error = NULL;
        char *hits = h->host.search(h->host.ctx, h->workspace_root, name, zt_arg_size(args_json, "max_results", 60, 500), &hook_error);
        if (hits == NULL) { if (out != NULL) *out = zt_format("Error: %s", hook_error != NULL ? hook_error : "falha na busca"); free(hook_error); free(name); return 0; }
        char *formatted = zt_format("SYMBOL '%s'\n%s", name, hits);
        *out = formatted;
        free(hits); free(hook_error); free(name);
        return 1;
    }
    if (strcmp(op, "tests") == 0) {
        char command[512];
        const char *framework = NULL;
        detect_test_command(h, command, sizeof(command), &framework);
        if (command[0] == '\0') { if (out != NULL) *out = zt_strdup("Error: nenhum framework de teste detectado no workspace."); return 0; }
        if (h->host.exec == NULL) { if (out != NULL) *out = zt_strdup("Error: hook host.exec nao configurado."); return 0; }
        int exit_code = 0;
        char *hook_error = NULL;
        char *result = h->host.exec(h->host.ctx, command, zt_arg_seconds_ms(args_json, "timeout_seconds", 300, 1800), &exit_code, &hook_error);
        if (result == NULL) {
            if (out != NULL) *out = zt_format("Error: %s", hook_error != NULL ? hook_error : "falha ao rodar os testes");
            if (err != NULL) *err = zt_strdup("tests failed");
            free(hook_error);
            return 0;
        }
        *out = zt_format("TESTS (%s: %s) exit=%d\n%s", framework, command, exit_code, result);
        if (exit_code != 0 && err != NULL) *err = zt_format("exit code %d", exit_code);
        free(result); free(hook_error);
        return exit_code == 0;
    }
    if (out != NULL) *out = zt_format("Error: operacao desconhecida de codigo: %s", op);
    return 0;
}

/* --- tools granulares cobertas --- */
/* run_tests: zt_tool_run_tests() -> zt_invoke(h, "run_tests", args) */
/* codebase_structure: zt_tool_codebase_structure() -> zt_invoke(h, "codebase_structure", args) */
/* find_symbol: zt_tool_find_symbol() -> zt_invoke(h, "find_symbol", args) */
