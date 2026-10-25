# memory_remember

**Categoria:** memory · **Efeito:** `WRITE_LOCAL`
**Perfis:** full · **Origem Zeno:** `ZenoC/src/zeno_tools.c:1330`
**Unificada equivalente:** `memory` (op `remember`)

Persist a durable memory note as a diary-style Markdown entry (see MEMORY WRITING FORMAT).

## Schema

```json
{
  "type": "object",
  "properties": {
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
    "title",
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
int ok = zt_invoke(h, "memory_remember", "{"title":"<texto>","content":"conteudo de exemplo\\n"}", &out, &err);
if (!ok) fprintf(stderr, "%s\n", err ? err : "falha");
printf("%s\n", out ? out : "");
free(out); free(err);
```

Arquivos desta pasta: [`port.c`](./port.c) (codigo pronto) · [`original.c`](./original.c) (fragmento ZenoC) · [`tool.json`](./tool.json).
