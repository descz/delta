/* =========================================================================
 * mcp_call
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/mcp_call.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1362  ->  tool_mcp
 * handler   : tool_mcp
 * efeito    : WRITE_EXTERNAL
 * perfis    : full, minimal
 * -------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"server_url":{"type":"string"},"command":{"type":"string","description":"stdio server command line"},"headers":{"type":"object","description":"HTTP headers for server_url; ${VAR} expands from the environment"},"tool_name":{"type":"string"},"args_json":{"type":"string"}},"required":["tool_name"]}
 * -------------------------------------------------------------------------
 * Call a tool on an external MCP server: pass server_url for HTTP JSON-RPC servers or command for stdio servers (e.g. "node server.js"); args_json carries the tool arguments; headers carries HTTP auth headers for server_url (e.g. {"X-API-KEY":"${COMPOSIO_API_KEY}"} - ${VAR} is read from the environment, never paste secrets literally).
 * =========================================================================
 */

static int tool_mcp(void *context, const char *args, char **output, char **error) {
    (void)context; char *server = NULL; char *command = NULL; char *tool_name = NULL; char *tool_args = NULL;
    arg_string(args, "server_url", &server, ""); arg_string(args, "command", &command, ""); arg_string(args, "tool_name", &tool_name, ""); arg_string(args, "args_json", &tool_args, "{}");
    char *headers = NULL;
    char *headers_error = NULL;
    ZjNode *root = zj_parse(args != NULL ? args : "{}", &headers_error);
    free(headers_error);
    if (root != NULL && zj_object_has(root, "headers")) headers = zj_stringify_compact(zj_object_get(root, "headers"));
    zj_free(root);
    if (headers != NULL && strcmp(headers, "null") == 0) { free(headers); headers = NULL; }
    if (command != NULL && *command != '\0') *output = zeno_mcp_call_stdio(command, tool_name, tool_args);
    else *output = zeno_mcp_call(server, tool_name, tool_args, headers);
    int ok = *output != NULL && strncmp(*output, "MCP error", 9) != 0;
    if (!ok && error != NULL) *error = zeno_strdup("mcp call failed");
    free(server); free(command); free(tool_name); free(tool_args); free(headers); return ok;
}
