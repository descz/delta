/* =========================================================================
 * assert_true
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/assert_true.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1335  ->  tool_assert_true
 * handler   : tool_assert_true
 * efeito    : READ_LOCAL
 * perfis    : full
 * -------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"condition_description":{"type":"string"},"value":{"type":"string"},"expected":{"type":"string"}},"required":["condition_description","value","expected"]}
 * -------------------------------------------------------------------------
 * Assert exact equality.
 * =========================================================================
 */

static int tool_assert_true(void *context, const char *args, char **output, char **error) { (void)context; (void)error; char *description = NULL; char *value = NULL; char *expected = NULL; arg_string(args, "condition_description", &description, "assertion"); arg_string(args, "value", &value, ""); arg_string(args, "expected", &expected, ""); *output = zeno_assert_true(description, value, expected); free(description); free(value); free(expected); return *output != NULL; }
