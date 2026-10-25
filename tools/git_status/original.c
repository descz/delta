/* =========================================================================
 * git_status
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/git_status.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1422  ->  tool_git_status
 * handler   : tool_git_status
 * efeito    : READ_LOCAL
 * perfis    : full
 * -------------------------------------------------------------------------
 * schema    : {"type":"object"}
 * -------------------------------------------------------------------------
 * Show Git status.
 * =========================================================================
 */

static int tool_git_status(void *context, const char *args, char **output, char **error) { (void)args; (void)error; ToolContext *ctx = (ToolContext *)context; *output = zeno_git_status(ctx->workspace_root); return *output != NULL; }
