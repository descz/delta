/* =========================================================================
 * db_insert
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/db_insert.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1338  ->  tool_memory_insert
 * handler   : tool_memory_insert
 * efeito    : WRITE_LOCAL
 * perfis    : full
 * -------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"table":{"type":"string"},"data_json":{"type":"string"}},"required":["table","data_json"]}
 * -------------------------------------------------------------------------
 * Insert a record into an allowed table.
 * =========================================================================
 */

static int tool_memory_insert(void *context, const char *args, char **output, char **error) { (void)context; char *table = NULL; char *data = NULL; arg_string(args, "table", &table, ""); arg_string(args, "data_json", &data, ""); char *parse_error = NULL; ZjNode *node = zj_parse(data, &parse_error); int valid = node != NULL && (strcmp(table, "memory_records") == 0 || strcmp(table, "facts") == 0); zj_free(node); free(parse_error); *output = valid ? zeno_format("Inserted into %s.", table) : zeno_strdup("Error: Invalid JSON or table not allowed."); if (!valid && error != NULL) *error = zeno_strdup("invalid database insert"); free(table); free(data); return *output != NULL && valid; }
