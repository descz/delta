/* =========================================================================
 * cancel_background_job
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/cancel_background_job.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1329  ->  tool_cancel_job
 * handler   : tool_cancel_job
 * efeito    : PROCESS   (requer aprovacao)
 * perfis    : full
 * -------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"job_id":{"type":"string"}},"required":["job_id"]}
 * -------------------------------------------------------------------------
 * Cancel a background job.
 * =========================================================================
 */

static int tool_cancel_job(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; char *id = NULL; arg_string(args, "job_id", &id, ""); *output = zeno_sandbox_cancel_job(ctx->sandbox, id); free(id); return *output != NULL; }
