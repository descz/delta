/* =========================================================================
 * update_plan
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/update_plan.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1186  ->  tool_update_plan
 * handler   : tool_update_plan
 * efeito    : WRITE_LOCAL
 * perfis    : full
 * -------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"plan_markdown":{"type":"string"}},"required":["plan_markdown"]}
 * -------------------------------------------------------------------------
 * Persist the live task plan as Markdown checklist lines ('- [ ] pending', '- [x] done'). Re-injected into your system prompt every turn.
 * =========================================================================
 */

static int tool_update_plan(void *context, const char *args, char **output, char **error) {
    (void)error;
    ToolContext *ctx = (ToolContext *)context;
    char *plan = NULL;
    arg_string(args, "plan_markdown", &plan, "");
    char *trimmed = plan != NULL ? zeno_trim_copy(plan) : NULL;
    if (trimmed == NULL || *trimmed == '\0') {
        *output = zeno_strdup("Error: update_plan requires a non-empty plan_markdown (one '- [ ]' line per step).");
        if (error != NULL) *error = zeno_strdup("invalid_plan");
        free(plan); free(trimmed);
        return 0;
    }
    size_t plan_length = strlen(trimmed);
    if (plan_length > 4000) plan_length = 4000;
    char *bounded = zeno_strndup(trimmed, plan_length);
    char *path = plan_file_path(ctx);
    int ok = bounded != NULL && path != NULL && zeno_write_file_atomic(path, bounded);
    *output = ok
        ? zeno_format("Plan saved (%zu characters). It is injected into your system prompt every turn; keep it current.", plan_length)
        : zeno_strdup("Error: could not persist the plan file.");
    if (!ok && error != NULL) *error = zeno_strdup("plan_persist_failed");
    free(bounded); free(path); free(plan); free(trimmed);
    return ok;
}
