/* =========================================================================
 * git_checkpoint
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/git_checkpoint.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1425  ->  tool_git_checkpoint
 * handler   : tool_git_checkpoint
 * efeito    : PROCESS   (requer aprovacao)
 * perfis    : full
 * -------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"message":{"type":"string"}}}
 * -------------------------------------------------------------------------
 * Create a Git checkpoint.
 * =========================================================================
 */

static int tool_git_checkpoint(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; char *message = NULL; arg_string(args, "message", &message, "Zeno checkpoint"); *output = zeno_git_checkpoint(ctx->workspace_root, message); free(message); return *output != NULL; }
