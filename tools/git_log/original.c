/* =========================================================================
 * git_log
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/git_log.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1424  ->  tool_git_log
 * handler   : tool_git_log
 * efeito    : READ_LOCAL
 * perfis    : full
 * -------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"n":{"type":"integer"}}}
 * -------------------------------------------------------------------------
 * Show recent Git commits.
 * =========================================================================
 */

static int tool_git_log(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; *output = zeno_git_log(ctx->workspace_root, (int)arg_size_limit(args, "n", 10, 1000)); return *output != NULL; }
