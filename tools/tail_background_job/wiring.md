# tail_background_job

**Categoria:** shell · **Efeito:** `READ_LOCAL`
**Perfis:** full · **Origem Zeno:** `ZenoC/src/zeno_tools.c:1328`
**Unificada equivalente:** `shell` (op `tail`)

Read background job output.

## Schema

```json
{
  "type": "object",
  "properties": {
    "job_id": {
      "type": "string"
    },
    "max_lines": {
      "type": "integer"
    }
  },
  "required": [
    "job_id"
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
int ok = zt_invoke(h, "tail_background_job", "{"job_id":"<texto>"}", &out, &err);
if (!ok) fprintf(stderr, "%s\n", err ? err : "falha");
printf("%s\n", out ? out : "");
free(out); free(err);
```

Arquivos desta pasta: [`port.c`](./port.c) (codigo pronto) · [`original.c`](./original.c) (fragmento ZenoC) · [`tool.json`](./tool.json).
