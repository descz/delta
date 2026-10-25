/* =========================================================================
 * list_background_jobs
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/list_background_jobs.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1327  ->  tool_list_jobs
 * handler   : tool_list_jobs
 * efeito    : READ_LOCAL
 * perfis    : full
 * -------------------------------------------------------------------------
 * schema    : {"type":"object"}
 * -------------------------------------------------------------------------
 * List tracked background jobs.
 * =========================================================================
 */

static int tool_list_jobs(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; *output = zeno_sandbox_list_jobs(ctx->sandbox, zeno_json_get_bool(args, "include_completed", 1)); return *output != NULL; }
