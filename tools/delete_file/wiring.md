# delete_file

**Categoria:** files · **Efeito:** `WRITE_LOCAL`
**Perfis:** full, minimal · **Origem Zeno:** `ZenoC/src/zeno_tools.c:1322`
**Unificada equivalente:** `fs` (op `delete`)

Delete a workspace file you created and no longer need. The previous version is backed up to .Zeno_trash automatically, but verify nothing still references the file before deleting it.

## Schema

```json
{
  "type": "object",
  "properties": {
    "relative_path": {
      "type": "string"
    }
  },
  "required": [
    "relative_path"
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
int ok = zt_invoke(h, "delete_file", "{"relative_path":"exemplo.txt"}", &out, &err);
if (!ok) fprintf(stderr, "%s\n", err ? err : "falha");
printf("%s\n", out ? out : "");
free(out); free(err);
```

Arquivos desta pasta: [`port.c`](./port.c) (codigo pronto) · [`original.c`](./original.c) (fragmento ZenoC) · [`tool.json`](./tool.json).
