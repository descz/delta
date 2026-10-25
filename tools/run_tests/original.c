/* =========================================================================
 * run_tests
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/run_tests.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1428  ->  tool_tests
 * handler   : tool_tests
 * efeito    : PROCESS   (requer aprovacao)
 * perfis    : full
 * -------------------------------------------------------------------------
 * schema    : {"type":"object"}
 * -------------------------------------------------------------------------
 * Detect the workspace test framework.
 * =========================================================================
 */

static int tool_tests(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; *output = zeno_run_tests_execute(ctx->workspace_root, arg_seconds_ms(args, "timeout_seconds", 120, 900)); return *output != NULL; }
