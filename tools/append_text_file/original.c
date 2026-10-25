/* =========================================================================
 * append_text_file
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/append_text_file.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1319  ->  tool_append
 * handler   : tool_append
 * efeito    : WRITE_LOCAL
 * perfis    : full
 * -------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"relative_path":{"type":"string"},"content":{"type":"string"}},"required":["relative_path","content"]}
 * -------------------------------------------------------------------------
 * Append text to a file in the workspace.
 * =========================================================================
 */

static int tool_append(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; char *path = NULL; char *content = NULL; arg_string(args, "relative_path", &path, ""); arg_string(args, "content", &content, ""); if (!enforce_read_before_edit(ctx, path, output, error)) { free(path); free(content); return 0; } char *old_content = workspace_file_exists(tool_scope_root(ctx, path), path) ? workspace_read(tool_scope_root(ctx, path), path, 1024U * 1024U) : NULL; *output = workspace_write(tool_scope_root(ctx, path), path, content, 1); if (*output != NULL) { char *new_content = workspace_read(tool_scope_root(ctx, path), path, 1024U * 1024U); attach_change_summary(output, summarize_change(old_content != NULL ? old_content : "", new_content != NULL ? new_content : "")); free(new_content); } free(old_content); free(path); free(content); return *output != NULL; }
