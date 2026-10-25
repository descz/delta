# mcp_call

**Categoria:** extension · **Efeito:** `WRITE_EXTERNAL`
**Perfis:** full, minimal · **Origem Zeno:** `ZenoC/src/zeno_tools.c:1362`
**Unificada equivalente:** `extend` (op `mcp`)

Call a tool on an external MCP server: pass server_url for HTTP JSON-RPC servers or command for stdio servers (e.g. "node server.js"); args_json carries the tool arguments; headers carries HTTP auth headers for server_url (e.g. {"X-API-KEY":"${COMPOSIO_API_KEY}"} - ${VAR} is read from the environment, never paste secrets literally).

## Schema

```json
{
  "type": "object",
  "properties": {
    "server_url": {
      "type": "string"
    },
    "command": {
      "type": "string",
      "description": "stdio server command line"
    },
    "headers": {
      "type": "object",
      "description": "HTTP headers for server_url; ${VAR} expands from the environment"
    },
    "tool_name": {
      "type": "string"
    },
    "args_json": {
      "type": "string"
    }
  },
  "required": [
    "tool_name"
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
int ok = zt_invoke(h, "mcp_call", "{"tool_name":"<texto>"}", &out, &err);
if (!ok) fprintf(stderr, "%s\n", err ? err : "falha");
printf("%s\n", out ? out : "");
free(out); free(err);
```

Arquivos desta pasta: [`port.c`](./port.c) (codigo pronto) · [`original.c`](./original.c) (fragmento ZenoC) · [`tool.json`](./tool.json).
