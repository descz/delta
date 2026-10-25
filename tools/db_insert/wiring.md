# db_insert

**Categoria:** memory · **Efeito:** `WRITE_LOCAL`
**Perfis:** full · **Origem Zeno:** `ZenoC/src/zeno_tools.c:1338`
**Unificada equivalente:** `memory` (op `insert`)

Insert a record into an allowed table.

## Schema

```json
{
  "type": "object",
  "properties": {
    "table": {
      "type": "string"
    },
    "data_json": {
      "type": "string"
    }
  },
  "required": [
    "table",
    "data_json"
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
int ok = zt_invoke(h, "db_insert", "{"table":"<texto>","data_json":"<texto>"}", &out, &err);
if (!ok) fprintf(stderr, "%s\n", err ? err : "falha");
printf("%s\n", out ? out : "");
free(out); free(err);
```

Arquivos desta pasta: [`port.c`](./port.c) (codigo pronto) · [`original.c`](./original.c) (fragmento ZenoC) · [`tool.json`](./tool.json).
