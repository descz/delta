/* =========================================================================
 * create_plugin
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/create_plugin.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_studio.c:1951  ->  zeno_register_studio_tools
 * handler   : tool_create_plugin
 * efeito    : WRITE_LOCAL
 * perfis    : studio
 * -------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"request":{"type":"string","description":"what the plugin must do"},"name_hint":{"type":"string","description":"short display name hint"},"plugins_dir":{"type":"string","description":"plugins directory (default zeno_plugins)"}},"required":["request"]}
 * -------------------------------------------------------------------------
 * Generate a Zeno JSON plugin from a user request, save it automatically, and make it available. Use after confirming plugin name + one-sentence description.
 * =========================================================================
 */

static int tool_create_plugin(void *context, const char *args_json, char **output, char **error) {
    char *request = NULL;
    char *hint = NULL;
    char *dir = NULL;
    char *plugin = NULL;
    int ok = 0;
    const char *plugins_dir;
    char err[256];
    (void)context;
    if (output != NULL) *output = NULL;
    studio_arg_string(args_json, "request", &request);
    studio_arg_string(args_json, "name_hint", &hint);
    studio_arg_string(args_json, "name", &dir);
    if (dir == NULL) studio_arg_string(args_json, "plugins_dir", &dir);
    if (request == NULL || *request == '\0') {
        if (error != NULL) *error = zeno_strdup("field 'request' is required");
        free(request);
        free(hint);
        free(dir);
        return 0;
    }
    if (dir != NULL && *dir != '\0') plugins_dir = dir;
    else if (getenv("ZENO_PLUGINS_DIR") != NULL && *getenv("ZENO_PLUGINS_DIR") != '\0')
        plugins_dir = getenv("ZENO_PLUGINS_DIR");
    else
        plugins_dir = "zeno_plugins";
    plugin = zeno_plugin_create(request, hint);
    if (plugin == NULL) {
        if (error != NULL) *error = zeno_strdup("could not generate plugin JSON");
        free(request);
        free(hint);
        free(dir);
        return 0;
    }
    if (!zeno_plugin_validate(plugin, err, sizeof(err))) {
        if (error != NULL) *error = zeno_format("generated invalid plugin: %s", err);
        free(plugin);
        free(request);
        free(hint);
        free(dir);
        return 0;
    }
    if (!zeno_plugin_save(plugins_dir, plugin)) {
        if (error != NULL) *error = zeno_strdup("could not save plugin file");
        free(plugin);
        free(request);
        free(hint);
        free(dir);
        return 0;
    }
    if (output != NULL) *output = plugin;
    else free(plugin);
    ok = 1;
    free(request);
    free(hint);
    free(dir);
    return ok;
}
