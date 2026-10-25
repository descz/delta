# memory_search

**Categoria:** memory · **Efeito:** `READ_LOCAL`
**Perfis:** full · **Origem Zeno:** `ZenoC/src/zeno_tools.c:1333`
**Unificada equivalente:** `memory` (op `search`)

Search durable memories - run this at the start of every user request and whenever the user references past context.

## Schema

```json
{
  "type": "object",
  "properties": {
    "query": {
      "type": "string"
    },
    "limit": {
      "type": "integer"
    }
  },
  "required": [
    "query"
  ]
}
```

## Hooks do host necessarios

- nenhum (tool pura / usa apenas o store de memoria)

## Como usar

```c
#include "zenotools.h"

zt_host host;
zt_host_stdio_at(&host, "/meu/workspace");
zt_harness *h = zt_harness_create(&host, "/meu/workspace");
zt_register_all(h);            /* ou zt_register_unified(h) para a versao consolidada */

char *out = NULL, *err = NULL;
int ok = zt_invoke(h, "memory_search", "{"query":"<texto>"}", &out, &err);
if (!ok) fprintf(stderr, "%s\n", err ? err : "falha");
printf("%s\n", out ? out : "");
free(out); free(err);
```

Arquivos desta pasta: [`port.c`](./port.c) (codigo pronto) · [`original.c`](./original.c) (fragmento ZenoC) · [`tool.json`](./tool.json).
