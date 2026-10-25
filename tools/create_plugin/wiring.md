# create_plugin

**Categoria:** extension · **Efeito:** `WRITE_LOCAL`
**Perfis:** studio · **Origem Zeno:** `ZenoC/src/zeno_studio.c:1951`
**Unificada equivalente:** `extend` (op `plugin`)

Generate a Zeno JSON plugin from a user request, save it automatically, and make it available. Use after confirming plugin name + one-sentence description.

## Schema

```json
{
  "type": "object",
  "properties": {
    "request": {
      "type": "string",
      "description": "what the plugin must do"
    },
    "name_hint": {
      "type": "string",
      "description": "short display name hint"
    },
    "plugins_dir": {
      "type": "string",
      "description": "plugins directory (default zeno_plugins)"
    }
  },
  "required": [
    "request"
  ]
}
```

## Hooks do host necessarios

- `host.mcp_call*`
- `host.agent_turn*`
- `host.read_file`
- `host.write_file`
- `host.exec`

## Como usar

```c
#include "zenotools.h"

zt_host host;
zt_host_stdio_at(&host, "/meu/workspace");
zt_harness *h = zt_harness_create(&host, "/meu/workspace");
zt_register_all(h);            /* ou zt_register_unified(h) para a versao consolidada */

char *out = NULL, *err = NULL;
int ok = zt_invoke(h, "create_plugin", "{"request":"<texto>"}", &out, &err);
if (!ok) fprintf(stderr, "%s\n", err ? err : "falha");
printf("%s\n", out ? out : "");
free(out); free(err);
```

Arquivos desta pasta: [`port.c`](./port.c) (codigo pronto) · [`original.c`](./original.c) (fragmento ZenoC) · [`tool.json`](./tool.json).
