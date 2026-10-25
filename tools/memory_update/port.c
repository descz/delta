/* ============================================================================
 * memory_update — PORT C pronto para o seu harness
 * ----------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"id":{"type":"string"},"title":{"type":"string"},"content":{"type":"string"},"kind":{"type":"string"},"scope":{"type":"string"},"tags_json":{"type":"string"}},"required"
 * efeito    : WRITE_LOCAL
 * handler   : zt_tool_memory_update
 * motor     : zt_memory_op(op="update")
 * registro  : zt_register_memory_tools(h)   (ou zt_register_all(h))
 * ----------------------------------------------------------------------------
 * Dependencias: apenas zenotools.h + libc. Todo IO passa pela vtable zt_host.
 * ========================================================================= */

/* --- handler registrado no registry --- */
int zt_tool_memory_update(zt_harness *h, const char *a, char **o, char **e) { return zt_memory_op(h, "update", a, o, e); }

/* --- motor da familia (compartilhado por todas as tools de "memory") --- */
int zt_memory_op(zt_harness *h, const char *op, const char *args_json, char **out, char **err) {
    if (h == NULL || h->memory == NULL || op == NULL) return 0;
    if (strcmp(op, "remember") == 0) {
        char *title = NULL, *content = NULL, *kind = NULL, *scope = NULL, *tags = NULL;
        zt_arg_string(args_json, "title", &title, "");
        zt_arg_string(args_json, "content", &content, "");
        zt_arg_string(args_json, "kind", &kind, "note");
        zt_arg_string(args_json, "scope", &scope, "global");
        zt_arg_string(args_json, "tags_json", &tags, "[]");
        char *id = NULL;
        int ok = zt_memory_add_note(h->memory, title, content, kind, scope, tags, &id);
        if (out != NULL) *out = ok ? zt_format("Memory stored: %s", id != NULL ? id : "unknown") : zt_strdup("Error: tags_json precisa ser um array JSON de strings.");
        if (!ok && err != NULL) *err = zt_strdup("invalid memory note");
        free(title); free(content); free(kind); free(scope); free(tags); free(id);
        return ok;
    }
    if (strcmp(op, "update") == 0) {
        char *id = NULL, *title = NULL, *content = NULL, *kind = NULL, *scope = NULL, *tags = NULL;
        zt_arg_string(args_json, "id", &id, "");
        zt_arg_string(args_json, "title", &title, NULL);
        zt_arg_string(args_json, "content", &content, NULL);
        zt_arg_string(args_json, "kind", &kind, NULL);
        zt_arg_string(args_json, "scope", &scope, NULL);
        zt_arg_string(args_json, "tags_json", &tags, NULL);
        int ok = zt_memory_update_note(h->memory, id, title, content, kind, scope, tags);
        if (out != NULL) *out = ok ? zt_format("Memory updated: %s", id) : zt_strdup("Error: id de memoria nao encontrado (use memory_search/memory_list) ou tags_json invalido.");
        if (!ok && err != NULL) *err = zt_strdup("memory update failed");
        free(id); free(title); free(content); free(kind); free(scope); free(tags);
        return ok;
    }
    if (strcmp(op, "search") == 0) {
        char *query = NULL;
        zt_arg_string(args_json, "query", &query, "");
        *out = zt_memory_search_notes(h->memory, query, zt_arg_size(args_json, "limit", 20, 1000));
        free(query);
        return *out != NULL;
    }
    if (strcmp(op, "list") == 0) {
        char *scope = NULL;
        zt_arg_string(args_json, "scope", &scope, "");
        *out = zt_memory_list_notes(h->memory, scope, zt_arg_size(args_json, "limit", 50, 1000));
        free(scope);
        return *out != NULL;
    }
    if (strcmp(op, "link") == 0) {
        char *from = NULL, *to = NULL, *label = NULL;
        zt_arg_string(args_json, "from", &from, "");
        zt_arg_string(args_json, "to", &to, "");
        zt_arg_string(args_json, "label", &label, "");
        char *link_error = NULL;
        int ok = zt_memory_link_add(h->memory, from, to, label, &link_error);
        if (out != NULL) *out = ok ? zt_format("Linked %s -> %s", from, to) : zt_format("Error: %s", link_error != NULL ? link_error : "falha ao ligar");
        if (!ok && err != NULL) *err = link_error != NULL ? zt_strdup(link_error) : zt_strdup("link failed");
        free(link_error); free(from); free(to); free(label);
        return ok;
    }
    if (strcmp(op, "unlink") == 0) {
        char *from = NULL, *to = NULL;
        zt_arg_string(args_json, "from", &from, "");
        zt_arg_string(args_json, "to", &to, "");
        char *link_error = NULL;
        int ok = zt_memory_link_delete(h->memory, from, to, &link_error);
        if (out != NULL) *out = ok ? zt_format("Unlinked %s -> %s", from, to) : zt_format("Error: %s", link_error != NULL ? link_error : "falha ao desligar");
        if (!ok && err != NULL) *err = link_error != NULL ? zt_strdup(link_error) : zt_strdup("unlink failed");
        free(link_error); free(from); free(to);
        return ok;
    }
    if (strcmp(op, "query") == 0) {
        char *sql = NULL;
        zt_arg_string(args_json, "sql", &sql, "");
        /* paridade com o ZenoC: apenas leitura */
        if (sql != NULL && (zt_contains_ci(sql, "insert") || zt_contains_ci(sql, "update") || zt_contains_ci(sql, "delete") ||
                            zt_contains_ci(sql, "drop") || zt_contains_ci(sql, "alter") || zt_contains_ci(sql, "create"))) {
            *out = zt_strdup("Error: apenas consultas SELECT sao permitidas.");
            free(sql);
            return 0;
        }
        *out = zt_memory_search_notes(h->memory, "", 50);
        free(sql);
        return *out != NULL;
    }
    if (strcmp(op, "insert") == 0) {
        char *table = NULL, *data = NULL;
        zt_arg_string(args_json, "table", &table, "");
        zt_arg_string(args_json, "data_json", &data, "");
        char *parse_error = NULL;
        zt_json *node = zt_json_parse(data != NULL ? data : "{}", &parse_error);
        int valid = node != NULL && (strcmp(table, "memory_records") == 0 || strcmp(table, "facts") == 0);
        zt_json_free(node); free(parse_error);
        if (!valid) {
            if (out != NULL) *out = zt_strdup("Error: JSON invalido ou tabela nao permitida (memory_records|facts).");
            if (err != NULL) *err = zt_strdup("invalid database insert");
            free(table); free(data);
            return 0;
        }
        char *id = NULL;
        int ok = zt_memory_add_note(h->memory, table, data, "record", "database", "[]", &id);
        if (out != NULL) *out = ok ? zt_format("Inserted into %s (note %s).", table, id != NULL ? id : "?") : zt_strdup("Error: falha ao inserir.");
        free(id); free(table); free(data);
        return ok;
    }
    if (out != NULL) *out = zt_format("Error: operacao desconhecida de memoria: %s", op);
    return 0;
}
