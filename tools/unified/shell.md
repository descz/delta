# shell (unificada)

**ops:** `run` · `start` · `list` · `tail` · `cancel`

**Cobre (5):** `run_command`, `start_background_job`, `list_background_jobs`, `tail_background_job`, `cancel_background_job`

```c
zt_invoke(h, "shell", "{\"op\":\"run\"}", &out, &err);
```

Motor: `zt_shell_op()` em `harness/src/zt_family_shell.c`.
