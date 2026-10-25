# plugin_<id>_<name>

**Categoria:** extension · **Efeito:** `WRITE_EXTERNAL`
**Perfis:** plugins · **Origem Zeno:** `zeno-bridge.c:753`
**Unificada equivalente:** _nenhuma (dinamica de plugin)_

Tools geradas dinamicamente a partir dos plugins/extensoes habilitados (Zeno JSON plugins). Cada tool de plugin vira "plugin_<plugin_id>_<tool_name>" e executa um template de comando shell com o schema montado em bridge_plugin_schema().

## Schema

```json
{
  "type": "object",
  "note": "schema derivado do template de comando do plugin"
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
int ok = zt_invoke(h, "plugin_<id>_<name>", "{}", &out, &err);
if (!ok) fprintf(stderr, "%s\n", err ? err : "falha");
printf("%s\n", out ? out : "");
free(out); free(err);
```

Arquivos desta pasta: [`port.c`](./port.c) (codigo pronto) · [`original.c`](./original.c) (fragmento ZenoC) · [`tool.json`](./tool.json).
