# scrape_url

**Categoria:** web · **Efeito:** `READ_EXTERNAL` · **requer aprovacao**
**Perfis:** full · **Origem Zeno:** `ZenoC/src/zeno_tools.c:1354`
**Unificada equivalente:** `web` (op `scrape`)

Fetch a URL and extract readable text (approval required).

## Schema

```json
{
  "type": "object",
  "properties": {
    "url": {
      "type": "string"
    },
    "max_chars": {
      "type": "integer"
    }
  },
  "required": [
    "url"
  ]
}
```

## Hooks do host necessarios

- `host.http*`
- `host.browser_read*`

## Como usar

```c
#include "zenotools.h"

zt_host host;
zt_host_stdio_at(&host, "/meu/workspace");
zt_harness *h = zt_harness_create(&host, "/meu/workspace");
zt_register_all(h);            /* ou zt_register_unified(h) para a versao consolidada */

char *out = NULL, *err = NULL;
int ok = zt_invoke(h, "scrape_url", "{"url":"<texto>"}", &out, &err);
if (!ok) fprintf(stderr, "%s\n", err ? err : "falha");
printf("%s\n", out ? out : "");
free(out); free(err);
```

Arquivos desta pasta: [`port.c`](./port.c) (codigo pronto) · [`original.c`](./original.c) (fragmento ZenoC) · [`tool.json`](./tool.json).
