# update_plan

**Categoria:** agent · **Efeito:** `WRITE_LOCAL`
**Perfis:** full · **Origem Zeno:** `ZenoC/src/zeno_tools.c:1186`
**Unificada equivalente:** `agent` (op `plan`)

Persist the live task plan as Markdown checklist lines ('- [ ] pending', '- [x] done'). Re-injected into your system prompt every turn.

## Schema

```json
{
  "type": "object",
  "properties": {
    "plan_markdown": {
      "type": "string"
    }
  },
  "required": [
    "plan_markdown"
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
int ok = zt_invoke(h, "update_plan", "{"plan_markdown":"<texto>"}", &out, &err);
if (!ok) fprintf(stderr, "%s\n", err ? err : "falha");
printf("%s\n", out ? out : "");
free(out); free(err);
```

Arquivos desta pasta: [`port.c`](./port.c) (codigo pronto) · [`original.c`](./original.c) (fragmento ZenoC) · [`tool.json`](./tool.json).
