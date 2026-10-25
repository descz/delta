/* =========================================================================
 * delete_file
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/delete_file.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1322  ->  tool_delete
 * handler   : tool_delete
 * efeito    : WRITE_LOCAL
 * perfis    : full, minimal
 * -------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"relative_path":{"type":"string"}},"required":["relative_path"]}
 * -------------------------------------------------------------------------
 * Delete a workspace file you created and no longer need. The previous version is backed up to .Zeno_trash automatically, but verify nothing still references the file before deleting it.
 * =========================================================================
 */

static int tool_delete(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; char *relative = NULL; arg_string(args, "relative_path", &relative, ""); *output = workspace_delete(tool_scope_root(ctx, relative), relative); free(relative); return *output != NULL && strncmp(*output, "Error", 5) != 0 && strncmp(*output, "File not found", 14) != 0; }
