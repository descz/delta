/* =========================================================================
 * write_text_file
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/write_text_file.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1318  ->  tool_write
 * handler   : tool_write
 * efeito    : WRITE_LOCAL
 * perfis    : full, minimal
 * -------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"relative_path":{"type":"string"},"content":{"type":"string"}},"required":["relative_path","content"]}
 * -------------------------------------------------------------------------
 * Create or overwrite a text file in the workspace.
 * =========================================================================
 */

static int tool_write(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; char *path = NULL; char *content = NULL; arg_string(args, "relative_path", &path, ""); arg_string(args, "content", &content, ""); if (!enforce_read_before_edit(ctx, path, output, error)) { free(path); free(content); return 0; } int existed = workspace_file_exists(tool_scope_root(ctx, path), path); char *old_content = existed ? workspace_read(tool_scope_root(ctx, path), path, 1024U * 1024U) : NULL; *output = workspace_write(tool_scope_root(ctx, path), path, content, 0); if (*output != NULL) attach_change_summary(output, existed ? summarize_change(old_content != NULL ? old_content : "", content != NULL ? content : "") : zeno_format("[change] created file (%zu bytes)", content != NULL ? strlen(content) : 0)); free(old_content); free(path); free(content); return *output != NULL; }
