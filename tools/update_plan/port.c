/* ============================================================================
 * update_plan — PORT C pronto para o seu harness
 * ----------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"plan_markdown":{"type":"string"}},"required":["plan_markdown"]}
 * efeito    : WRITE_LOCAL
 * handler   : zt_tool_update_plan
 * motor     : zt_agent_op(op="plan")
 * registro  : zt_register_agent(h)   (ou zt_register_all(h))
 * ----------------------------------------------------------------------------
 * Dependencias: apenas zenotools.h + libc. Todo IO passa pela vtable zt_host.
 * ========================================================================= */

/* --- handler registrado no registry --- */
int zt_tool_update_plan(zt_harness *h, const char *a, char **o, char **e) { return zt_agent_op(h, "plan", a, o, e); }

/* --- motor da familia (compartilhado por todas as tools de "agent") --- */
int zt_agent_op(zt_harness *h, const char *op, const char *args_json, char **out, char **err) {
    if (h == NULL || op == NULL) return 0;
    if (strcmp(op, "plan") == 0) {
        char *plan = NULL;
        zt_arg_string(args_json, "plan_markdown", &plan, "");
        if (plan == NULL) plan = zt_strdup("");
        size_t pending = 0, done = 0;
        for (char *cursor = plan; (cursor = strstr(cursor, "- [")) != NULL; cursor += 3) {
            if (cursor[3] == ']') { if (cursor[2] == 'x' || cursor[2] == 'X') done++; else pending++; }
        }
        free(h->plan_markdown);
        h->plan_markdown = plan;
        if (out != NULL) *out = zt_format("Plan updated: %lu pendentes, %lu concluidos.", (unsigned long)pending, (unsigned long)done);
        return 1;
    }
    if (strcmp(op, "subagent") == 0) {
        if (h->host.subagent == NULL) {
            if (out != NULL) *out = zt_strdup("Error: hook host.subagent nao configurado (spawn_subagent precisa de um modelo).");
            if (err != NULL) *err = zt_strdup("no subagent hook");
            return 0;
        }
        char *task = NULL;
        zt_arg_string(args_json, "task", &task, "");
        if (task == NULL || *task == '\0') { if (out != NULL) *out = zt_strdup("Error: 'task' e obrigatorio."); return 0; }
        char *hook_error = NULL;
        char *result = h->host.subagent(h->host.ctx, task, (int)zt_arg_number(args_json, "max_turns", 12), &hook_error);
        if (result == NULL) { if (out != NULL) *out = zt_format("Error: %s", hook_error != NULL ? hook_error : "subagente falhou"); if (err != NULL) *err = zt_strdup("subagent failed"); }
        else *out = result;
        free(hook_error); free(task);
        return result != NULL;
    }
    if (strcmp(op, "skill") == 0) {
        char *name = NULL;
        zt_arg_string(args_json, "name", &name, "");
        if (name == NULL || *name == '\0') {
            char *index = zt_prompt_skills(h, 4000);
            if (out != NULL) *out = index != NULL ? index : zt_strdup("Error: nenhuma skill disponivel (diretorio skills/ vazio ou sem SKILL.md).");
            free(name);
            return index != NULL;
        }
        if (!skill_name_valid(name)) { if (out != NULL) *out = zt_strdup("Error: nome de skill invalido (sem separadores de caminho)."); free(name); return 0; }
        char *skill_path = zt_format("%s/%s/SKILL.md", h->skills_dir, name);
        char *content = zt_read_file(h, skill_path, 200000);
        int ok = content != NULL && strncmp(content, "Error:", 6) != 0;
        if (out != NULL) *out = ok ? content : zt_format("Error: skill '%s' nao encontrada em %s.", name, h->skills_dir);
        if (!ok && err != NULL) *err = zt_strdup("skill not found");
        if (!ok) free(content);
        free(skill_path); free(name);
        return ok;
    }
    if (out != NULL) *out = zt_format("Error: operacao desconhecida de agente: %s", op);
    return 0;
}
