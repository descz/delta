/* =========================================================================
 * memory_update
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/memory_update.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1332  ->  tool_memory_update
 * handler   : tool_memory_update
 * efeito    : WRITE_LOCAL
 * perfis    : full
 * -------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"id":{"type":"string"},"title":{"type":"string"},"content":{"type":"string"},"kind":{"type":"string"},"scope":{"type":"string"},"tags_json":{"type":"string"}},"required":["id","content"]}
 * -------------------------------------------------------------------------
 * Update an existing durable memory note: pass id (from memory_search/memory_list) plus the fields to change (title, content, kind, scope, tags_json). Fields omitted are preserved; append new information instead of replacing it.
 * =========================================================================
 */

static int tool_memory_update(void *context, const char *args, char **output, char **error) { ToolContext *ctx = (ToolContext *)context; char *id = NULL; char *title = NULL; char *content = NULL; char *kind = NULL; char *scope = NULL; char *tags = NULL; arg_string(args, "id", &id, ""); arg_string(args, "title", &title, NULL); arg_string(args, "content", &content, NULL); arg_string(args, "kind", &kind, NULL); arg_string(args, "scope", &scope, NULL); arg_string(args, "tags_json", &tags, NULL); int ok = zeno_memory_update_note(ctx->memory, id, title, content, kind, scope, tags); *output = ok ? zeno_format("Memory updated: %s", id) : zeno_strdup("Error: memory id not found (use memory_search to list ids) or invalid tags_json."); if (!ok && error != NULL) *error = zeno_strdup("memory update failed"); free(id); free(title); free(content); free(kind); free(scope); free(tags); return *output != NULL && ok; }
