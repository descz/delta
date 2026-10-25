/* =========================================================================
 * read_text_file
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/read_text_file.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1317  ->  tool_read
 * handler   : tool_read
 * efeito    : READ_LOCAL
 * perfis    : full, minimal
 * -------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"relative_path":{"type":"string"},"max_chars":{"type":"integer"}},"required":["relative_path"]}
 * -------------------------------------------------------------------------
 * Read a UTF-8 text file from the workspace.
 * =========================================================================
 */

static int tool_read(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; char *path = NULL; size_t max = arg_size_limit(args, "max_chars", 50000, 2U * 1024U * 1024U); arg_string(args, "relative_path", &path, ""); *output = workspace_read(tool_scope_root(ctx, path), path, max); if (*output != NULL && strncmp(*output, "Error:", 6) != 0 && path != NULL && *path != '\0') { zeno_mutex_lock(&ctx->lock); ledger_record(ctx, path); zeno_mutex_unlock(&ctx->lock); } free(path); return *output != NULL; }
