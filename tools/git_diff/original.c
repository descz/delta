/* =========================================================================
 * git_diff
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/git_diff.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1423  ->  tool_git_diff
 * handler   : tool_git_diff
 * efeito    : READ_LOCAL
 * perfis    : full
 * -------------------------------------------------------------------------
 * schema    : {"type":"object"}
 * -------------------------------------------------------------------------
 * Show the current Git diff.
 * =========================================================================
 */

static int tool_git_diff(void *context, const char *args, char **output, char **error) { (void)args; (void)error; ToolContext *ctx = (ToolContext *)context; *output = zeno_git_diff(ctx->workspace_root); return *output != NULL; }
