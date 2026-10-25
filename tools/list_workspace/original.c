/* =========================================================================
 * list_workspace
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/list_workspace.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1316  ->  tool_list
 * handler   : tool_list
 * efeito    : READ_LOCAL
 * perfis    : full
 * -------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"relative_path":{"type":"string"}}}
 * -------------------------------------------------------------------------
 * List files and folders inside the workspace.
 * =========================================================================
 */

static int tool_list(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; char *relative = NULL; size_t max = arg_size_limit(args, "max_entries", 100, 10000); arg_string(args, "relative_path", &relative, "."); *output = workspace_list(tool_scope_root(ctx, relative), relative, max); free(relative); return *output != NULL; }
