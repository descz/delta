/* =========================================================================
 * glob_workspace
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/glob_workspace.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1324  ->  tool_glob
 * handler   : tool_glob
 * efeito    : READ_LOCAL
 * perfis    : full
 * -------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"pattern":{"type":"string"},"relative_path":{"type":"string"},"max_results":{"type":"integer"}},"required":["pattern"]}
 * -------------------------------------------------------------------------
 * Return workspace paths matching a glob pattern.
 * =========================================================================
 */

static int tool_glob(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; char *pattern = NULL; char *relative = NULL; size_t max = arg_size_limit(args, "max_results", 100, 1000); arg_string(args, "pattern", &pattern, "*"); arg_string(args, "relative_path", &relative, "."); *output = workspace_glob(tool_scope_root(ctx, relative), pattern, relative, max); free(pattern); free(relative); return *output != NULL; }
