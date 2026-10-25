/* =========================================================================
 * db_query
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/db_query.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1337  ->  tool_memory_query
 * handler   : tool_memory_query
 * efeito    : READ_LOCAL
 * perfis    : full
 * -------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"sql":{"type":"string"}},"required":["sql"]}
 * -------------------------------------------------------------------------
 * Run a read-only memory query.
 * =========================================================================
 */

static int tool_memory_query(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; char *query = NULL; arg_string(args, "sql", &query, ""); if (zeno_contains_ci(query, "insert") || zeno_contains_ci(query, "update") || zeno_contains_ci(query, "delete") || zeno_contains_ci(query, "drop") || zeno_contains_ci(query, "alter") || zeno_contains_ci(query, "create")) *output = zeno_strdup("Error: Only SELECT queries allowed."); else *output = zeno_memory_search_notes(ctx->memory, "", 50); free(query); return *output != NULL; }
