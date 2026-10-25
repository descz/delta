# spawn_subagent

**Categoria:** agent · **Efeito:** `READ_LOCAL`
**Perfis:** full · **Origem Zeno:** `ZenoC/src/zeno_tools.c:1235`
**Unificada equivalente:** `agent` (op `subagent`)

Run an isolated read-only subagent on a self-contained research/analysis task and return its final report. Subagents cannot write files or execute commands.

## Schema

```json
{
  "type": "object",
  "properties": {
    "task": {
      "type": "string"
    },
    "max_turns": {
      "type": "integer"
    }
  },
  "required": [
    "task"
  ]
}
```

## Hooks do host necessarios

- `host.list_dir (skills)`
- `host.subagent*`

## Como usar

```c
#include "zenotools.h"

zt_host host;
zt_host_stdio_at(&host, "/meu/workspace");
zt_harness *h = zt_harness_create(&host, "/meu/workspace");
zt_register_all(h);            /* ou zt_register_unified(h) para a versao consolidada */

char *out = NULL, *err = NULL;
int ok = zt_invoke(h, "spawn_subagent", "{"task":"<texto>"}", &out, &err);
if (!ok) fprintf(stderr, "%s\n", err ? err : "falha");
printf("%s\n", out ? out : "");
free(out); free(err);
```

Arquivos desta pasta: [`port.c`](./port.c) (codigo pronto) · [`original.c`](./original.c) (fragmento ZenoC) · [`tool.json`](./tool.json).
