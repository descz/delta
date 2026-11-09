/* hooks.h - eventos do harness ("change the harness, not your workflow")
 *
 * Uma extensao kind="hook" nao adiciona ferramenta: ela intercepta o que o
 * harness ja' faz. `name` e' o evento, `arg` e' a linha de comando do hook.
 *
 * Eventos:
 *   tool_call    antes de executar uma ferramenta
 *                entrada:  {"event":"tool_call","tool":"bash","args":"<json>","cwd":"..."}
 *                resposta: {"block":true,"reason":"..."}  ou  {"args":"<json novo>"}
 *   tool_result  depois de executar
 *                entrada:  {"event":"tool_result","tool":"bash","is_error":0,"output":"..."}
 *                resposta: {"output":"..."}  (substitui)  ou  {"append":"..."}
 *   turn_end     fim do turno (resposta ignorada)
 *                entrada:  {"event":"turn_end"}
 *
 * O hook recebe o caminho do arquivo JSON como argv[1] (argv do Windows tem
 * teto de ~32 KB e um tool result pode ter 200 KB) e imprime JSON no stdout.
 * Tudo fail-open: hook quebrado, lento (> 5 s) ou sem resposta e' ignorado, o
 * agente segue como se nao existisse.
 */
#ifndef DELTA_HOOKS_H
#define DELTA_HOOKS_H

#include "util.h"

/* 1 = segue (args_out pode ter sido reescrito); 0 = bloqueado (motivo em reason) */
int  hooks_tool_call(const char *tool, const char *args_json, const char *cwd,
                     Str *args_out, Str *reason);

/* 1 = a saida foi reescrita/estendida em output_out */
int  hooks_tool_result(const char *tool, const char *output, int is_error, Str *output_out);

void hooks_turn_end(void);

#endif /* DELTA_HOOKS_H */
