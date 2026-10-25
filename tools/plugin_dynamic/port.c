/* ============================================================================
 * plugin_<id>_<name> — PORT C pronto para o seu harness
 * ----------------------------------------------------------------------------
 * schema    : {"type":"object","note":"schema derivado do template de comando do plugin"}
 * efeito    : WRITE_EXTERNAL
 * handler   : zt_tool_plugin_dispatch
 * motor     : plugin_command_for() + host.exec
 * registro  : zt_register_plugin_tools(h)   (ou zt_register_all(h))
 * ----------------------------------------------------------------------------
 * Dependencias: apenas zenotools.h + libc. Todo IO passa pela vtable zt_host.
 * ========================================================================= */

/* --- handler registrado no registry --- */
int zt_tool_plugin_dispatch(zt_harness *h, const char *args_json, char **out, char **err) {
    /* o nome vem do tool em execucao (zt_invoke define h->current_tool);
       tambem aceitamos o campo opcional 'tool' nos argumentos */
    char *name = NULL;
    zt_arg_string(args_json, "tool", &name, NULL);
    if (name == NULL && h->current_tool != NULL) name = zt_strdup(h->current_tool);
    if (name == NULL) { if (out != NULL) *out = zt_strdup("Error: dispatch de plugin sem nome de tool em execucao."); return 0; }
    char *command = plugin_command_for(h, name, args_json);
    if (command == NULL) { if (out != NULL) *out = zt_format("Error: plugin tool '%s' nao encontrada em %s.", name, h->plugins_path); free(name); return 0; }
    if (h->host.exec == NULL) { if (out != NULL) *out = zt_strdup("Error: hook host.exec nao configurado."); free(command); free(name); return 0; }
    int exit_code = 0;
    char *hook_error = NULL;
    char *result = h->host.exec(h->host.ctx, command, 120000, &exit_code, &hook_error);
    if (result == NULL) { if (out != NULL) *out = zt_format("Error: %s", hook_error != NULL ? hook_error : "plugin falhou"); if (err != NULL) *err = zt_strdup("plugin failed"); }
    else if (exit_code == 0) { if (out != NULL) *out = result; result = NULL; }
    else if (out != NULL) *out = zt_format("Error: plugin retornou %d\n%s", exit_code, result);
    free(hook_error); free(result); free(command); free(name);
    return 1;
}

