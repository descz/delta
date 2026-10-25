/* =========================================================================
 * replace_in_file
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/replace_in_file.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1320  ->  tool_replace
 * handler   : tool_replace
 * efeito    : WRITE_LOCAL
 * perfis    : full, minimal
 * -------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"relative_path":{"type":"string"},"old_text":{"type":"string"},"new_text":{"type":"string"},"replace_all":{"type":"boolean"}},"required":["relative_path","old_text","new_text"]}
 * -------------------------------------------------------------------------
 * Replace exact text inside a workspace file.
 * =========================================================================
 */

static int tool_replace(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; char *path = NULL; char *old_text = NULL; char *new_text = NULL; arg_string(args, "relative_path", &path, ""); arg_string(args, "old_text", &old_text, ""); arg_string(args, "new_text", &new_text, ""); if (!enforce_read_before_edit(ctx, path, output, error)) { free(path); free(old_text); free(new_text); return 0; } char *before_content = workspace_file_exists(tool_scope_root(ctx, path), path) ? workspace_read(tool_scope_root(ctx, path), path, 1024U * 1024U) : NULL; *output = workspace_replace(tool_scope_root(ctx, path), path, old_text, new_text, zeno_json_get_bool(args, "replace_all", 1)); if (*output != NULL && strncmp(*output, "Error", 5) != 0) { char *after_content = workspace_read(tool_scope_root(ctx, path), path, 1024U * 1024U); attach_change_summary(output, summarize_change(before_content != NULL ? before_content : "", after_content != NULL ? after_content : "")); free(after_content); } free(before_content); free(path); free(old_text); free(new_text); return *output != NULL; }
