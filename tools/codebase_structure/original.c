/* =========================================================================
 * codebase_structure
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/codebase_structure.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1426  ->  tool_codebase
 * handler   : tool_codebase
 * efeito    : READ_LOCAL
 * perfis    : full
 * -------------------------------------------------------------------------
 * schema    : {"type":"object"}
 * -------------------------------------------------------------------------
 * Scan code structure.
 * =========================================================================
 */

static int tool_codebase(void *context, const char *args, char **output, char **error) { (void)args; (void)error; ToolContext *ctx = (ToolContext *)context; *output = zeno_codebase_structure(ctx->workspace_root); return *output != NULL; }
