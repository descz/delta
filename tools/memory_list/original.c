/* =========================================================================
 * memory_list
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/memory_list.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1334  ->  tool_memory_list
 * handler   : tool_memory_list
 * efeito    : READ_LOCAL
 * perfis    : full
 * -------------------------------------------------------------------------
 * schema    : {"type":"object"}
 * -------------------------------------------------------------------------
 * List durable memories.
 * =========================================================================
 */

static int tool_memory_list(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; char *scope = NULL; arg_string(args, "scope", &scope, ""); *output = zeno_memory_list_notes(ctx->memory, scope, arg_size_limit(args, "limit", 50, 1000)); free(scope); return *output != NULL; }
