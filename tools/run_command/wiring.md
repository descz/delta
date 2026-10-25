# run_command

**Categoria:** shell · **Efeito:** `PROCESS` · **requer aprovacao**
**Perfis:** full, minimal · **Origem Zeno:** `ZenoC/src/zeno_tools.c:1325`
**Unificada equivalente:** `shell` (op `run`)

Execute a shell command in the workspace sandbox.

## Schema

```json
{
  "type": "object",
  "properties": {
    "command": {
      "type": "string"
    },
    "timeout_seconds": {
      "type": "integer"
    }
  },
  "required": [
    "command"
  ]
}
```

## Hooks do host necessarios

- `host.exec`
- `host.job_start*`
- `host.job_list*`
- `host.job_tail*`
- `host.job_cancel*`

## Como usar

```c
#include "zenotools.h"

zt_host host;
zt_host_stdio_at(&host, "/meu/workspace");
zt_harness *h = zt_harness_create(&host, "/meu/workspace");
zt_register_all(h);            /* ou zt_register_unified(h) para a versao consolidada */

char *out = NULL, *err = NULL;
int ok = zt_invoke(h, "run_command", "{"command":"echo ok"}", &out, &err);
if (!ok) fprintf(stderr, "%s\n", err ? err : "falha");
printf("%s\n", out ? out : "");
free(out); free(err);
```

Arquivos desta pasta: [`port.c`](./port.c) (codigo pronto) · [`original.c`](./original.c) (fragmento ZenoC) · [`tool.json`](./tool.json).
