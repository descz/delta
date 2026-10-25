# http_request

**Categoria:** web · **Efeito:** `WRITE_EXTERNAL` · **requer aprovacao**
**Perfis:** full · **Origem Zeno:** `ZenoC/src/zeno_tools.c:1340`
**Unificada equivalente:** `web` (op `http`)

Perform an HTTP request.

## Schema

```json
{
  "type": "object",
  "properties": {
    "url": {
      "type": "string"
    },
    "method": {
      "type": "string"
    },
    "headers": {
      "type": "object"
    },
    "json_body": {},
    "text_body": {
      "type": "string"
    },
    "timeout_seconds": {
      "type": "integer"
    },
    "max_response_chars": {
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
int ok = zt_invoke(h, "http_request", "{"url":"<texto>"}", &out, &err);
if (!ok) fprintf(stderr, "%s\n", err ? err : "falha");
printf("%s\n", out ? out : "");
free(out); free(err);
```

Arquivos desta pasta: [`port.c`](./port.c) (codigo pronto) · [`original.c`](./original.c) (fragmento ZenoC) · [`tool.json`](./tool.json).
