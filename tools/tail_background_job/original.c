/* =========================================================================
 * tail_background_job
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/tail_background_job.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1328  ->  tool_tail_job
 * handler   : tool_tail_job
 * efeito    : READ_LOCAL
 * perfis    : full
 * -------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"job_id":{"type":"string"},"max_lines":{"type":"integer"}},"required":["job_id"]}
 * -------------------------------------------------------------------------
 * Read background job output.
 * =========================================================================
 */

static int tool_tail_job(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; char *id = NULL; arg_string(args, "job_id", &id, ""); *output = zeno_sandbox_tail_job(ctx->sandbox, id, arg_size_limit(args, "max_lines", 100, 10000)); free(id); return *output != NULL; }
