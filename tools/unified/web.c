/* ============================================================================
 * web — TOOL UNIFICADA (substitui 3 tools granulares)
 * ----------------------------------------------------------------------------
 * ops      : http, scrape, browser
 * cobre    : http_request, scrape_url, browser_navigate
 * handler  : zt_tool_web_unified
 * motor    : zt_web_op()
 * ========================================================================= */

/* --- schema (use em zt_register_unified ou no seu proprio registro) --- */
/* schema da tool "web": ver ZenoTools/harness/src/zt_register.c (tabela UNIFIED_TOOLS) */

/* --- handler unificado --- */
int zt_tool_web_unified(zt_harness *h, const char *args_json, char **out, char **err) {
    char *op = NULL;
    if (!zt_arg_string(args_json, "op", &op, NULL) || op == NULL) {
        if (out != NULL) *out = zt_strdup("Error: 'op' e obrigatorio (http|scrape|browser).");
        return 0;
    }
    int ok = zt_web_op(h, op, args_json, out, err);
    free(op);
    return ok;
}

/* --- motor compartilhado com as tools granulares --- */
int zt_web_op(zt_harness *h, const char *op, const char *args_json, char **out, char **err) {
    if (h == NULL || op == NULL) return 0;
    char *url = NULL;
    zt_arg_string(args_json, "url", &url, "");
    if (url == NULL || *url == '\0') { if (out != NULL) *out = zt_strdup("Error: 'url' e obrigatorio."); return 0; }
    if (strcmp(op, "http") == 0) {
        if (h->host.http == NULL) { if (out != NULL) *out = zt_strdup("Error: hook host.http nao configurado."); free(url); return 0; }
        char *method = NULL, *headers = NULL, *text_body = NULL;
        zt_arg_string(args_json, "method", &method, "GET");
        zt_arg_string(args_json, "headers", &headers, NULL);
        zt_arg_string(args_json, "text_body", &text_body, NULL);
        char *json_body_text = NULL;
        char *body = text_body;
        int body_is_json = 0;
        {
            char *parse_error = NULL;
            zt_json *root = zt_json_parse(args_json != NULL ? args_json : "{}", &parse_error);
            free(parse_error);
            zt_json *json_body = root != NULL ? zt_json_object_get(root, "json_body") : NULL;
            if (json_body != NULL && json_body->type != ZT_JSON_NULL) { json_body_text = zt_json_dump(json_body); body = json_body_text; body_is_json = 1; }
            zt_json_free(root);
        }
        char *hook_error = NULL;
        char *result = h->host.http(h->host.ctx, method, url, headers, body, body_is_json,
                                    zt_arg_seconds_ms(args_json, "timeout_seconds", 30, 300),
                                    zt_arg_size(args_json, "max_response_chars", 200000, 2U * 1024U * 1024U), &hook_error);
        if (result == NULL) {
            if (out != NULL) *out = zt_format("Error: %s", hook_error != NULL ? hook_error : "falha na requisicao HTTP");
            if (err != NULL) *err = zt_strdup("http failed");
        } else *out = result;
        free(hook_error); free(method); free(headers); free(text_body); free(json_body_text); free(url);
        return result != NULL;
    }
    if (strcmp(op, "scrape") == 0 || strcmp(op, "browser") == 0) {
        char *hook_error = NULL;
        char *result = web_fetch_text(h, url, zt_arg_size(args_json, "max_chars", 20000, 2U * 1024U * 1024U), &hook_error);
        if (result == NULL) {
            if (out != NULL) *out = zt_format("Error: %s", hook_error != NULL ? hook_error : "falha ao ler a pagina");
            if (err != NULL) *err = zt_strdup("fetch failed");
        } else *out = result;
        free(hook_error); free(url);
        return result != NULL;
    }
    if (out != NULL) *out = zt_format("Error: operacao desconhecida de web: %s", op);
    free(url);
    return 0;
}

/* --- tools granulares cobertas --- */
/* http_request: zt_tool_http_request() -> zt_invoke(h, "http_request", args) */
/* scrape_url: zt_tool_scrape_url() -> zt_invoke(h, "scrape_url", args) */
/* browser_navigate: zt_tool_browser_navigate() -> zt_invoke(h, "browser_navigate", args) */
