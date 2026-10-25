/* =========================================================================
 * http_request
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/http_request.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1340  ->  tool_http
 * handler   : tool_http
 * efeito    : WRITE_EXTERNAL   (requer aprovacao)
 * perfis    : full
 * -------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"url":{"type":"string"},"method":{"type":"string"},"headers":{"type":"object"},"json_body":{},"text_body":{"type":"string"},"timeout_seconds":{"type":"integer"},"max_response_chars":{"type":"integer"}},"required":["url"]}
 * -------------------------------------------------------------------------
 * Perform an HTTP request.
 * =========================================================================
 */

static int tool_http(void *context, const char *args, char **output, char **error) {
    (void)context;
    char *url = NULL; char *method = NULL; char *text_body = NULL;
    arg_string(args, "url", &url, ""); arg_string(args, "method", &method, "GET"); arg_string(args, "text_body", &text_body, "");
    char *parse_error = NULL; ZjNode *root = zj_parse(args != NULL ? args : "{}", &parse_error); free(parse_error);
    char *headers = root != NULL ? zj_stringify_compact(zj_object_get(root, "headers")) : NULL;
    char *json_body = root != NULL && zj_object_has(root, "json_body") ? zj_stringify_compact(zj_object_get(root, "json_body")) : NULL;
    int ok = json_body == NULL || text_body == NULL || *text_body == '\0';
    if (!ok) { if (error != NULL) *error = zeno_strdup("json_body and text_body are mutually exclusive"); *output = zeno_strdup("Error: provide json_body OR text_body, not both."); }
    else ok = zeno_http_request(url, method, headers, json_body != NULL ? json_body : (*text_body != '\0' ? text_body : NULL), arg_seconds_ms(args, "timeout_seconds", 60, 900), arg_size_limit(args, "max_response_chars", 8000, 16U * 1024U * 1024U), output);
    if (!ok && *output == NULL) *output = zeno_strdup("Request error.");
    zj_free(root); free(url); free(method); free(text_body); free(headers); free(json_body); return ok;
}
