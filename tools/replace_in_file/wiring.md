# replace_in_file

**Categoria:** files · **Efeito:** `WRITE_LOCAL`
**Perfis:** full, minimal · **Origem Zeno:** `ZenoC/src/zeno_tools.c:1320`
**Unificada equivalente:** `fs` (op `replace`)

Replace exact text inside a workspace file.

## Schema

```json
{
  "type": "object",
  "properties": {
    "relative_path": {
      "type": "string"
    },
    "old_text": {
      "type": "string"
    },
    "new_text": {
      "type": "string"
    },
    "replace_all": {
      "type": "boolean"
    }
  },
  "required": [
    "relative_path",
    "old_text",
    "new_text"
  ]
}
```

## Hooks do host necessarios

- `host.read_file`
- `host.write_file`
- `host.make_dir`
- `host.remove_file`
- `host.list_dir`
- `host.glob`
- `host.search`
- `host.file_exists`

## Como usar

```c
#include "zenotools.h"

zt_host host;
zt_host_stdio_at(&host, "/meu/workspace");
zt_harness *h = zt_harness_create(&host, "/meu/workspace");
zt_register_all(h);            /* ou zt_register_unified(h) para a versao consolidada */

char *out = NULL, *err = NULL;
int ok = zt_invoke(h, "replace_in_file", "{"relative_path":"exemplo.txt","old_text":"<texto>","new_text":"<texto>"}", &out, &err);
if (!ok) fprintf(stderr, "%s\n", err ? err : "falha");
printf("%s\n", out ? out : "");
free(out); free(err);
```

Arquivos desta pasta: [`port.c`](./port.c) (codigo pronto) · [`original.c`](./original.c) (fragmento ZenoC) · [`tool.json`](./tool.json).
