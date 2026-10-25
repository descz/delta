/* =========================================================================
 * search_workspace
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/search_workspace.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1323  ->  tool_search
 * handler   : tool_search
 * efeito    : READ_LOCAL
 * perfis    : full
 * -------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"query":{"type":"string"},"relative_path":{"type":"string"},"max_results":{"type":"integer"}},"required":["query"]}
 * -------------------------------------------------------------------------
 * Search text across workspace files.
 * =========================================================================
 */

static int tool_search(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; char *query = NULL; char *relative = NULL; size_t max = arg_size_limit(args, "max_results", 50, 1000); arg_string(args, "query", &query, ""); arg_string(args, "relative_path", &relative, "."); *output = workspace_search(tool_scope_root(ctx, relative), query, relative, max); free(query); free(relative); return *output != NULL; }
