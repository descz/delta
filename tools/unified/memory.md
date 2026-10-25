# memory (unificada)

**ops:** `remember` · `update` · `search` · `list` · `link` · `unlink` · `query` · `insert`

**Cobre (8):** `memory_remember`, `memory_update`, `memory_search`, `memory_list`, `memory_link`, `memory_unlink`, `db_query`, `db_insert`

```c
zt_invoke(h, "memory", "{\"op\":\"remember\"}", &out, &err);
```

Motor: `zt_memory_op()` em `harness/src/zt_family_memory.c`.
