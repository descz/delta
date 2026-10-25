/* =========================================================================
 * create_directory
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/create_directory.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1321  ->  tool_mkdir
 * handler   : tool_mkdir
 * efeito    : WRITE_LOCAL
 * perfis    : full
 * -------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"relative_path":{"type":"string"}},"required":["relative_path"]}
 * -------------------------------------------------------------------------
 * Create a folder inside the workspace.
 * =========================================================================
 */

static int tool_mkdir(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; char *relative = NULL; arg_string(args, "relative_path", &relative, ""); char *path = workspace_resolve(tool_scope_root(ctx, relative), relative); int ok = path != NULL && zeno_mkdirs(path); *output = ok ? zeno_format("Directory created: %s", relative) : zeno_strdup("Error creating directory."); free(relative); free(path); return *output != NULL; }
