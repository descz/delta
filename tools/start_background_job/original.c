/* =========================================================================
 * start_background_job
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/start_background_job.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1326  ->  tool_start_job
 * handler   : tool_start_job
 * efeito    : PROCESS   (requer aprovacao)
 * perfis    : full
 * -------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"command":{"type":"string"},"name":{"type":"string"}},"required":["command"]}
 * -------------------------------------------------------------------------
 * Start a tracked shell command.
 * =========================================================================
 */

static int tool_start_job(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; char *command = NULL; char *name = NULL; arg_string(args, "command", &command, ""); arg_string(args, "name", &name, ""); *output = zeno_sandbox_start_job(ctx->sandbox, command, name, ctx->workspace_root); free(command); free(name); return *output != NULL; }
