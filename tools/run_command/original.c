/* =========================================================================
 * run_command
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/run_command.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1325  ->  tool_run_command
 * handler   : tool_run_command
 * efeito    : PROCESS   (requer aprovacao)
 * perfis    : full, minimal
 * -------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"command":{"type":"string"},"timeout_seconds":{"type":"integer"}},"required":["command"]}
 * -------------------------------------------------------------------------
 * Execute a shell command in the workspace sandbox.
 * =========================================================================
 */

static int tool_run_command(void *context, const char *args, char **output, char **error) { ToolContext *ctx = (ToolContext *)context; char *command = NULL; arg_string(args, "command", &command, ""); int timeout_ms = arg_seconds_ms(args, "timeout_seconds", 120, 900); ZenoExecResult result = zeno_sandbox_execute(ctx->sandbox, command, ctx->workspace_root, timeout_ms); int ok = result.ok; *output = ok ? zeno_strdup(result.output) : zeno_format("%s%s%s", result.blocked ? "SANDBOX BLOCKED: " : "Error: ", result.reason != NULL ? result.reason : "command failed", result.output != NULL && *result.output != '\0' ? "\n" : ""); if (*output != NULL && result.output != NULL && !ok && *result.output != '\0') { char *next = zeno_format("%s%s", *output, result.output); free(*output); *output = next; } if (!ok && error != NULL) *error = zeno_strdup(result.reason != NULL ? result.reason : "command failed"); zeno_exec_result_free(&result); free(command); return *output != NULL && ok; }
