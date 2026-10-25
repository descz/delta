# fs (unificada)

**ops:** `list` · `read` · `write` · `append` · `replace` · `mkdir` · `delete` · `search` · `glob`

**Cobre (9):** `list_workspace`, `read_text_file`, `write_text_file`, `append_text_file`, `replace_in_file`, `create_directory`, `delete_file`, `search_workspace`, `glob_workspace`

```c
zt_invoke(h, "fs", "{\"op\":\"list\"}", &out, &err);
```

Motor: `zt_fs_op()` em `harness/src/zt_family_files.c`.
