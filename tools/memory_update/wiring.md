# memory_update

**Categoria:** memory · **Efeito:** `WRITE_LOCAL`
**Perfis:** full · **Origem Zeno:** `ZenoC/src/zeno_tools.c:1332`
**Unificada equivalente:** `memory` (op `update`)

Update an existing durable memory note: pass id (from memory_search/memory_list) plus the fields to change (title, content, kind, scope, tags_json). Fields omitted are preserved; append new information instead of replacing it.

## Schema

```json
{
  "type": "object",
  "properties": {
    "id": {
      "type": "string"
    },
    "title": {
      "type": "string"
    },
    "content": {
      "type": "string"
    },
    "kind": {
      "type": "string"
    },
    "scope": {
      "type": "string"
    },
    "tags_json": {
      "type": "string"
    }
  },
  "required": [
    "id",
    "content"
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
int ok = zt_invoke(h, "memory_update", "{"id":"<texto>","content":"conteudo de exemplo\\n"}", &out, &err);
if (!ok) fprintf(stderr, "%s\n", err ? err : "falha");
printf("%s\n", out ? out : "");
free(out); free(err);
```

Arquivos desta pasta: [`port.c`](./port.c) (codigo pronto) · [`original.c`](./original.c) (fragmento ZenoC) · [`tool.json`](./tool.json).
