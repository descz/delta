/* ============================================================================
 * mcp_call — PORT C pronto para o seu harness
 * ----------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"server_url":{"type":"string"},"command":{"type":"string","description":"stdio server command line"},"headers":{"type":"object","description":"HTTP headers for server_ur
 * efeito    : WRITE_EXTERNAL
 * handler   : zt_tool_mcp_call
 * motor     : zt_extend_op(op="mcp")
 * registro  : zt_register_all(h)   (ou zt_register_all(h))
 * ----------------------------------------------------------------------------
 * Dependencias: apenas zenotools.h + libc. Todo IO passa pela vtable zt_host.
 * ========================================================================= */

/* --- handler registrado no registry --- */
int zt_tool_mcp_call(zt_harness *h, const char *a, char **o, char **e) { return zt_extend_op(h, "mcp", a, o, e); }

/* --- motor da familia (compartilhado por todas as tools de "extend") --- */
int zt_extend_op(zt_harness *h, const char *op, const char *args_json, char **out, char **err) {
    if (h == NULL || op == NULL) return 0;
    if (strcmp(op, "mcp") == 0) {
        if (h->host.mcp_call == NULL) {
            if (out != NULL) *out = zt_strdup("Error: hook host.mcp_call nao configurado.");
            if (err != NULL) *err = zt_strdup("no mcp hook");
            return 0;
        }
        char *server_url = NULL, *command = NULL, *headers = NULL, *args_json = NULL, *tool_name = NULL;
        zt_arg_string(args_json, "server_url", &server_url, NULL);
        zt_arg_string(args_json, "command", &command, NULL);
        zt_arg_string(args_json, "headers", &headers, NULL);
        zt_arg_string(args_json, "args_json", &args_json, "{}");
        zt_arg_string(args_json, "tool_name", &tool_name, "");
        if (tool_name == NULL || *tool_name == '\0') {
            if (out != NULL) *out = zt_strdup("Error: 'tool_name' e obrigatorio para mcp_call.");
            free(server_url); free(command); free(headers); free(args_json);
            return 0;
        }
        char *hook_error = NULL;
        char *result = h->host.mcp_call(h->host.ctx, server_url, command, headers, tool_name, args_json, &hook_error);
        if (result == NULL) { if (out != NULL) *out = zt_format("Error: %s", hook_error != NULL ? hook_error : "chamada MCP falhou"); if (err != NULL) *err = zt_strdup("mcp failed"); }
        else *out = result;
        free(hook_error); free(server_url); free(command); free(headers); free(args_json); free(tool_name);
        return result != NULL;
    }
    if (strcmp(op, "plugin") == 0) {
        char *request = NULL, *name_hint = NULL, *plugins_dir = NULL;
        zt_arg_string(args_json, "request", &request, "");
        zt_arg_string(args_json, "name_hint", &name_hint, "");
        zt_arg_string(args_json, "plugins_dir", &plugins_dir, NULL);
        if (request == NULL || *request == '\0') { if (out != NULL) *out = zt_strdup("Error: 'request' e obrigatorio."); free(name_hint); free(plugins_dir); return 0; }
        if (h->host.agent_turn == NULL) {
            if (out != NULL) *out = zt_strdup("Error: hook host.agent_turn nao configurado (create_plugin precisa de um modelo para gerar o JSON).");
            if (err != NULL) *err = zt_strdup("no agent hook");
            free(request); free(name_hint); free(plugins_dir);
            return 0;
        }
        char *prompt = zt_format(
            "Gere um plugin do Zeno em JSON ESTRITO (sem comentarios, sem markdown).\n"
            "Formato: {\"id\":\"slug\",\"name\":\"Nome\",\"description\":\"frase\",\"enabled\":true,"
            "\"tools\":[{\"name\":\"slug\",\"description\":\"o que faz\",\"command\":\"comando shell com {parametros}\"}]}\n"
            "Pedido do usuario: %s\nDica de nome: %s\nResponda APENAS o JSON do plugin.",
            request, name_hint != NULL && *name_hint != '\0' ? name_hint : "(livre)");
        char *hook_error = NULL;
        char *generated = h->host.agent_turn(h->host.ctx, prompt, &hook_error);
        free(prompt);
        if (generated == NULL) { if (out != NULL) *out = zt_format("Error: %s", hook_error != NULL ? hook_error : "modelo nao respondeu"); if (err != NULL) *err = zt_strdup("plugin generation failed"); free(hook_error); free(request); free(name_hint); free(plugins_dir); return 0; }
        /* recorta o primeiro objeto JSON da resposta */
        char *first = strchr(generated, '{');
        char *last = strrchr(generated, '}');
        if (first == NULL || last == NULL || last < first) { if (out != NULL) *out = zt_strdup("Error: o modelo nao devolveu um JSON de plugin valido."); free(generated); free(hook_error); free(request); free(name_hint); free(plugins_dir); return 0; }
        char saved = last[1]; last[1] = '\0';
        char *plugin_json = zt_strdup(first);
        last[1] = saved;
        char *parse_error = NULL;
        zt_json *plugin = zt_json_parse(plugin_json, &parse_error);
        if (plugin == NULL || plugin->type != ZT_JSON_OBJECT) {
            if (out != NULL) *out = zt_format("Error: JSON de plugin invalido: %s", parse_error != NULL ? parse_error : "parse falhou");
            free(parse_error); free(plugin_json); free(generated); free(hook_error); free(request); free(name_hint); free(plugins_dir);
            return 0;
        }
        free(parse_error);
        /* carrega o arquivo atual (ou cria) e adiciona o plugin */
        char *store_path = NULL;
        if (plugins_dir != NULL && *plugins_dir != '\0') {
            char *relative = zt_format("%s/plugins.json", plugins_dir);
            store_path = zt_resolve(h, relative);
            free(relative);
        }
        if (store_path == NULL) store_path = zt_strdup(h->plugins_path);
        char *raw = h->host.read_file != NULL ? h->host.read_file(h->host.ctx, store_path, 1024U * 1024U, NULL) : NULL;
        zt_json *store = raw != NULL ? zt_json_parse(raw, NULL) : NULL;
        if (store == NULL || store->type != ZT_JSON_OBJECT) { zt_json_free(store); store = zt_json_object(); }
        zt_json *plugins = zt_json_object_get(store, "plugins");
        if (plugins == NULL || plugins->type != ZT_JSON_ARRAY) { plugins = zt_json_array(); zt_json_object_set(store, "plugins", plugins); }
        zt_json_object_set_bool(plugin, "enabled", 1);
        zt_json_array_push(plugins, plugin);
        char *serialized = zt_json_dump_pretty(store, 1);
        int ok = serialized != NULL && zt_write_file(h, store_path, serialized, 0) == NULL;
        if (ok) zt_register_plugin_tools(h, serialized);
        if (out != NULL) *out = ok ? zt_format("Plugin criado e salvo em %s.\n%s", store_path, serialized) : zt_strdup("Error: nao foi possivel salvar o plugin.");
        if (!ok && err != NULL) *err = zt_strdup("could not save plugin file");
        free(serialized); free(store); free(raw); free(plugin_json); free(store_path); free(generated); free(hook_error); free(request); free(name_hint); free(plugins_dir);
        return ok;
    }
    if (out != NULL) *out = zt_format("Error: operacao desconhecida de extensao: %s", op);
    return 0;
}
