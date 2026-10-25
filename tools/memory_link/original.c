/* =========================================================================
 * memory_link
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/memory_link.c        (fragmento extraido, nao standalone)
 * origem    : zeno-bridge.c:493  ->  bridge_memory_link_tool
 * handler   : bridge_memory_link_tool
 * efeito    : WRITE_LOCAL
 * perfis    : bridge
 * -------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"from":{"type":"string"},"to":{"type":"string"},"label":{"type":"string"}},"required":["from","to"]}
 * -------------------------------------------------------------------------
 * Link two durable memory notes in the workspace knowledge graph (from/to are note ids returned by memory_remember or memory_list).
 * =========================================================================
 */

static int bridge_memory_link_tool(void *context, const char *args_json, char **output, char **error) {
    (void)context;
    char *parse_error = NULL;
    ZjNode *root = zj_parse(args_json != NULL ? args_json : "{}", &parse_error);
    free(parse_error);
    const char *from = root != NULL ? zj_string(zj_object_get(root, "from")) : NULL;
    const char *to = root != NULL ? zj_string(zj_object_get(root, "to")) : NULL;
    const char *label = root != NULL ? zj_string(zj_object_get(root, "label")) : NULL;
    if (from == NULL || *from == '\0' || to == NULL || *to == '\0') {
        if (output != NULL) *output = zeno_strdup("Error: from and to note ids are required.");
        if (error != NULL) *error = zeno_strdup("invalid link arguments");
        zj_free(root);
        return 0;
    }
    zeno_mutex_lock(&g_bridge.lock);
    char *link_error = NULL;
    char *result = bridge_link_add_locked(from, to, label, &link_error);
    zeno_mutex_unlock(&g_bridge.lock);
    if (result == NULL) {
        if (output != NULL) *output = zeno_format("Error: %s", link_error != NULL ? link_error : "could not link notes");
        free(link_error);
        zj_free(root);
        return 0;
    }
    if (output != NULL) *output = zeno_format("Linked %s -> %s", from, to);
    free(result);
    free(link_error);
    zj_free(root);
    return 1;
}
