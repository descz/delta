/* tools.h - ferramentas do agente (read, write, edit, bash, ls, find, grep) */
#ifndef DELTA_TOOLS_H
#define DELTA_TOOLS_H

#include "util.h"
#include "json.h"

typedef struct {
    Str  output;      /* saida textual para o modelo */
    int  is_error;
    int  truncated;
    int  exit_code;
    double wall_seconds;
} ToolResult;

typedef struct Tool {
    const char *name;
    const char *description;
    const char *schema;   /* JSON Schema dos parametros */
    int (*run)(struct Tool *self, JValue *args, const char *cwd, ToolResult *res);
} Tool;

void        tool_result_init(ToolResult *r);
void        tool_result_free(ToolResult *r);

/* Chamado mentre uma ferramenta longa roda (bash): deixa a TUI redesenhar
   o tempo decorrido. Pode ser NULL. */
void        tools_set_tick(void (*fn)(void *), void *ud);

/* Lista terminada por um Tool com name == NULL. */
const Tool *tools_all(void);
const Tool *tool_find(const char *name);

/* Familias unificadas (tools_ext.c): fs, git, shell, web, memory, code,
   assert, agent, extend. Lista terminada por name == NULL. */
const Tool *tools_ext_all(void);

/* Extensoes plugaveis (extensions.c): tools dinamicas, skills, comandos, mcp. */
const Tool *extensions_all(void);      /* ferramenta "extensions" */
const Tool *ext_tools_all(void);       /* tools dinamicas da extensao (kind=tool) */
void        ext_reload(void);          /* invalida a tabela dinamica */
int         ext_count(void);
int         ext_get(int i, char *name, size_t nn, char *kind, size_t nk, int *enabled);
int         ext_set_enabled(const char *name, int on);
int         ext_remove(const char *name);
int         ext_add(const char *kind, const char *name, const char *arg, const char *desc, const char *schema);
int         ext_is_enabled(const char *name);
int         ext_command_prompt(const char *name, char *out, size_t n);

/* Helpers compartilhados entre os dois arquivos de tools. */
char *tools_resolve(const char *cwd, const char *path);      /* caminho absoluto */
int   tools_exec(const char *cwd, const char *command, int timeout_sec, Str *out, int *exit_code);
void  tools_oem_to_utf8(Str *out);   /* console do Windows: cp850 -> UTF-8 */

/* Salva a mensagem do usuario e os fatos duraveis em ~/.deltaCore/memory.json. */
void  tools_auto_memorize(const char *texto);

/* Indice de skills (skills/, ~/.deltaCore/skills e ~/.agents/skills) para o prompt. */
char *tools_skills_index(const char *cwd, size_t max_chars);

/* Diff unificado simples (prefixo/sufixo comum) entre dois textos. */
void        diff_lines(const char *a, const char *b, Str *out, int context);

#endif /* DELTA_TOOLS_H */
