/* =========================================================================
 * find_symbol
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/find_symbol.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1427  ->  tool_symbol
 * handler   : tool_symbol
 * efeito    : READ_LOCAL
 * perfis    : full
 * -------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"name":{"type":"string"}},"required":["name"]}
 * -------------------------------------------------------------------------
 * Find code symbols by name.
 * =========================================================================
 */

static int tool_symbol(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; char *name = NULL; arg_string(args, "name", &name, ""); *output = zeno_find_symbol(ctx->workspace_root, name); free(name); return *output != NULL; }
