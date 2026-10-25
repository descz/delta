# load_skill

**Categoria:** agent · **Efeito:** `READ_LOCAL`
**Perfis:** skill, skill · **Origem Zeno:** `ZenoC/src/zeno_tools.c:1381`
**Unificada equivalente:** `agent` (op `skill`)

Load the full instructions of one available skill by name (skills are listed in the system prompt).

## Schema

```json
{
  "type": "object",
  "properties": {
    "name": {
      "type": "string",
      "description": "Skill directory name"
    }
  },
  "required": [
    "name"
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
int ok = zt_invoke(h, "load_skill", "{"name":"<texto>"}", &out, &err);
if (!ok) fprintf(stderr, "%s\n", err ? err : "falha");
printf("%s\n", out ? out : "");
free(out); free(err);
```

Arquivos desta pasta: [`port.c`](./port.c) (codigo pronto) · [`original.c`](./original.c) (fragmento ZenoC) · [`tool.json`](./tool.json).
