# find_symbol

**Categoria:** code · **Efeito:** `READ_LOCAL`
**Perfis:** full · **Origem Zeno:** `ZenoC/src/zeno_tools.c:1427`
**Unificada equivalente:** `code` (op `symbol`)

Find code symbols by name.

## Schema

```json
{
  "type": "object",
  "properties": {
    "name": {
      "type": "string"
    }
  },
  "required": [
    "name"
  ]
}
```

## Hooks do host necessarios

- `host.list_dir`
- `host.search`
- `host.exec`
- `host.file_exists`

## Como usar

```c
#include "zenotools.h"

zt_host host;
zt_host_stdio_at(&host, "/meu/workspace");
zt_harness *h = zt_harness_create(&host, "/meu/workspace");
zt_register_all(h);            /* ou zt_register_unified(h) para a versao consolidada */

char *out = NULL, *err = NULL;
int ok = zt_invoke(h, "find_symbol", "{"name":"<texto>"}", &out, &err);
if (!ok) fprintf(stderr, "%s\n", err ? err : "falha");
printf("%s\n", out ? out : "");
free(out); free(err);
```

Arquivos desta pasta: [`port.c`](./port.c) (codigo pronto) · [`original.c`](./original.c) (fragmento ZenoC) · [`tool.json`](./tool.json).
