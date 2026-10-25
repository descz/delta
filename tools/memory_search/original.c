/* =========================================================================
 * memory_search
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/memory_search.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1333  ->  tool_memory_search
 * handler   : tool_memory_search
 * efeito    : READ_LOCAL
 * perfis    : full
 * -------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"query":{"type":"string"},"limit":{"type":"integer"}},"required":["query"]}
 * -------------------------------------------------------------------------
 * Search durable memories - run this at the start of every user request and whenever the user references past context.
 * =========================================================================
 */

static int tool_memory_search(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; char *query = NULL; arg_string(args, "query", &query, ""); *output = zeno_memory_search_notes(ctx->memory, query, arg_size_limit(args, "limit", 20, 1000)); free(query); return *output != NULL; }
