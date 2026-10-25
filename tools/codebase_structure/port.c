/* ============================================================================
 * codebase_structure — PORT C pronto para o seu harness
 * ----------------------------------------------------------------------------
 * schema    : {"type":"object"}
 * efeito    : READ_LOCAL
 * handler   : zt_tool_codebase_structure
 * motor     : zt_code_op(op="structure")
 * registro  : zt_register_code(h)   (ou zt_register_all(h))
 * ----------------------------------------------------------------------------
 * Dependencias: apenas zenotools.h + libc. Todo IO passa pela vtable zt_host.
 * ========================================================================= */

/* --- handler registrado no registry --- */
int zt_tool_codebase_structure(zt_harness *h, const char *a, char **o, char **e) { return zt_code_op(h, "structure", a, o, e); }

/* --- motor da familia (compartilhado por todas as tools de "code") --- */
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
