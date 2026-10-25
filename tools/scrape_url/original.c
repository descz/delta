/* =========================================================================
 * scrape_url
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/scrape_url.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1354  ->  tool_scrape
 * handler   : tool_scrape
 * efeito    : READ_EXTERNAL   (requer aprovacao)
 * perfis    : full
 * -------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"url":{"type":"string"},"max_chars":{"type":"integer"}},"required":["url"]}
 * -------------------------------------------------------------------------
 * Fetch a URL and extract readable text (approval required).
 * =========================================================================
 */

static int tool_scrape(void *context, const char *args, char **output, char **error) {
    (void)context; (void)error; char *url = NULL; arg_string(args, "url", &url, ""); int ok = zeno_scrape_url(url, arg_seconds_ms(args, "timeout_seconds", 30, 900), arg_size_limit(args, "max_chars", 8000, 8U * 1024U * 1024U), output); free(url); return ok;
}
