# memory_link

**Categoria:** memory · **Efeito:** `WRITE_LOCAL`
**Perfis:** bridge · **Origem Zeno:** `zeno-bridge.c:493`
**Unificada equivalente:** `memory` (op `link`)

Link two durable memory notes in the workspace knowledge graph (from/to are note ids returned by memory_remember or memory_list).

## Schema

```json
{
  "type": "object",
  "properties": {
    "from": {
      "type": "string"
    },
    "to": {
      "type": "string"
    },
    "label": {
      "type": "string"
    }
  },
  "required": [
    "from",
    "to"
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
int ok = zt_invoke(h, "memory_link", "{"from":"<texto>","to":"<texto>"}", &out, &err);
if (!ok) fprintf(stderr, "%s\n", err ? err : "falha");
printf("%s\n", out ? out : "");
free(out); free(err);
```

Arquivos desta pasta: [`port.c`](./port.c) (codigo pronto) · [`original.c`](./original.c) (fragmento ZenoC) · [`tool.json`](./tool.json).
