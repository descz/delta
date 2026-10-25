/* ============================================================================
 * start_background_job — PORT C pronto para o seu harness
 * ----------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"command":{"type":"string"},"name":{"type":"string"}},"required":["command"]}
 * efeito    : PROCESS  (requer aprovacao)
 * handler   : zt_tool_start_background_job
 * motor     : zt_shell_op(op="start")
 * registro  : zt_register_shell(h)   (ou zt_register_all(h))
 * ----------------------------------------------------------------------------
 * Dependencias: apenas zenotools.h + libc. Todo IO passa pela vtable zt_host.
 * ========================================================================= */

/* --- handler registrado no registry --- */
int zt_tool_start_background_job(zt_harness *h, const char *a, char **o, char **e) { return zt_shell_op(h, "start", a, o, e); }

/* --- motor da familia (compartilhado por todas as tools de "shell") --- */
int zt_shell_op(zt_harness *h, const char *op, const char *args_json, char **out, char **err) {
    if (h == NULL || op == NULL) return 0;
    if (strcmp(op, "run") == 0) {
        char *command = NULL;
        zt_arg_string(args_json, "command", &command, "");
        if (command == NULL || *command == '\0') { if (out != NULL) *out = zt_strdup("Error: 'command' e obrigatorio."); return 0; }
        if (zt_command_blocked(command)) {
            if (out != NULL) *out = zt_format("SANDBOX BLOCKED: comando recusado pela politica de seguranca: %s", command);
            if (err != NULL) *err = zt_strdup("blocked by policy");
            free(command);
            return 0;
        }
        if (h->host.exec == NULL) {
            if (out != NULL) *out = zt_strdup("Error: hook host.exec nao configurado.");
            if (err != NULL) *err = zt_strdup("no exec hook");
            free(command);
            return 0;
        }
        int exit_code = 0;
        char *hook_error = NULL;
        char *result = h->host.exec(h->host.ctx, command, zt_arg_seconds_ms(args_json, "timeout_seconds", 120, 900), &exit_code, &hook_error);
        if (result == NULL) {
            if (out != NULL) *out = zt_format("Error: %s", hook_error != NULL ? hook_error : "falha ao executar comando");
            if (err != NULL) *err = hook_error != NULL ? zt_strdup(hook_error) : zt_strdup("exec failed");
            free(hook_error); free(command);
            return 0;
        }
        *out = exit_code == 0 ? result : zt_format("Error: comando retornou codigo %d\n%s", exit_code, result);
        int ok = exit_code == 0;
        if (ok) result = NULL;                 /* ownership passou para *out */
        if (!ok && err != NULL) *err = zt_format("exit code %d", exit_code);
        free(result); free(hook_error); free(command);
        return ok;
    }
    if (strcmp(op, "start") == 0) {
        if (h->host.job_start == NULL) { if (out != NULL) *out = zt_strdup("Error: hook host.job_start nao configurado."); return 0; }
        char *command = NULL, *name = NULL;
        zt_arg_string(args_json, "command", &command, "");
        zt_arg_string(args_json, "name", &name, "job");
        if (zt_command_blocked(command)) {
            if (out != NULL) *out = zt_format("SANDBOX BLOCKED: comando recusado: %s", command);
            free(command); free(name);
            return 0;
        }
        char *hook_error = NULL;
        char *result = h->host.job_start(h->host.ctx, command, name, &hook_error);
        if (result == NULL) { if (out != NULL) *out = zt_format("Error: %s", hook_error != NULL ? hook_error : "falha ao iniciar job"); free(hook_error); free(command); free(name); return 0; }
        *out = result;
        free(hook_error); free(command); free(name);
        return 1;
    }
    if (strcmp(op, "list") == 0) {
        if (h->host.job_list == NULL) { if (out != NULL) *out = zt_strdup("Error: hook host.job_list nao configurado."); return 0; }
        char *hook_error = NULL;
        char *result = h->host.job_list(h->host.ctx, zt_arg_bool(args_json, "include_completed", 1), &hook_error);
        if (result == NULL) { if (out != NULL) *out = zt_format("Error: %s", hook_error != NULL ? hook_error : "falha ao listar jobs"); free(hook_error); return 0; }
        *out = result; free(hook_error);
        return 1;
    }
    if (strcmp(op, "tail") == 0) {
        if (h->host.job_tail == NULL) { if (out != NULL) *out = zt_strdup("Error: hook host.job_tail nao configurado."); return 0; }
        char *job_id = NULL;
        zt_arg_string(args_json, "job_id", &job_id, "");
        if (job_id == NULL || *job_id == '\0') { if (out != NULL) *out = zt_strdup("Error: 'job_id' e obrigatorio."); return 0; }
        char *hook_error = NULL;
        char *result = h->host.job_tail(h->host.ctx, job_id, zt_arg_size(args_json, "max_lines", 100, 5000), &hook_error);
        if (result == NULL) { if (out != NULL) *out = zt_format("Error: %s", hook_error != NULL ? hook_error : "falha ao ler job"); free(hook_error); free(job_id); return 0; }
        *out = result; free(hook_error); free(job_id);
        return 1;
    }
    if (strcmp(op, "cancel") == 0) {
        if (h->host.job_cancel == NULL) { if (out != NULL) *out = zt_strdup("Error: hook host.job_cancel nao configurado."); return 0; }
        char *job_id = NULL;
        zt_arg_string(args_json, "job_id", &job_id, "");
        if (job_id == NULL || *job_id == '\0') { if (out != NULL) *out = zt_strdup("Error: 'job_id' e obrigatorio."); return 0; }
        char *hook_error = NULL;
        char *result = h->host.job_cancel(h->host.ctx, job_id, &hook_error);
        if (result == NULL) { if (out != NULL) *out = zt_format("Error: %s", hook_error != NULL ? hook_error : "falha ao cancelar job"); free(hook_error); free(job_id); return 0; }
        *out = result; free(hook_error); free(job_id);
        return 1;
    }
    if (out != NULL) *out = zt_format("Error: operacao desconhecida de shell: %s", op);
    return 0;
}
