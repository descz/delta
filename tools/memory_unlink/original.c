/* =========================================================================
 * memory_unlink
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/memory_unlink.c        (fragmento extraido, nao standalone)
 * origem    : zeno-bridge.c:549  ->  bridge_memory_unlink_tool
 * handler   : bridge_memory_unlink_tool
 * efeito    : WRITE_LOCAL
 * perfis    : bridge
 * -------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"from":{"type":"string"},"to":{"type":"string"}},"required":["from","to"]}
 * -------------------------------------------------------------------------
 * Remove a link between two memory notes (from/to are note ids from memory_search or memory_list). Use when a relation is wrong/obsolete or the user asks to disconnect two memories; the notes themselves are kept.
 * =========================================================================
 */

static int bridge_memory_unlink_tool(void *context, const char *args_json, char **output, char **error) {
    (void)context;
    char *parse_error = NULL;
    ZjNode *root = zj_parse(args_json != NULL ? args_json : "{}", &parse_error);
    free(parse_error);
    const char *from = root != NULL ? zj_string(zj_object_get(root, "from")) : NULL;
    const char *to = root != NULL ? zj_string(zj_object_get(root, "to")) : NULL;
    if (from == NULL || *from == '\0' || to == NULL || *to == '\0') {
        if (output != NULL) *output = zeno_strdup("Error: from and to note ids are required.");
        if (error != NULL) *error = zeno_strdup("invalid unlink arguments");
        zj_free(root);
        return 0;
    }
    char *request = zeno_format("{\"from\":%s,\"to\":%s}", zeno_json_escape(from), zeno_json_escape(to));
    zj_free(root);
    if (request == NULL) {
        if (output != NULL) *output = zeno_strdup("Error: out of memory.");
        if (error != NULL) *error = zeno_strdup("unlink failed");
        return 0;
    }
    char *link_error = NULL;
    char *result = zeno_bridge_memory_link_delete(request, &link_error);
    free(request);
    if (result == NULL) {
        if (output != NULL) *output = zeno_format("Error: %s", link_error != NULL ? link_error : "could not remove link");
        if (error != NULL) *error = zeno_strdup("unlink failed");
        free(link_error);
        return 0;
    }
    if (output != NULL) *output = zeno_format("Unlinked %s -> %s", from, to);
    free(result);
    free(link_error);
    return 1;
}
