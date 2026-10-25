/* =========================================================================
 * browser_navigate
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/browser_navigate.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1358  ->  tool_browser_navigate
 * handler   : tool_browser_navigate
 * efeito    : BROWSER   (requer aprovacao)
 * perfis    : full
 * -------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"url":{"type":"string"}},"required":["url"]}
 * -------------------------------------------------------------------------
 * Navigate a lightweight external reader.
 * =========================================================================
 */

static int tool_browser_navigate(void *context, const char *args, char **output, char **error) {
    return tool_scrape(context, args, output, error);
}
