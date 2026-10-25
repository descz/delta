# glob_workspace

**Categoria:** files · **Efeito:** `READ_LOCAL`
**Perfis:** full · **Origem Zeno:** `ZenoC/src/zeno_tools.c:1324`
**Unificada equivalente:** `fs` (op `glob`)

Return workspace paths matching a glob pattern.

## Schema

```json
{
  "type": "object",
  "properties": {
    "pattern": {
      "type": "string"
    },
    "relative_path": {
      "type": "string"
    },
    "max_results": {
      "type": "integer"
    }
  },
  "required": [
    "pattern"
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
int ok = zt_invoke(h, "glob_workspace", "{"pattern":"<texto>"}", &out, &err);
if (!ok) fprintf(stderr, "%s\n", err ? err : "falha");
printf("%s\n", out ? out : "");
free(out); free(err);
```

Arquivos desta pasta: [`port.c`](./port.c) (codigo pronto) · [`original.c`](./original.c) (fragmento ZenoC) · [`tool.json`](./tool.json).
