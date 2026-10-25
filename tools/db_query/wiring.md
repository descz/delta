# db_query

**Categoria:** memory · **Efeito:** `READ_LOCAL`
**Perfis:** full · **Origem Zeno:** `ZenoC/src/zeno_tools.c:1337`
**Unificada equivalente:** `memory` (op `query`)

Run a read-only memory query.

## Schema

```json
{
  "type": "object",
  "properties": {
    "sql": {
      "type": "string"
    }
  },
  "required": [
    "sql"
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
int ok = zt_invoke(h, "db_query", "{"sql":"<texto>"}", &out, &err);
if (!ok) fprintf(stderr, "%s\n", err ? err : "falha");
printf("%s\n", out ? out : "");
free(out); free(err);
```

Arquivos desta pasta: [`port.c`](./port.c) (codigo pronto) · [`original.c`](./original.c) (fragmento ZenoC) · [`tool.json`](./tool.json).
