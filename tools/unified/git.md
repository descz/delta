# git (unificada)

**ops:** `status` · `diff` · `log` · `checkpoint`

**Cobre (4):** `git_status`, `git_diff`, `git_log`, `git_checkpoint`

```c
zt_invoke(h, "git", "{\"op\":\"status\"}", &out, &err);
```

Motor: `zt_git_op()` em `harness/src/zt_family_git.c`.
