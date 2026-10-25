# git_checkpoint

**Categoria:** git · **Efeito:** `PROCESS` · **requer aprovacao**
**Perfis:** full · **Origem Zeno:** `ZenoC/src/zeno_tools.c:1425`
**Unificada equivalente:** `git` (op `checkpoint`)

Create a Git checkpoint.

## Schema

```json
{
  "type": "object",
  "properties": {
    "message": {
      "type": "string"
    }
  }
}
```

## Hooks do host necessarios

- `host.git* (ou exec)`

## Como usar

```c
#include "zenotools.h"

zt_host host;
zt_host_stdio_at(&host, "/meu/workspace");
zt_harness *h = zt_harness_create(&host, "/meu/workspace");
zt_register_all(h);            /* ou zt_register_unified(h) para a versao consolidada */

char *out = NULL, *err = NULL;
int ok = zt_invoke(h, "git_checkpoint", "{}", &out, &err);
if (!ok) fprintf(stderr, "%s\n", err ? err : "falha");
printf("%s\n", out ? out : "");
free(out); free(err);
```

Arquivos desta pasta: [`port.c`](./port.c) (codigo pronto) · [`original.c`](./original.c) (fragmento ZenoC) · [`tool.json`](./tool.json).
