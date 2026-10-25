/* =========================================================================
 * assert_contains
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/assert_contains.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1336  ->  tool_assert_contains
 * handler   : tool_assert_contains
 * efeito    : READ_LOCAL
 * perfis    : full
 * -------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"condition_description":{"type":"string"},"value":{"type":"string"},"substring":{"type":"string"}},"required":["condition_description","value","substring"]}
 * -------------------------------------------------------------------------
 * Assert substring containment.
 * =========================================================================
 */

static int tool_assert_contains(void *context, const char *args, char **output, char **error) { (void)context; (void)error; char *description = NULL; char *value = NULL; char *substring = NULL; arg_string(args, "condition_description", &description, "assertion"); arg_string(args, "value", &value, ""); arg_string(args, "substring", &substring, ""); *output = zeno_assert_contains(description, value, substring); free(description); free(value); free(substring); return *output != NULL; }
