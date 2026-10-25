/* =========================================================================
 * memory_remember
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/memory_remember.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1330  ->  tool_memory_remember
 * handler   : tool_memory_remember
 * efeito    : WRITE_LOCAL
 * perfis    : full
 * -------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"title":{"type":"string"},"content":{"type":"string"},"kind":{"type":"string"},"scope":{"type":"string"},"tags_json":{"type":"string"}},"required":["title","content"]}
 * -------------------------------------------------------------------------
 * Persist a durable memory note as a diary-style Markdown entry (see MEMORY WRITING FORMAT).
 * =========================================================================
 */

static int tool_memory_remember(void *context, const char *args, char **output, char **error) { ToolContext *ctx = (ToolContext *)context; char *title = NULL; char *content = NULL; char *kind = NULL; char *scope = NULL; char *tags = NULL; arg_string(args, "title", &title, ""); arg_string(args, "content", &content, ""); arg_string(args, "kind", &kind, "note"); arg_string(args, "scope", &scope, "global"); arg_string(args, "tags_json", &tags, "[]"); char *id = NULL; int ok = zeno_memory_add_note(ctx->memory, title, content, kind, scope, tags, &id); *output = ok ? zeno_format("Memory stored: %s", id != NULL ? id : "unknown") : zeno_strdup("Error: tags_json must be a JSON string array."); if (!ok && error != NULL) *error = zeno_strdup("invalid memory note"); free(title); free(content); free(kind); free(scope); free(tags); free(id); return *output != NULL && ok; }
