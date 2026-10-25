/* ============================================================================
 * fs — TOOL UNIFICADA (substitui 9 tools granulares)
 * ----------------------------------------------------------------------------
 * ops      : list, read, write, append, replace, mkdir, delete, search, glob
 * cobre    : list_workspace, read_text_file, write_text_file, append_text_file, replace_in_file, create_directory, delete_file, search_workspace, glob_workspace
 * handler  : zt_tool_fs_unified
 * motor    : zt_fs_op()
 * ========================================================================= */

/* --- schema (use em zt_register_unified ou no seu proprio registro) --- */
/* schema da tool "fs": ver ZenoTools/harness/src/zt_register.c (tabela UNIFIED_TOOLS) */

/* --- handler unificado --- */
int zt_tool_fs_unified(zt_harness *h, const char *args_json, char **out, char **err) {
    char *op = NULL;
    if (!zt_arg_string(args_json, "op", &op, NULL) || op == NULL) {
        if (out != NULL) *out = zt_strdup("Error: 'op' e obrigatorio (list|read|write|append|replace|mkdir|delete|search|glob).");
        if (err != NULL) *err = zt_strdup("missing op");
        return 0;
    }
    int ok = zt_fs_op(h, op, args_json, out, err);
    free(op);
    return ok;
}

/* --- motor compartilhado com as tools granulares --- */
int zt_fs_op(zt_harness *h, const char *op, const char *args_json, char **out, char **err) {
    if (h == NULL || op == NULL) return 0;
    char *relative = NULL;
    zt_arg_string(args_json, "relative_path", &relative, ".");
    char *path = zt_resolve(h, relative != NULL ? relative : ".");
    if (path == NULL) {
        if (out != NULL) *out = zt_strdup("Error: caminho fora do workspace (sandbox).");
        if (err != NULL) *err = zt_strdup("path escape");
        free(relative);
        return 0;
    }
    int ok = 0;

    if (strcmp(op, "list") == 0) {
        if (h->host.list_dir == NULL) { if (out != NULL) *out = zt_strdup("Error: hook host.list_dir nao configurado."); }
        else {
            char *hook_error = NULL;
            char *listing = h->host.list_dir(h->host.ctx, path, 0, zt_arg_size(args_json, "max_entries", 200, 10000), &hook_error);
            if (listing != NULL) { *out = listing; ok = 1; } else if (out != NULL) *out = zt_format("Error: %s", hook_error != NULL ? hook_error : "falha ao listar");
            free(hook_error);
        }
    } else if (strcmp(op, "read") == 0) {
        char *content = zt_read_file(h, path, zt_arg_size(args_json, "max_chars", 50000, 2U * 1024U * 1024U));
        if (content != NULL) {
            *out = content;
            ok = strncmp(content, "Error:", 6) != 0;
            if (ok) zt_ledger_record(h, path);
        }
    } else if (strcmp(op, "write") == 0 || strcmp(op, "append") == 0) {
        char *content = NULL;
        zt_arg_string(args_json, "content", &content, "");
        int append = strcmp(op, "append") == 0;
        if (!fs_require_read(h, path, out, err)) { free(content); free(path); free(relative); return 0; }
        char *before = append || !host_exists(h, path) ? NULL : zt_read_file(h, path, 1024U * 1024U);
        char *write_error = zt_write_file(h, path, content, append);
        if (write_error == NULL) {
            char *after = zt_read_file(h, path, 1024U * 1024U);
            char *summary = zt_summarize_change(before, after);
            *out = zt_format("%s arquivo %s: %s\n%s", append ? "Appended:" : "Written:", append ? "(append)" : relative, path, summary != NULL ? summary : "");
            ok = 1;
            free(summary); free(after);
        } else *out = write_error;
        free(before); free(content);
    } else if (strcmp(op, "replace") == 0) {
        char *old_text = NULL, *new_text = NULL;
        zt_arg_string(args_json, "old_text", &old_text, "");
        zt_arg_string(args_json, "new_text", &new_text, "");
        if (!fs_require_read(h, path, out, err)) { free(old_text); free(new_text); free(path); free(relative); return 0; }
        char *before = zt_read_file(h, path, 1024U * 1024U);
        if (before == NULL || strncmp(before, "Error:", 6) == 0) {
            if (out != NULL) *out = zt_format("Error: nao foi possivel ler %s para substituir texto.", relative);
        } else {
            char *updated = fs_search_replace(before, old_text, new_text, zt_arg_bool(args_json, "replace_all", 1));
            if (updated == NULL) {
                if (out != NULL) *out = zt_strdup("Error: old_text nao encontrado no arquivo.");
            } else {
                char *write_error = zt_write_file(h, path, updated, 0);
                if (write_error == NULL) {
                    char *summary = zt_summarize_change(before, updated);
                    *out = zt_format("Replaced in %s.\n%s", relative, summary != NULL ? summary : "");
                    ok = 1; free(summary);
                } else *out = write_error;
                free(updated);
            }
        }
        free(before); free(old_text); free(new_text);
    } else if (strcmp(op, "mkdir") == 0) {
        if (h->host.make_dir == NULL) { if (out != NULL) *out = zt_strdup("Error: hook host.make_dir nao configurado."); }
        else {
            char *hook_error = NULL;
            ok = h->host.make_dir(h->host.ctx, path, &hook_error);
            if (ok) *out = zt_format("Directory created: %s", relative);
            else *out = zt_format("Error: %s", hook_error != NULL ? hook_error : "falha ao criar diretorio");
            free(hook_error);
        }
    } else if (strcmp(op, "delete") == 0) {
        if (h->host.remove_file == NULL) { if (out != NULL) *out = zt_strdup("Error: hook host.remove_file nao configurado."); }
        else {
            char *backup = zt_trash_backup(h, path);
            char *hook_error = NULL;
            if (h->host.remove_file(h->host.ctx, path, &hook_error)) {
                *out = zt_format("Deleted: %s%s%s", relative, backup != NULL ? " (backup em " : "", backup != NULL ? backup : "");
                if (backup != NULL) { char *next = zt_format("%s%s)", *out, backup); free(*out); *out = next; }
                ok = 1;
            } else *out = zt_format("Error: %s", hook_error != NULL ? hook_error : "falha ao remover");
            free(hook_error); free(backup);
        }
    } else if (strcmp(op, "search") == 0 || strcmp(op, "glob") == 0) {
        int is_glob = strcmp(op, "glob") == 0;
        char *needle = NULL;
        zt_arg_string(args_json, is_glob ? "pattern" : "query", &needle, is_glob ? "*" : "");
        char *hook_error = NULL;
        char *result = NULL;
        if (is_glob && h->host.glob != NULL) result = h->host.glob(h->host.ctx, path, needle, zt_arg_size(args_json, "max_results", 200, 5000), &hook_error);
        else if (!is_glob && h->host.search != NULL) result = h->host.search(h->host.ctx, path, needle, zt_arg_size(args_json, "max_results", 100, 2000), &hook_error);
        if (result != NULL) { *out = result; ok = 1; }
        else if (out != NULL) *out = zt_format("Error: %s", hook_error != NULL ? hook_error : (is_glob ? "hook host.glob nao configurado." : "hook host.search nao configurado."));
        free(hook_error); free(needle);
    } else {
        if (out != NULL) *out = zt_format("Error: operacao desconhecida de fs: %s", op);
    }
    free(path); free(relative);
    return ok;
}

/* --- tools granulares cobertas --- */
/* list_workspace: zt_tool_list_workspace() -> zt_invoke(h, "list_workspace", args) */
/* read_text_file: zt_tool_read_text_file() -> zt_invoke(h, "read_text_file", args) */
/* write_text_file: zt_tool_write_text_file() -> zt_invoke(h, "write_text_file", args) */
/* append_text_file: zt_tool_append_text_file() -> zt_invoke(h, "append_text_file", args) */
/* replace_in_file: zt_tool_replace_in_file() -> zt_invoke(h, "replace_in_file", args) */
/* create_directory: zt_tool_create_directory() -> zt_invoke(h, "create_directory", args) */
/* delete_file: zt_tool_delete_file() -> zt_invoke(h, "delete_file", args) */
/* search_workspace: zt_tool_search_workspace() -> zt_invoke(h, "search_workspace", args) */
/* glob_workspace: zt_tool_glob_workspace() -> zt_invoke(h, "glob_workspace", args) */
