/* ============================================================================
 * git — TOOL UNIFICADA (substitui 4 tools granulares)
 * ----------------------------------------------------------------------------
 * ops      : status, diff, log, checkpoint
 * cobre    : git_status, git_diff, git_log, git_checkpoint
 * handler  : zt_tool_git_unified
 * motor    : zt_git_op()
 * ========================================================================= */

/* --- schema (use em zt_register_unified ou no seu proprio registro) --- */
/* schema da tool "git": ver ZenoTools/harness/src/zt_register.c (tabela UNIFIED_TOOLS) */

/* --- handler unificado --- */
int zt_tool_git_unified(zt_harness *h, const char *args_json, char **out, char **err) {
    char *op = NULL;
    if (!zt_arg_string(args_json, "op", &op, NULL) || op == NULL) {
        if (out != NULL) *out = zt_strdup("Error: 'op' e obrigatorio (status|diff|log|checkpoint).");
        return 0;
    }
    int ok = zt_git_op(h, op, args_json, out, err);
    free(op);
    return ok;
}

/* --- motor compartilhado com as tools granulares --- */
int zt_git_op(zt_harness *h, const char *op, const char *args_json, char **out, char **err) {
    if (h == NULL || op == NULL) return 0;
    if (strcmp(op, "status") == 0) return run_git(h, "status --short --branch", out, err);
    if (strcmp(op, "diff") == 0) return run_git(h, "diff --stat --patch", out, err);
    if (strcmp(op, "log") == 0) {
        long count = (long)zt_arg_number(args_json, "n", 10);
        if (count <= 0) count = 10;
        if (count > 500) count = 500;
        char *command = zt_format("log -n %ld --oneline --decorate", count);
        int ok = run_git(h, command, out, err);
        free(command);
        return ok;
    }
    if (strcmp(op, "checkpoint") == 0) {
        char *message = NULL;
        zt_arg_string(args_json, "message", &message, "Zeno checkpoint");
        char *add_command = zt_format("add -A && git -C \"%s\" commit -m \"%s\"", h->workspace_root, message);
        int ok = 0;
        if (h->host.exec != NULL) {
            int exit_code = 0;
            char *hook_error = NULL;
            char *result = h->host.exec(h->host.ctx, zt_format("git -C \"%s\" %s", h->workspace_root, add_command), 120000, &exit_code, &hook_error);
            if (result != NULL) { if (out != NULL) *out = exit_code == 0 ? result : zt_format("Error: %s", result); ok = exit_code == 0; free(result); }
            else if (out != NULL) *out = zt_format("Error: %s", hook_error != NULL ? hook_error : "checkpoint falhou");
            free(hook_error);
        } else if (h->host.git != NULL) {
            char *hook_error = NULL;
            char *result = h->host.git(h->host.ctx, h->workspace_root, "checkpoint", &hook_error);
            if (result != NULL) { if (out != NULL) *out = result; ok = 1; free(result); }
            else if (out != NULL) *out = zt_format("Error: %s", hook_error != NULL ? hook_error : "checkpoint falhou");
            free(hook_error);
        } else if (out != NULL) *out = zt_strdup("Error: hooks host.exec/host.git nao configurados.");
        if (!ok && err != NULL) *err = zt_strdup("checkpoint failed");
        free(add_command); free(message);
        return ok;
    }
    if (out != NULL) *out = zt_format("Error: operacao desconhecida de git: %s", op);
    return 0;
}

/* --- tools granulares cobertas --- */
/* git_status: zt_tool_git_status() -> zt_invoke(h, "git_status", args) */
/* git_diff: zt_tool_git_diff() -> zt_invoke(h, "git_diff", args) */
/* git_log: zt_tool_git_log() -> zt_invoke(h, "git_log", args) */
/* git_checkpoint: zt_tool_git_checkpoint() -> zt_invoke(h, "git_checkpoint", args) */
