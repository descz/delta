/* =========================================================================
 * spawn_subagent
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/spawn_subagent.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1235  ->  tool_spawn_subagent
 * handler   : tool_spawn_subagent
 * efeito    : READ_LOCAL
 * perfis    : full
 * -------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"task":{"type":"string"},"max_turns":{"type":"integer"}},"required":["task"]}
 * -------------------------------------------------------------------------
 * Run an isolated read-only subagent on a self-contained research/analysis task and return its final report. Subagents cannot write files or execute commands.
 * =========================================================================
 */

static int tool_spawn_subagent(void *context, const char *args, char **output, char **error) {
    ToolContext *ctx = (ToolContext *)context;
    ZenoAgent *parent = (ZenoAgent *)ctx->bound_agent;
    char *task = NULL;
    arg_string(args, "task", &task, "");
    int max_turns = (int)arg_size_limit(args, "max_turns", 6, 12);
    char *trimmed_task = task != NULL ? zeno_trim_copy(task) : NULL;
    if (parent == NULL || trimmed_task == NULL || *trimmed_task == '\0') {
        *output = zeno_strdup("Error: spawn_subagent requires a bound runtime and a non-empty task.");
        if (error != NULL) *error = zeno_strdup("subagent_unavailable");
        free(task); free(trimmed_task);
        return 0;
    }
    zeno_mutex_lock(&ctx->lock);
    if (ctx->subagent_depth >= 1) {
        zeno_mutex_unlock(&ctx->lock);
        *output = zeno_strdup("Error: nested subagents are not allowed; finish your own scope instead.");
        if (error != NULL) *error = zeno_strdup("subagent_nested");
        free(task); free(trimmed_task);
        return 0;
    }
    ctx->subagent_depth++;
    unsigned long sequence = ++ctx->subagent_seq;
    zeno_mutex_unlock(&ctx->lock);

    ZenoAgentOptions options;
    memset(&options, 0, sizeof(options));
    options.registry = parent->registry;
    options.router = parent->router;
    options.memory = parent->memory;
    options.sandbox = parent->sandbox;
    options.approval = parent->approval;
    options.model = parent->model;
    options.runs_dir = parent->runs_dir;
    options.require_approval = 1;
    options.mode = parent->options.mode;
    ZenoAgent *subagent = zeno_agent_create(&options);
    ZenoRunOptions run_options;
    memset(&run_options, 0, sizeof(run_options));
    run_options.model = parent->model;
    run_options.max_turns = max_turns;
    run_options.max_tokens = 2048;
    run_options.temperature = 0.2;
    run_options.approve = deny_all_approvals;
    char *session_id = zeno_format("session-sub-%lu", sequence);
    ZenoAgentResult result = zeno_agent_run(subagent, session_id != NULL ? session_id : "session-sub", trimmed_task, &run_options);

    size_t response_length = result.response != NULL ? strlen(result.response) : 0;
    if (response_length > 6000) response_length = 6000;
    *output = zeno_format("{\"status\":\"%s\",\"response\":\"%.*s\"}",
                          result.status != NULL ? result.status : "failed",
                          (int)response_length, result.response != NULL ? result.response : "");
    int ok = *output != NULL && result.status != NULL && strcmp(result.status, "completed") == 0;
    if (!ok && error != NULL) *error = zeno_strdup(result.status != NULL ? result.status : "failed");

    zeno_agent_result_free(&result);
    zeno_agent_destroy(subagent);
    /* The shared registry context must point back at the parent runtime;
     * creating the subagent rebound it. */
    zeno_tools_bind_agent(ctx, parent);
    free(session_id);
    free(task); free(trimmed_task);
    zeno_mutex_lock(&ctx->lock);
    ctx->subagent_depth--;
    zeno_mutex_unlock(&ctx->lock);
    return ok;
}
